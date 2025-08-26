-- fast_paint.lua
print("APP:STARTED")

local win = createWindow(20, 20, 240, 160)
WIN_setName(win, "Paint")

local bigScreen = 1
local leftScreen = 2

local totalW, totalH = 240, 160
local sidebarW = math.floor(totalW * 0.15)
local colorBarW = 24
local canvasX = sidebarW
local canvasW = totalW - sidebarW - colorBarW
local canvasH = totalH

local bgColor = RGB(230, 230, 230)
local textColor = RGB(20, 20, 20)
local white = RGB(255, 255, 255)

local brushModes = {"circle", "triangle", "square"} -- only these three
local brushMode = "circle"

local brushSize = 10
local minSize, maxSize = 1, 30

local palette = {RGB(0, 0, 0), RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 255, 0), RGB(255, 165, 0),
                 RGB(128, 0, 128), RGB(255, 192, 203), RGB(0, 255, 255), RGB(128, 128, 128)}
local currentColor = palette[1]

local colorScrollY = 0
local isColorScrolling = false
local colorScrollStartY, colorScrollStartOffset = 0, 0

local lastX, lastY = nil, nil
local isDrawing = false

-- UI state caching (to avoid unnecessary redraws)
local uiState = {
    brushMode = nil,
    brushSize = nil,
    currentColor = nil,
    colorScrollY = nil,
    canvasCleared = false
}

-- Draw sidebar (mode button, clear button, slider)
local function drawSidebar()
    WIN_fillRect(win, bigScreen, 0, 0, sidebarW, canvasH, bgColor)

    WIN_fillRect(win, bigScreen, 2, 2, sidebarW - 4, 16, RGB(200, 200, 200))
    WIN_writeText(win, bigScreen, 4, 4, brushMode, 1, textColor)

    WIN_fillRect(win, bigScreen, 2, 20, sidebarW - 4, 16, RGB(220, 80, 80))
    WIN_writeText(win, bigScreen, 4, 22, "clear", 1, white)

    local sliderX = math.floor(sidebarW / 2)
    local sliderY = 40
    local sliderH = canvasH - sliderY - 4
    WIN_fillRect(win, bigScreen, sliderX - 2, sliderY, 4, sliderH, RGB(180, 180, 180))

    local pct = (brushSize - minSize) / (maxSize - minSize)
    local cursorY = sliderY + math.floor(pct * (sliderH - 1))
    local r = math.floor(brushSize / 2)

    -- draw simple square cursor (cheap)
    WIN_fillRect(win, bigScreen, sliderX - r, cursorY - r, r * 2 + 1, r * 2 + 1, currentColor)
end

local function drawCanvas()
    WIN_fillRect(win, bigScreen, canvasX, 0, canvasW, canvasH, white)
    -- keep a 1px white border to avoid artifacts
    WIN_fillRect(win, bigScreen, canvasX - 1, -1, canvasW + 2, canvasH + 2, white)
end

local function drawColorBar()
    WIN_fillRect(win, leftScreen, 0, 0, colorBarW, canvasH, RGB(200, 200, 200))
    local fieldH = 30
    for i, c in ipairs(palette) do
        local y = (i - 1) * fieldH - colorScrollY
        if y + fieldH >= 0 and y < canvasH then
            WIN_fillRect(win, leftScreen, 0, y, colorBarW, fieldH, c)
        end
    end
end

local function clearAll()
    WIN_fillBg(win, bigScreen, white)
    WIN_fillBg(win, leftScreen, white)
    uiState.canvasCleared = true
end

-- stamping using fast native draws:
local function stampAt(x, y, mode, col)
    -- clip cheap check (keep stamps roughly in canvas area)
    if x < canvasX - maxSize or x > canvasX + canvasW + maxSize or y < -maxSize or y > canvasH + maxSize then
        return
    end

    if mode == "square" then
        local half = math.floor(brushSize / 2)
        WIN_fillRect(win, bigScreen, x - half, y - half, brushSize, brushSize, col)
        return
    end

    if mode == "circle" then
        -- WIN_fillCircle(win, screenId, x, y, r, color) -- r in pixels
        local r = math.floor(brushSize / 2)
        WIN_fillCircle(win, bigScreen, x, y, r, col)
        return
    end

    if mode == "triangle" then
        -- centered isosceles triangle: apex up, base below
        local halfh = math.floor(brushSize / 2)
        local halfw = math.floor(brushSize / 2)
        local x0, y0 = x, y - halfh -- apex
        local x1, y1 = x - halfw, y + halfh -- base left
        local x2, y2 = x + halfw, y + halfh -- base right
        WIN_fillTriangle(win, bigScreen, x0, y0, x1, y1, x2, y2, col)
        return
    end
