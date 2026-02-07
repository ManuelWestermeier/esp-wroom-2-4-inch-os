-- === Responsive Scrollable ToDo App ===
local winW, winH = 150, 150
local win = createWindow(10, 10, winW, winH)
WIN_setName(win, "ToDo App")

-- Load tasks
local tasks = {}
local saved = FS_get("todo_data")
if saved then tasks = load("return " .. saved)() end

-- Colors
local BG_COLOR = 0xFFFF
local TEXT_COLOR = 0x0000
local DONE_COLOR = 0x07E0
local BUTTON_COLOR = 0xF800

-- Scroll state
local scrollY = 0
local lastTouchY = nil

-- Track last drawn tasks to minimize redraw
local lastDrawn = {}

local function saveTasks() FS_set("todo_data", tostring(tasks)) end

local function addTask()
    local ok, text = WIN_readText(win, "New task:", "")
    if ok and text ~= "" then
        table.insert(tasks, {text = text, done = false})
        saveTasks()
    end
end

local function handleClick(x, y)
    local lineHeight = 20

    -- Add Task button
    if x >= 10 and x <= winW - 10 and y >= winH - 20 and y <= winH then
        addTask()
        return
    end

    -- Tasks
    for i, task in ipairs(tasks) do
        local ty = (i - 1) * lineHeight - scrollY + 25
        if ty >= 25 and ty <= winH - 25 then
            -- Delete button
            if x >= winW - 60 and x <= winW - 10 and y >= ty and y <= ty + 16 then
                table.remove(tasks, i)
                saveTasks()
                lastDrawn[i] = nil
                return
            end
            -- Toggle done
            if x >= 10 and x <= winW - 70 and y >= ty and y <= ty + 16 then
                task.done = not task.done
                lastDrawn[i] = nil
                saveTasks()
                return
            end
        end
    end
end

local function drawTasks()
    local lineHeight = 20

    -- Draw title
    if not lastDrawn.title then
        WIN_fillBg(win, 1, BG_COLOR)
        WIN_writeText(win, 1, 10, 5, "ToDo List", 2, TEXT_COLOR)
        lastDrawn.title = true
    end

    -- Draw visible tasks incrementally
    for i, task in ipairs(tasks) do
        local ty = (i - 1) * lineHeight - scrollY + 25
        if ty >= 25 and ty <= winH - 25 then
            if not lastDrawn[i] or lastDrawn[i].text ~= task.text or
                lastDrawn[i].done ~= task.done then
                -- Draw background rectangle for task area
                WIN_fillRect(win, 1, 10, ty, winW - 20, lineHeight, BG_COLOR)
                local col = task.done and DONE_COLOR or TEXT_COLOR
                WIN_writeText(win, 1, 10, ty, (i .. ". ") .. task.text, 1, col)
                WIN_drawRect(win, 1, winW - 60, ty, 50, 16, BUTTON_COLOR)
                WIN_writeText(win, 1, winW - 55, ty + 3, "Del", 1, 0xFFFF)
                lastDrawn[i] = {text = task.text, done = task.done}
            end
        end
    end

    -- Draw Add Task button
    if not lastDrawn.addButton then
        WIN_drawRect(win, 1, 10, winH - 20, winW - 20, 20, BUTTON_COLOR)
        WIN_writeText(win, 1, 15, winH - 17, "Add Task", 1, 0xFFFF)
        lastDrawn.addButton = true
    end
end

-- Main loop
while not WIN_closed(win) do
    drawTasks()

    local pressed, state, x, y, moveX, moveY, wasClicked, nr = WIN_getLastEvent(
                                                                   win, 1)

    if pressed then
        if state == 1 then
            lastTouchY = y
        elseif state == 2 and lastTouchY then
            scrollY = scrollY - (y - lastTouchY)
            if scrollY < 0 then scrollY = 0 end
            local maxScroll = math.max(0, #tasks * 20 - (winH - 50))
            if scrollY > maxScroll then scrollY = maxScroll end
            lastTouchY = y
        elseif state == 3 then
            if wasClicked then handleClick(x, y) end
            lastTouchY = nil
        end
    end

    WIN_finishFrame(win)
    delay(16)
end
