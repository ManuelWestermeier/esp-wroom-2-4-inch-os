-- === Scrollable ToDo App for ESP32 OS ===
local win = createWindow(10, 10, 300, 220)
WIN_setName(win, "ToDo App")

-- Load tasks
local tasks = {}
local saved = FS_get("todo_data")
if saved then
    tasks = load("return "..saved)()  -- deserialize table
end

-- Colors
local BG_COLOR = 0xFFFF
local TEXT_COLOR = 0x0000
local DONE_COLOR = 0x07E0
local BUTTON_COLOR = 0xF800

-- Scroll state
local scrollY = 0
local lastTouchY = nil

-- Save tasks to FS
local function saveTasks()
    FS_set("todo_data", tostring(tasks))
end

-- Add a task
local function addTask()
    local ok, text = WIN_readText(win, "New task:", "")
    if ok and text ~= "" then
        table.insert(tasks, {text=text, done=false})
        saveTasks()
    end
end

-- Handle a click or tap
local function handleClick(x, y)
    local visibleStart = scrollY
    local visibleEnd = scrollY + 180  -- window content height
    local lineHeight = 20

    -- Check Add Task button
    if x >= 10 and x <= 290 and y >= 200 and y <= 220 then
        addTask()
        return
    end

    -- Check Delete buttons & toggle done
    for i, task in ipairs(tasks) do
        local ty = (i-1)*lineHeight - scrollY + 25
        if ty >= 25 and ty <= 200 then
            if x >= 200 and x <= 260 and y >= ty and y <= ty+16 then
                table.remove(tasks, i)
                saveTasks()
                return
            elseif x >= 10 and x <= 190 and y >= ty and y <= ty+16 then
                task.done = not task.done
                saveTasks()
                return
            end
        end
    end
end

-- Draw visible tasks
local function drawTasks()
    WIN_fillBg(win, 1, BG_COLOR)
    WIN_writeText(win, 1, 10, 5, "ToDo List", 2, TEXT_COLOR)

    local lineHeight = 20
    for i, task in ipairs(tasks) do
        local ty = (i-1)*lineHeight - scrollY + 25
        if ty >= 25 and ty <= 200 then  -- only draw visible
            local col = task.done and DONE_COLOR or TEXT_COLOR
            WIN_writeText(win, 1, 10, ty, (i..". ")..task.text, 1, col)
            WIN_drawRect(win, 1, 200, ty, 60, 16, BUTTON_COLOR)
            WIN_writeText(win, 1, 205, ty+3, "Del", 1, 0xFFFF)
        end
    end

    -- Add Task button
    WIN_drawRect(win, 1, 10, 200, 280, 20, BUTTON_COLOR)
    WIN_writeText(win, 1, 15, 203, "Add Task", 1, 0xFFFF)
end

-- Main loop
while not WIN_closed(win) do
    drawTasks()

    local pressed, state, x, y, moveX, moveY, wasClicked = WIN_getLastEvent(win, 1)

    if pressed then
        if state == 1 then
            -- Start dragging for scroll
            lastTouchY = y
        elseif state == 2 and lastTouchY then
            -- Dragging: adjust scroll
            scrollY = scrollY - (y - lastTouchY)
            if scrollY < 0 then scrollY = 0 end
            local maxScroll = math.max(0, #tasks*20 - 180)
            if scrollY > maxScroll then scrollY = maxScroll end
            lastTouchY = y
        elseif state == 3 then
            -- Touch released, handle click
            if wasClicked then
                handleClick(x, y)
            end
            lastTouchY = nil
        end
    end

    delay(16) -- ~60 FPS
end
