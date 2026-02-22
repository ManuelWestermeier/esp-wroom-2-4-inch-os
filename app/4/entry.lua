-- Minimal Todo List (180x180 optimized)
local win = createWindow(0, 0, 180, 180)
WIN_setName(win, "Todo")

local KEY = "todo_v1"
local tasks = {}
local scroll = 0
local itemH = 22

-- load / save
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

    -- header
    WIN_fillRect(win, 1, 0, 0, 180, 28, 0x07E0)
    WIN_writeText(win, 1, 6, 8, "Todo", 2, 0xFFFF)

    -- add button
    WIN_fillRect(win, 1, 130, 4, 46, 20, 0xF800)
    WIN_writeText(win, 1, 138, 8, "+Add", 1, 0xFFFF)

    local y0 = 32
    local visible = math.floor((180 - 50) / itemH)

    for i = 1, visible do
        local idx = scroll + i
        if idx > #tasks then break end

        local y = y0 + (i - 1) * itemH
        local t = tasks[idx]

        -- checkbox
        WIN_drawRect(win, 1, 6, y + 4, 12, 12, 0x0000)
        if t.d then WIN_fillRect(win, 1, 8, y + 6, 8, 8, 0x07E0) end

        -- text
        WIN_writeText(win, 1, 24, y + 5, t.t, 1, 0x0000)

        -- delete X
        WIN_drawLine(win, 1, 158, y + 4, 172, y + 18, 0xF800)
        WIN_drawLine(win, 1, 172, y + 4, 158, y + 18, 0xF800)
    end

    WIN_writeText(win, 1, 6, 162, #tasks .. " items", 1, 0x0000)
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
    if y < 28 and x > 128 then
        add()
        return
    end

    local row = math.floor((y - 32) / itemH) + 1
    local i = scroll + row
    if not tasks[i] then return end

    if x < 20 then
        tasks[i].d = not tasks[i].d
        save()
    elseif x > 154 then
        table.remove(tasks, i)
        save()
    else
        edit(i)
    end
end

load()

while not WIN_closed(win) do
    local _, s, x, y, _, _, _, nr = WIN_getLastEvent(win, 1)

    if nr or s > 0 then
        if s > 0 then touch(x, y) end
        draw()
    end

    delay(16)
end
