-- Minimal Todo List (ESP32 Display OS)
-- tap checkbox = toggle
-- tap text = edit
-- tap X = delete
-- + Add button
-- renders only when needed
local win = createWindow(0, 0, 240, 320)
WIN_setName(win, "Todo")

local KEY = "todo_v1"
local tasks = {}
local scroll = 0
local itemH = 28

-- load/save
local function load()
    local d = FS_get(KEY)
    if not d then return end
    for line in string.gmatch(d, "[^\n]+") do
        local m, t = line:match("(%d)|(.+)")
        if m then table.insert(tasks, {t = t, d = m == "1"}) end
    end
end

local function save()
    local out = {}
    for _, v in ipairs(tasks) do
        table.insert(out, (v.d and "1|" or "0|") .. v.t)
    end
    FS_set(KEY, table.concat(out, "\n"))
end

local function draw()
    WIN_fillBg(win, 1, 0xFFFF)
    WIN_fillRect(win, 1, 0, 0, 240, 40, 0x07E0)
    WIN_writeText(win, 1, 8, 12, "Todo", 3, 0xFFFF)

    WIN_fillRect(win, 1, 180, 6, 50, 28, 0xF800)
    WIN_writeText(win, 1, 192, 12, "+Add", 2, 0xFFFF)

    local y0 = 44
    local visible = math.floor((320 - 80) / itemH)

    for i = 1, visible do
        local idx = scroll + i
        if idx > #tasks then break end
        local y = y0 + (i - 1) * itemH
        local t = tasks[idx]

        WIN_drawRect(win, 1, 10, y + 6, 16, 16, 0x0000)
        if t.d then WIN_fillRect(win, 1, 12, y + 8, 12, 12, 0x07E0) end

        WIN_writeText(win, 1, 34, y + 8, t.t, 2, 0x0000)

        WIN_drawLine(win, 1, 210, y + 6, 230, y + 22, 0xF800)
        WIN_drawLine(win, 1, 230, y + 6, 210, y + 22, 0xF800)
    end

    WIN_writeText(win, 1, 8, 300, #tasks .. " items", 2, 0x0000)
    WIN_finishFrame(win)
end

local function add()
    local ok, t = WIN_readText(win, "New task:", "")
    if ok and t ~= "" then
        table.insert(tasks, 1, {t = t, d = false})
        save()
    end
end

local function edit(i)
    local ok, t = WIN_readText(win, "Edit:", tasks[i].t)
    if ok and t ~= "" then
        tasks[i].t = t
        save()
    end
end

local function touch(x, y)
    if y < 40 and x > 180 then
        add()
        return
    end

    local row = math.floor((y - 44) / itemH) + 1
    local i = scroll + row
    if not tasks[i] then return end

    if x < 30 then
        tasks[i].d = not tasks[i].d
        save()
    elseif x > 205 then
        table.remove(tasks, i)
        save()
    else
        edit(i)
    end
end

load()

while not WIN_closed(win) do
    local p, s, x, y, _, _, _, nr = WIN_getLastEvent(win, 1)

    if nr or s > 0 then
        if s > 0 then touch(x, y) end
        draw()
    end

    delay(16)
end
