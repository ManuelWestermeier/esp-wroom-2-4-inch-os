-- Simple Todo List for ESP32 OS (2.4/2.8" display)
-- Single-file app. Saves tasks to FS under key "todolist_v1"
-- Renders only when WIN_getLastEvent indicates redraw or an input event occurred.
local WIN = {}
local win = createWindow(0, 0, 240, 320)
WIN_setName(win, "Todo")
-- constants
local BG = 0xFFFF
local TEXT = 0x0000
local PRIMARY = 0x07E0
local ACCENT = 0xF800
local ITEM_H = 28
local HEADER_H = 44
local FOOTER_H = 28
local STORE_KEY = "todolist_v1"

-- state
local tasks = {}
local scroll = 0
local need_full_redraw = true

-- storage helpers (simple newline-separated values, prefix "1|" for done / "0|" for not done)
local function load_tasks()
    local raw = FS_get(STORE_KEY)
    tasks = {}
    if not raw or raw == "" then return end
    for line in string.gmatch(raw, "([^\n]*)\n?") do
        if line and line ~= "" then
            local marker, rest = string.match(line, "^(%d)|(.+)$")
            if marker and rest then
                table.insert(tasks, {text = rest, done = (marker == "1")})
            end
        end
    end
end

local function save_tasks()
    local lines = {}
    for i, t in ipairs(tasks) do
        table.insert(lines, (t.done and "1|" or "0|") .. t.text)
    end
    FS_set(STORE_KEY, table.concat(lines, "\n"))
end

-- utils
local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

-- rendering
local function render()
    WIN_fillBg(win, 1, BG)

    -- header
    WIN_fillRect(win, 1, 0, 0, 240, HEADER_H, PRIMARY)
    WIN_writeText(win, 1, 8, 12, "To-Do List", 3, 0xFFFF)

    -- add button (top-right)
    WIN_fillRoundRect(win, 1, 180, 8, 52, 28, 6, ACCENT)
    WIN_writeText(win, 1, 196, 14, "+ Add", 2, 0xFFFF)

    -- list area background
    WIN_drawRect(win, 1, 6, HEADER_H + 4, 240 - 12,
                 320 - HEADER_H - FOOTER_H - 12, TEXT)

    local list_y = HEADER_H + 8
    local view_h = 320 - HEADER_H - FOOTER_H - 16
    local visible = math.floor(view_h / ITEM_H)
    visible = math.max(1, visible)

    -- draw items
    for i = 1, visible do
        local idx = scroll + i
        local y = list_y + (i - 1) * ITEM_H
        if idx <= #tasks then
            local t = tasks[idx]
            -- background for even/odd
            if (idx % 2) == 0 then
                WIN_fillRect(win, 1, 8, y, 224, ITEM_H - 2, 0xFFEF) -- subtle gray (approx)
            end
            -- checkbox
            WIN_drawRect(win, 1, 12, y + 6, 18, 18, TEXT)
            if t.done then
                WIN_fillRect(win, 1, 14, y + 8, 14, 14, 0x07E0)
            end
            -- text (clip if long)
            local text = t.text
            WIN_writeText(win, 1, 36, y + 8, text, 2, TEXT)
            -- delete icon (small X) on right
            WIN_drawLine(win, 1, 210, y + 6, 230, y + 22, ACCENT)
            WIN_drawLine(win, 1, 230, y + 6, 210, y + 22, ACCENT)
        else
            -- empty slot (do nothing)
        end
    end

    -- footer: scroll indicators and count
    WIN_fillRect(win, 1, 0, 320 - FOOTER_H, 240, FOOTER_H, PRIMARY)
    WIN_writeText(win, 1, 8, 320 - FOOTER_H + 6, tostring(#tasks) .. " items",
                  2, 0xFFFF)

    -- up/down arrows
    local can_up = scroll > 0
    local can_down = (scroll + visible) < #tasks
    if can_up then
        WIN_writeText(win, 1, 140, 320 - FOOTER_H + 6, "▲", 2, 0xFFFF)
    end
    if can_down then
        WIN_writeText(win, 1, 168, 320 - FOOTER_H + 6, "▼", 2, 0xFFFF)
    end

    WIN_finishFrame(win)
end

-- input handling (touch)
local function handle_touch(x, y, state)
    -- header add button
    if y >= 8 and y <= 36 and x >= 180 and x <= 232 then
        -- Add new task using blocking text input
        local ok, text = WIN_readText(win, "New task:", "")
        if ok and text and text ~= "" then
            table.insert(tasks, 1, {text = text, done = false})
            scroll = 0
            save_tasks()
            need_full_redraw = true
        end
        return
    end

    -- footer arrows
    if y >= (320 - FOOTER_H) and y <= 320 then
        if x >= 140 and x <= 156 and scroll > 0 then
            scroll = clamp(scroll - 1, 0, math.max(0, #tasks - 1))
            need_full_redraw = true
            return
        end
        if x >= 168 and x <= 184 and
            (scroll + math.floor((320 - HEADER_H - FOOTER_H - 16) / ITEM_H)) <
            #tasks then
            scroll = clamp(scroll + 1, 0, math.max(0, #tasks - 1))
            need_full_redraw = true
            return
        end
    end

    -- list taps: toggle checkbox or delete
    if y >= HEADER_H + 8 and y <= 320 - FOOTER_H - 8 then
        local list_y = HEADER_H + 8
        local rel = y - list_y
        local row = math.floor(rel / ITEM_H) + 1
        local idx = scroll + row
        if idx >= 1 and idx <= #tasks then
            local item_y = list_y + (row - 1) * ITEM_H
            -- checkbox area (12..30)
            if x >= 12 and x <= 30 then
                tasks[idx].done = not tasks[idx].done
                save_tasks()
                need_full_redraw = true
                return
            end
            -- delete area (210..230)
            if x >= 210 and x <= 234 then
                -- quick delete
                table.remove(tasks, idx)
                save_tasks()
                -- adjust scroll if needed
                local visible = math.floor(
                                    (320 - HEADER_H - FOOTER_H - 16) / ITEM_H)
                if scroll > 0 and (scroll + visible) > #tasks then
                    scroll = math.max(0, #tasks - visible)
                end
                need_full_redraw = true
                return
            end
            -- otherwise tapped item text -> edit
            local ok, newtext = WIN_readText(win, "Edit task:", tasks[idx].text)
            if ok and newtext and newtext ~= tasks[idx].text then
                tasks[idx].text = newtext
                save_tasks()
                need_full_redraw = true
            end
        end
    end
end

-- init
load_tasks()
need_full_redraw = true
local last_event_frame = WIN_lastChanged and WIN_lastChanged() or 0

-- main loop
while not WIN_closed(win) do
    local pressed, state, x, y, mx, my, wasClicked, needRedraw =
        WIN_getLastEvent(win, 1)
    -- needRedraw is numeric / boolean from API examples above (nr)
    if needRedraw or state > 0 or need_full_redraw then
        -- handle tap events immediately if state > 0
        if state > 0 and x and y then handle_touch(x, y, state) end

        render()
        need_full_redraw = false
    end

    -- small sleep to yield CPU
    delay(16)
end

-- on close, save
save_tasks()
WIN_close(win)