end

-- stamp along line between two points at intervals to avoid gaps while minimizing stamps
local function stampLine(x1, y1, x2, y2, mode, col)
    if x1 == x2 and y1 == y2 then
        stampAt(x1, y1, mode, col)
        return
    end
    local dx = x2 - x1
    local dy = y2 - y1
    local dist = math.sqrt(dx * dx + dy * dy)
    local step = math.max(1, math.floor(brushSize * 0.5)) -- fewer stamps for larger brushes
    local steps = math.ceil(dist / step)
    for i = 0, steps do
        local t = i / math.max(1, steps)
        local sx = math.floor(x1 + dx * t + 0.5)
        local sy = math.floor(y1 + dy * t + 0.5)
        stampAt(sx, sy, mode, col)
    end
end

-- initial draw
drawSidebar()
drawCanvas()
drawColorBar()

-- helper: redraw only on changes
local function maybeRedrawUI()
    if uiState.brushMode ~= brushMode or uiState.brushSize ~= brushSize or uiState.currentColor ~= currentColor or
        uiState.canvasCleared then
        drawSidebar()
        uiState.brushMode = brushMode
        uiState.brushSize = brushSize
        uiState.currentColor = currentColor
        uiState.canvasCleared = false
    end
    if uiState.colorScrollY ~= colorScrollY then
        drawColorBar()
        uiState.colorScrollY = colorScrollY
    end
end

-- main loop
while not WIN_closed(win) do
    local render = WIN_isRendering()
    if not render then
        delay(100)
    end

    maybeRedrawUI()

    -- NOTE: WIN_getLastEvent now returns 6 values: has, state, pos.x, pos.y, move.x, move.y
    local h, s, px, py, mx, my = WIN_getLastEvent(win, bigScreen)
    if h then
        if s == 0 then
            -- click in sidebar?
            if px >= 2 and px <= sidebarW - 2 and py >= 2 and py <= 18 then
                -- cycle brush modes
                for i, m in ipairs(brushModes) do
                    if m == brushMode then
                        brushMode = brushModes[(i % #brushModes) + 1]
                        break
                    end
                end
            elseif px >= 2 and px <= sidebarW - 2 and py >= 20 and py <= 36 then
                -- clear canvas
                drawCanvas()
            else
                local sliderX, sliderY, sliderH = math.floor(sidebarW / 2), 40, canvasH - 40 - 4
                if px >= sliderX - 5 and px <= sliderX + 5 and py >= sliderY and py <= sliderY + sliderH then
                    local rel = py - sliderY;
                    local pct = rel / sliderH
                    brushSize = math.floor(minSize + pct * (maxSize - minSize) + 0.5)
                    brushSize = math.max(minSize, math.min(maxSize, brushSize))
                end
                if px >= canvasX then
                    isDrawing = true
                    lastX, lastY = px, py
                    local col = currentColor
                    stampAt(px, py, brushMode, col)
                end
            end
        elseif s == 1 and isDrawing then
            local col = currentColor
            if lastX and lastY then
                stampLine(lastX, lastY, px, py, brushMode, col)
            else
                stampAt(px, py, brushMode, col)
            end
            lastX, lastY = px, py
        elseif s == 2 then
            isDrawing = false
            lastX, lastY = nil, nil
        end
    end

    local h2, s2, px2, py2, mx2, my2 = WIN_getLastEvent(win, leftScreen)
    if h2 then
        if s2 == 0 then
            isColorScrolling = true
            colorScrollStartY = py2
            colorScrollStartOffset = colorScrollY
            local idx = math.floor((py2 + colorScrollY) / 30) + 1
            if idx >= 1 and idx <= #palette then
                currentColor = palette[idx]
                brushMode = "circle" -- switch to circle as drawing default
            end
        elseif s2 == 1 and isColorScrolling then
            local dy = py2 - colorScrollStartY
            colorScrollY = colorScrollStartOffset - dy
            local maxScroll = math.max(0, #palette * 30 - canvasH)
            colorScrollY = math.max(0, math.min(maxScroll, colorScrollY))
        elseif s2 == 2 then
            isColorScrolling = false
        end
    end

    delay(10)
end

print("APP:EXITED")
