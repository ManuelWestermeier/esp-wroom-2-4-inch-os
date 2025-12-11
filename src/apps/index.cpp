#include "./index.hpp"
// ---------------------- Globals ----------------------
TaskHandle_t WindowAppRenderHandle = NULL;

// Füge Taskhandle (einmalig) ein
static void addRunningTask(TaskHandle_t h)
{
    if (!h)
        return;
    RUNNING_TASKS_LOCK();
    if (std::find(runningTasks.begin(), runningTasks.end(), h) == runningTasks.end())
        runningTasks.push_back(h);
    RUNNING_TASKS_UNLOCK();
}

// Entferne Taskhandle, wenn vorhanden
static void removeRunningTask(TaskHandle_t h)
{
    if (!h)
        return;
    RUNNING_TASKS_LOCK();
    auto it = std::find(runningTasks.begin(), runningTasks.end(), h);
    if (it != runningTasks.end())
        runningTasks.erase(it);
    RUNNING_TASKS_UNLOCK();
}

// ---------------------- App Run Task ----------------------
//
// Erwartet in pvParameters einen Zeiger auf eine heap-allokierte std::vector<String>
// args[0] = App-Pfad; args[1..] = App-Argumente
//
void AppRunTask(void *pvParameters)
{
    esp_task_wdt_delete(NULL); // unregister this task
    auto taskArgsPtr = static_cast<std::vector<String> *>(pvParameters);
    std::vector<String> args = *taskArgsPtr;
    delete taskArgsPtr; // Übergabe-Speicher freigeben

    // Ab jetzt "läuft" der Task und gehört in die Liste
    addRunningTask(xTaskGetCurrentTaskHandle());

    Serial.println("Running Lua app...");

    std::vector<String> appArgs;
    if (args.size() > 1)
        appArgs.assign(args.begin() + 1, args.end());

    // App ausführen
    LuaApps::App app(args[0], appArgs);
    int result = app.run();
    Serial.printf("Lua App exited with code: %d\n", result);

    // Sich selbst aus der Liste entfernen und beenden
    removeRunningTask(xTaskGetCurrentTaskHandle());
    vTaskDelete(NULL);
}

// ---------------------- Start application in a new task ----------------------
//
// args wird kopiert und die Kopie (heap) in den Task übergeben.
//
bool executeApplication(const std::vector<String> &args)
{
    if (args.empty())
    {
        Serial.println("ERROR: no execute path specified");
        return false;
    }

    // Serial.println(runningTasks[0].name);-
    if (runningTasks.size() > 1)
    {
        return false;
    }

    // Heap-allokierte Kopie der Argumente. AppRunTask löscht sie.
    auto taskArgsPtr = new std::vector<String>(args);

    TaskHandle_t WindowAppRunHandle = NULL;
    BaseType_t res = xTaskCreate(
        AppRunTask,                          // task function
        (String("App>>") + args[0]).c_str(), // name
        8172,                                // stack (words) -> erhöht für Stabilität
        taskArgsPtr,                         // parameter (heap-allocated copy)
        1,                                   // priority
        &WindowAppRunHandle                  // task handle (optional)
    );

    if (res != pdPASS)
    {
        Serial.println("ERROR: failed to create AppRunTask");
        delete taskArgsPtr; // bei Fehler Speicher freigeben
        return false;
    }

    return true;
}

