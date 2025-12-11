#pragma once

#include <vector>
#include <algorithm>
#include <Arduino.h>

#include "functions.hpp"
#include "app.hpp"
#include "windows.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
// semphr.h wird nicht mehr benötigt, da wir keinen Mutex verwenden
#include "esp_system.h"
#include "windows.hpp"

#include "../wifi/index.hpp"

// ---------------------- Globals ----------------------
extern TaskHandle_t WindowAppRenderHandle;

// Wir schützen runningTasks mit einem ESP32-Spinlock (Critical Section),
// damit es keine Owner-Asserts wie bei Mutexen gibt.
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
static portMUX_TYPE runningTasksLock = portMUX_INITIALIZER_UNLOCKED;
#else
static portMUX_TYPE runningTasksLock; // Fallback
#endif

static std::vector<TaskHandle_t> runningTasks;

// Kurze Hilfsmakros für atomare Zugriffe
#define RUNNING_TASKS_LOCK() taskENTER_CRITICAL(&runningTasksLock)
#define RUNNING_TASKS_UNLOCK() taskEXIT_CRITICAL(&runningTasksLock)

// ---------------------- Helpers: runningTasks ----------------------

// Füge Taskhandle (einmalig) ein
static void addRunningTask(TaskHandle_t h);

// Entferne Taskhandle, wenn vorhanden
static void removeRunningTask(TaskHandle_t h);

// ---------------------- App Run Task ----------------------
//
// Erwartet in pvParameters einen Zeiger auf eine heap-allokierte std::vector<String>
// args[0] = App-Pfad; args[1..] = App-Argumente
//
void AppRunTask(void *pvParameters);

// ---------------------- Start application in a new task ----------------------
//
// args wird kopiert und die Kopie (heap) in den Task übergeben.
//
bool executeApplication(const std::vector<String> &args);

// ---------------------- Persistent Render Task ----------------------
void AppRenderTask(void *pvParameters);

void startWindowRender();

// ---------------------- Task Monitor ----------------------
//
// Druckt High-Water-Marks aller bekannten Tasks,
// bereinigt gelöschte Tasks und loggt freien Heap.
//
void TaskMonitor(void *pvParameters);

void startTaskMonitor(unsigned priority = 1);
// ---------------------- Debug (single-shot) ----------------------
void debugTaskLog();