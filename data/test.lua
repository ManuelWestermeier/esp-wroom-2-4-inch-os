print("APP:STARTED")

local win = createWindow(20, 20, 240, 160)
WIN_setName(win, "Paint")

local bigScreen = 1
local leftScreen = 2

local screenH = 160

local bgColor = RGB(230, 230, 230)
local textColor = RGB(20, 20, 20)

local sidebarW = 40
local canvasX, canvasY = sidebarW, 0
local canvasW, canvasH = 200, screenH

local palette = {
    RGB(0,0,0), RGB(255,0,0), RGB(0,255,0), RGB(0,0,255),
    RGB(255,255,0), RGB(255,165,0), RGB(128,0,128), RGB(255,192,203),
    RGB(0,255,255), RGB(128,128,128), RGB(255,255,255), RGB(50,50,50)
}

local currentColor = palette[1]
local brushSize = 3
local brushMode = "brush" -- brush, square, line, eraser

-- For line mode
local lastX, lastY = nil, nil

local colorScrollY = 0
local isColorScrolling = false
local colorScrollStartY = 0
local colorScrollStartOffset = 0

local brushModes = { "brush", "square", "line", "eraser" }

local function drawSidebar()
    WIN_writeRect(win, bigScreen, 0, 0, sidebarW, canvasH, bgColor)

    -- Mode Button
    WIN_writeRect(win, bigScreen, 2, 2, 36, 16, RGB(180,180,180))
    WIN_writeText(win, bigScreen, 4, 4, brushMode, 1, textColor)

    -- Size Buttons
    for i = 1, 5 do
        local y = 25 + (i - 1) * 20
        local col = (brushSize == i) and RGB(100,200,100) or RGB(200,200,200)
        WIN_writeRect(win, bigScreen, 5, y, 30, 16, col)
        WIN_writeText(win, bigScreen, 15, y + 2, tostring(i), 1, textColor)
    end
end

local function drawCanvas()
    WIN_writeRect(win, bigScreen, canvasX, canvasY, canvasW, canvasH, RGB(255,255,255))
    WIN_writeRect(win, bigScreen, canvasX - 1, canvasY - 1, canvasW + 2, canvasH + 2, textColor)
end

local function drawColorBar()
    WIN_writeRect(win, leftScreen, 0, 0, 12, screenH, RGB(200,200,200))

    local fieldHeight = 20
    for i, c in ipairs(palette) do
        local y = (i - 1) * fieldHeight - colorScrollY
        if y + fieldHeight >= 0 and y < screenH then
            WIN_writeRect(win, leftScreen, 0, y, 12, fieldHeight, c)
        end
    end
end

local function paintAt(x, y)
    local col = (brushMode == "eraser") and RGB(255,255,255) or currentColor
    local size = brushSize
    local half = math.floor(size / 2)

    if brushMode == "brush" or brushMode == "eraser" then
        for dx = -half, half do
            for dy = -half, half do
                if dx*dx + dy*dy <= half*half then
                    WIN_writeRect(win, bigScreen, x + dx, y + dy, 1, 1, col)
                end
            end
        end
    elseif brushMode == "square" then
        WIN_writeRect(win, bigScreen, x - half, y - half, size, size, col)
    elseif brushMode == "line" and lastX then
        local dx = x - lastX
        local dy = y - lastY
        local steps = math.max(math.abs(dx), math.abs(dy))
        for i = 0, steps do
            local px = math.floor(lastX + dx * (i / steps))
            local py = math.floor(lastY + dy * (i / steps))
            WIN_writeRect(win, bigScreen, px, py, 1, 1, col)
        end
    end

    lastX, lastY = x, y
end

drawSidebar()
drawCanvas()
drawColorBar()

local isDrawing = false

while not WIN_closed(win) do
    local redrawSidebar = false
    local redrawColorBar = false

    local happened, state, posX, posY = WIN_getLastEvent(win, bigScreen)
    local happened2, state2, posX2, posY2 = WIN_getLastEvent(win, leftScreen)

    -- Main canvas events
    if happened then
        if state == 0 and posX >= canvasX then
            isDrawing = true
            lastX, lastY = posX, posY
            paintAt(posX, posY)
        elseif state == 1 and isDrawing then
            paintAt(posX, posY)
        elseif state == 2 then
            isDrawing = false
            lastX, lastY = nil, nil
        end

        -- Sidebar clicks
        if state == 0 and posX < sidebarW then
            if posY >= 2 and posY <= 18 then
                -- Switch brush mode
                for i, m in ipairs(brushModes) do
                    if brushModes[i] == brushMode then
                        brushMode = brushModes[(i % #brushModes) + 1]
                        break
                    end
                end
                redrawSidebar = true
            end

            -- Size buttons
            for i = 1, 5 do
                local y = 25 + (i - 1) * 20
                if posY >= y and posY <= y + 16 then
                    brushSize = i
                    redrawSidebar = true
                    break
                end
            end
        end
    end

    -- Color picker scroll and selection
    if happened2 then
        if state2 == 0 then
            isColorScrolling = true
            colorScrollStartY = posY2
            colorScrollStartOffset = colorScrollY
        elseif state2 == 1 and isColorScrolling then
            local dy = posY2 - colorScrollStartY
            colorScrollY = colorScrollStartOffset - dy
            local maxScroll = math.max(0, (#palette * 20) - screenH)
            colorScrollY = math.max(0, math.min(colorScrollY, maxScroll))
            redrawColorBar = true
        elseif state2 == 2 then
            isColorScrolling = false
        end

        -- Click to select color
        if state2 == 0 then
            local idx = math.floor((posY2 + colorScrollY) / 20) + 1
            if idx >= 1 and idx <= #palette then
                currentColor = palette[idx]
                brushMode = (brushMode == "eraser") and "brush" or brushMode
                redrawSidebar = true
            end
        end
    end

    if redrawSidebar then drawSidebar() end
    if redrawColorBar then drawColorBar() end

    delay(10)
end

print("APP:EXITED")