// ---------------------- Persistent Render Task ----------------------
void AppRenderTask(void *pvParameters)
{
    esp_task_wdt_delete(NULL); // unregister this task
    (void)pvParameters;
    // Render-Task in die Liste aufnehmen
    addRunningTask(xTaskGetCurrentTaskHandle());

    while (true)
    {
        Windows::loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void startWindowRender()
{
    // Falls bereits gestartet, nicht nochmal starten
    if (WindowAppRenderHandle != NULL)
    {
        eTaskState s = eTaskGetState(WindowAppRenderHandle);
        if (s != eDeleted)
        {
            Serial.println("AppRenderTask already running");
            return;
        }
        // Falls gelöscht, Handle zurücksetzen
        WindowAppRenderHandle = NULL;
    }

    BaseType_t res = xTaskCreate(
        AppRenderTask,
        "AppRenderTask",
        8172, // etwas größerer Stack
        NULL,
        2, // etwas höhere Prio als App-Tasks
        &WindowAppRenderHandle);

    if (res != pdPASS)
    {
        Serial.println("ERROR: failed to create AppRenderTask");
        WindowAppRenderHandle = NULL;
        return;
    }

    // Kein addRunningTask() hier -> macht der Task selbst am Anfang
}

// ---------------------- Task Monitor ----------------------
//
// Druckt High-Water-Marks aller bekannten Tasks,
// bereinigt gelöschte Tasks und loggt freien Heap.
//
void TaskMonitor(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        // Globaler Heap-Status
        Serial.printf("[TaskMonitor] Free heap: %u bytes, MaxAlloc: %u bytes\n",
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap());

        // Schnappschuss der Handles unter kurzer Critical-Section
        std::vector<TaskHandle_t> snapshot;
        RUNNING_TASKS_LOCK();
        snapshot = runningTasks;
        RUNNING_TASKS_UNLOCK();

        // Außerhalb der Critical-Section über Handles iterieren
        for (TaskHandle_t h : snapshot)
        {
            if (!h)
                continue;

            eTaskState state = eTaskGetState(h);

            if (state == eDeleted)
            {
                // Aus der echten Liste entfernen und melden
                removeRunningTask(h);
                Serial.printf("Task %p state=DELETED -> removed\n", (void *)h);
                continue;
            }

            // High-Water-Mark abfragen
            UBaseType_t highWords = uxTaskGetStackHighWaterMark(h);
            unsigned int highBytes = (unsigned int)(highWords * sizeof(StackType_t));
            UBaseType_t prio = uxTaskPriorityGet(h);

            // Optionale Namensausgabe (wenn verfügbar)
            const char *name = pcTaskGetTaskName(h);
            if (name == nullptr)
                name = "?";

            Serial.printf("Task %p name=%s prio=%u state=%d highWater=%u bytes\n",
                          (void *)h, name, (unsigned)prio, (int)state, highBytes);
        }

        // Einmal pro Sekunde
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void startTaskMonitor(unsigned priority)
{
    // Nur einen Monitor starten
    static TaskHandle_t monitorHandle = NULL;

    if (monitorHandle != NULL)
    {
        eTaskState s = eTaskGetState(monitorHandle);
        if (s != eDeleted)
        {
            Serial.println("TaskMonitor already running");
            return;
        }
        monitorHandle = NULL;
    }

    BaseType_t res = xTaskCreate(
        TaskMonitor,
        "TaskMonitor",
        3072, // moderater Stack
        NULL,
        priority,
        &monitorHandle);

    if (res != pdPASS)
    {
        Serial.println("ERROR: failed to create TaskMonitor");
        return;
    }

    // Der Monitor taucht in der Liste auf, sobald er addRunningTask() ruft?
    // -> Nein, der Monitor verwaltet andere Tasks und muss selbst nicht
    // in runningTasks erscheinen. Wenn gewünscht, hier einkommentieren:
    // addRunningTask(monitorHandle);
}

// ---------------------- Debug (single-shot) ----------------------
void debugTaskLog()
{
    Serial.println("Min,Nor,Max");
    Serial.println(ESP.getMinFreeHeap());
    Serial.println(ESP.getFreeHeap());
    Serial.println(ESP.getMaxAllocHeap());

    if (WindowAppRenderHandle)
    {
        Serial.printf("AppRenderTask free stack: %u bytes\n",
                      (unsigned int)(uxTaskGetStackHighWaterMark(WindowAppRenderHandle) * sizeof(StackType_t)));
    }
    else
    {
        Serial.println("AppRenderTask handle not set");
    }

    // Optional: Alle bekannten Tasks einmalig loggen
    std::vector<TaskHandle_t> snapshot;
    RUNNING_TASKS_LOCK();
    snapshot = runningTasks;
    RUNNING_TASKS_UNLOCK();

    for (TaskHandle_t h : snapshot)
    {
        if (!h)
            continue;
        UBaseType_t highWords = uxTaskGetStackHighWaterMark(h);
        unsigned int highBytes = (unsigned int)(highWords * sizeof(StackType_t));
        const char *name = pcTaskGetTaskName(h);
        if (!name)
            name = "?";
        Serial.printf("[debugTaskLog] Task %p name=%s highWater=%u bytes\n",
                      (void *)h, name, highBytes);
    }
}
