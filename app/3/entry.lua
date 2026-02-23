-- Paint app (easy) - 4 colors, single size select, redraw UI only on resize/clear
print("Paint start")

local win = createWindow(10, 10, 180, 180)
WIN_setName(win, "Paint")

local function int(n) return math.floor(tonumber(n) or 0) end

-- layout (base values; actual canvas size tracked from WIN_getRect)
local sidebarW = 28
local colorW = 18

local white = 0xFFFF
local bg = RGB(230, 230, 230)
local text = 0x0000

local modes = {"circle", "square", "triangle"}
local mode = 1

local size = 8
local minS, maxS = 2, 28

-- Simple 4-color palette
local palette = { 0x0000, 0xF800, 0x07E0, 0x001F }
local color = palette[2]

local drawing = false
local lastX, lastY = nil, nil

-- cached window rect to detect resize
local prevW, prevH = 0, 0
local canvasX, canvasY, canvasW, canvasH

local function updateLayout()
    local x,y,w,h = WIN_getRect(win)
    canvasX = sidebarW
    canvasY = 0
    canvasW = w - sidebarW - colorW
    canvasH = h
end

local function drawUI()
    -- sidebar background
    WIN_fillRect(win, 1, 0, 0, sidebarW, canvasH, bg)
    -- mode text
    WIN_writeText(win, 1, 4, 4, modes[mode], 1, text)
    -- clear button
    WIN_fillRect(win, 1, 4, 22, sidebarW - 8, 14, 0xF800)
    WIN_writeText(win, 1, 6, 24, "CLR", 1, white)
    -- size display and +/- buttons
    WIN_writeText(win, 1, 4, 44, "Size", 1, text)
    WIN_fillRect(win, 1, 6, 60, 8, 12, 0x07E0) -- -
    WIN_writeText(win, 1, 8, 62, "-", 1, white)
    WIN_fillRect(win, 1, 18, 60, 8, 12, 0x07E0) -- +
    WIN_writeText(win, 1, 20, 62, "+", 1, white)
    WIN_writeText(win, 1, 6, 76, tostring(size), 1, text)
    -- draw color strip background
    WIN_fillRect(win, 1, canvasX + canvasW, 0, colorW, canvasH, 0xFFFF)
end

local function drawCanvas(clear)
    if clear then
        WIN_fillRect(win, 1, canvasX, canvasY, canvasW, canvasH, white)
    end
end

local function drawColors()
    local h = int(canvasH / #palette)
    for i, c in ipairs(palette) do
        local y = (i - 1) * h
        WIN_fillRect(win, 1, canvasX + canvasW, y, colorW, h, c)
    end
end

local function inCanvas(x, y)
    return x >= canvasX and x < canvasX + canvasW and y >= canvasY and y < canvasY + canvasH
end

local function stamp(x, y)
    if not inCanvas(x, y) then return end
    local r = int(math.max(1, size // 2))
    if modes[mode] == "square" then
        WIN_fillRect(win, 1, int(x - r), int(y - r), int(size), int(size), color)
    elseif modes[mode] == "circle" then
        WIN_fillCircle(win, 1, int(x), int(y), r, color)
    else
        WIN_fillTriangle(win, 1, int(x), int(y - r), int(x - r), int(y + r), int(x + r), int(y + r), color)
    end
end

local function line(x1, y1, x2, y2)
    local dx, dy = x2 - x1, y2 - y1
    local dist = math.sqrt(dx*dx + dy*dy)
    local step = math.max(1, size // 2)
    local n = math.ceil(dist / step)
    for i = 0, n do
        local t = i / n
        stamp(x1 + dx * t, y1 + dy * t)
    end
end

-- initial layout + draw UI + canvas
updateLayout()
drawCanvas(true)
drawUI()
drawColors()
WIN_finishFrame(win)

-- main loop
while not WIN_closed(win) do
    local pressed, state, x, y, mx, my, wc, needRedraw = WIN_getLastEvent(win, 1)
    -- detect resize
    local rx, ry, rw, rh = WIN_getRect(win)
    if rw ~= prevW or rh ~= prevH then
        prevW, prevH = rw, rh
        updateLayout()
        drawCanvas(true)    -- clear canvas on resize
        drawUI()
        drawColors()
        WIN_finishFrame(win)
    end

    if needRedraw or state > 0 then
        -- handle touches only when event present
        if state >= 0 then
            -- sidebar actions
            if x < sidebarW then
                if y >= 22 and y < 36 then
                    -- clear
                    drawCanvas(true)
                    WIN_finishFrame(win)
                elseif y >= 60 and y < 72 then
                    -- minus
                    size = math.max(minS, size - 2)
                    drawUI(); WIN_finishFrame(win)
                elseif y >= 60 and y < 72 and x >= 18 then
                    size = math.min(maxS, size + 2)
                    drawUI(); WIN_finishFrame(win)
                elseif y < 22 then
                    mode = (mode % #modes) + 1
                    drawUI(); WIN_finishFrame(win)
                end
            -- color picker
            elseif x >= canvasX + canvasW then
                local idx = int(y / math.max(1, canvasH / #palette)) + 1
                if palette[idx] then
                    color = palette[idx]
                    drawUI(); WIN_finishFrame(win)
                end
            else
                -- canvas drawing
                if state == 0 then
                    drawing = true
                    lastX, lastY = x, y
                    stamp(x, y)
                    WIN_finishFrame(win)
                elseif state == 1 and drawing then
                    line(lastX, lastY, x, y)
                    lastX, lastY = x, y
                    WIN_finishFrame(win)
                else
                    drawing = false
                end
            end
        end
    end

    delay(10)
end

print("Paint exit")