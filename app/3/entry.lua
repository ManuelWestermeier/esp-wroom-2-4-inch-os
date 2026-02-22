print("Paint start")

local win = createWindow(0, 0, 180, 180)
WIN_setName(win, "Paint")

local sidebarW = 26
local colorW = 18
local canvasX = sidebarW
local canvasW = 180 - sidebarW - colorW
local canvasH = 180

local white = 0xFFFF
local bg = RGB(230, 230, 230)
local text = 0x0000

local modes = {"circle", "square", "triangle"}
local mode = 1

local size = 8
local minS, maxS = 1, 24

local palette = {
    0x0000, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, RGB(255, 165, 0),
    RGB(128, 0, 128), RGB(128, 128, 128)
}
local color = palette[1]

local drawing = false
local lastX, lastY = nil, nil

-- ---------- UI ----------
local function drawUI()
    WIN_fillRect(win, 1, 0, 0, sidebarW, canvasH, bg)

    WIN_writeText(win, 1, 4, 4, modes[mode], 1, text)

    WIN_fillRect(win, 1, 3, 18, sidebarW - 6, 12, 0xF800)
    WIN_writeText(win, 1, 6, 20, "clr", 1, white)

    local sliderY = 40
    local sliderH = canvasH - 45
    WIN_fillRect(win, 1, sidebarW // 2, sliderY, 2, sliderH, 0x0000)

    local pct = (size - minS) / (maxS - minS)
    local y = sliderY + math.floor(pct * sliderH)
    WIN_fillRect(win, 1, sidebarW // 2 - 3, y - 3, 6, 6, color)
end

local function drawCanvas()
    WIN_fillRect(win, 1, canvasX, 0, canvasW, canvasH, white)
end

local function drawColors()
    local h = 18
    for i, c in ipairs(palette) do
        WIN_fillRect(win, 1, 180 - colorW, (i - 1) * h, colorW, h, c)
    end
end

-- ---------- drawing ----------
local function stamp(x, y)
    if x < canvasX or x > canvasX + canvasW then return end
    local r = size // 2

    if modes[mode] == "square" then
        WIN_fillRect(win, 1, x - r, y - r, size, size, color)
    elseif modes[mode] == "circle" then
        WIN_fillCircle(win, 1, x, y, r, color)
    else
        WIN_fillTriangle(win, 1, x, y - r, x - r, y + r, x + r, y + r, color)
    end
end

local function line(x1, y1, x2, y2)
    local dx, dy = x2 - x1, y2 - y1
    local dist = math.sqrt(dx * dx + dy * dy)
    local step = math.max(1, size // 2)
    local n = math.ceil(dist / step)

    for i = 0, n do
        local t = i / n
        stamp(x1 + dx * t, y1 + dy * t)
    end
end

-- ---------- input ----------
local function touch(x, y, state)
    -- sidebar
    if x < sidebarW then
        if y < 16 then
            mode = (mode % #modes) + 1
            drawUI()
            return
        elseif y < 32 then
            drawCanvas()
            return
        else
            local sliderY = 40
            local sliderH = canvasH - 45
            local pct = (y - sliderY) / sliderH
            size = math.max(minS, math.min(maxS, math.floor(
                                               minS + pct * (maxS - minS))))
            drawUI()
            return
        end
    end

    -- color picker
    if x > 180 - colorW then
        local idx = math.floor(y / 18) + 1
        if palette[idx] then
            color = palette[idx]
            drawUI()
        end
        return
    end

    -- canvas drawing
    if state == 0 then
        drawing = true
        lastX, lastY = x, y
        stamp(x, y)
    elseif state == 1 and drawing then
        line(lastX, lastY, x, y)
        lastX, lastY = x, y
    else
        drawing = false
    end
end

-- ---------- init ----------
drawCanvas()
drawUI()
drawColors()

while not WIN_closed(win) do
    local _, s, x, y, _, _, _, nr = WIN_getLastEvent(win, 1)

    if nr or s > 0 then
        if s >= 0 then touch(x, y, s) end
        WIN_finishFrame(win)
    end

    delay(8)
end

print("Paint exit")
