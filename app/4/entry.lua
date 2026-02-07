-- todo_app.lua
-- Simple Todo List app for ESP32 windowing system
local win = createWindow(0, 12, 320, 220)
WIN_setName(win, "Todo App")

-- Config
local margin = 8
local taskHeight = 24
local tasks = {} -- list of {text, done}
local staticDirty = true

-- Layout
local layout = {}
local function computeLayout()
    local x, y, w, h = WIN_getRect(win)
    layout.winW, layout.winH = w, h

    layout.toolbar = {x = margin, y = margin, w = w - 2 * margin, h = 40}
    layout.taskArea = {
        x = margin,
        y = layout.toolbar.y + layout.toolbar.h + 6,
        w = w - 2 * margin,
        h = h - layout.toolbar.h - 2 * margin
    }
    layout.btnAdd = {
        x = layout.toolbar.x + layout.toolbar.w - 60,
        y = layout.toolbar.y + 6,
        w = 50,
        h = 28
    }
end

-- Draw static UI (toolbar, buttons)
local function drawStatic()
    WIN_fillBg(win, 1, 0xFFFF)
    local tb = layout.toolbar
    WIN_fillRect(win, 1, tb.x, tb.y, tb.w, tb.h, 0xFFFF)
    WIN_drawRect(win, 1, tb.x, tb.y, tb.w, tb.h, 0x0000)
    WIN_writeText(win, 1, tb.x + 6, tb.y + 8, "Todo List", 2, 0x0000)

    -- Add button
    local b = layout.btnAdd
    WIN_fillRect(win, 1, b.x, b.y, b.w, b.h, 0xFFFF)
    WIN_drawRect(win, 1, b.x, b.y, b.w, b.h, 0x0000)
    WIN_writeText(win, 1, b.x + 10, b.y + 6, "Add", 1, 0x0000)

    staticDirty = false
end

-- Draw tasks
local function drawTasks()
    local x, y = layout.taskArea.x, layout.taskArea.y
    local w = layout.taskArea.w
    WIN_fillRect(win, 1, x, y, w, layout.taskArea.h, 0xFFFF)

    for i, task in ipairs(tasks) do
        local ty = y + (i - 1) * taskHeight
        local color = task.done and 0x07E0 or 0x0000 -- green if done
        WIN_writeText(win, 1, x + 6, ty + 4, task.text, 1, color)
        WIN_drawRect(win, 1, x, ty, w, taskHeight, 0x0000)
        task.rect = {x = x, y = ty, w = w, h = taskHeight} -- hitbox for toggling
    end
end

-- Add a new task
local function addTask()
    local ok, text = WIN_readText(win, "Enter new task:", "")
    if ok and text and #text > 0 then
        tasks[#tasks + 1] = {text = text, done = false}
    end
end

-- Handle touch events
local function handleTouch(x, y)
    -- Add button
    if pointInRect(x, y, layout.btnAdd) then
        addTask()
        return
    end

    -- Toggle task done
    for _, task in ipairs(tasks) do
        if pointInRect(x, y, task.rect) then
            task.done = not task.done
            break
        end
    end
end

-- Point in rectangle
function pointInRect(px, py, r)
    return px >= r.x and px <= r.x + r.w and py >= r.y and py <= r.y + r.h
end

-- Main loop
computeLayout()
staticDirty = true
while not WIN_closed(win) do
    local pressed, state, x, y, mx, my, wc, nr = WIN_getLastEvent(win, 1)

    if nr or state > 0 then
        WIN_finishFrame(win)

        if staticDirty then drawStatic() end

        if state > 0 then handleTouch(x, y) end

        drawTasks()
    end

    delay(16)
end

WIN_close(win)
