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
    RGB(0,255,255), RGB(128,128,128), RGB(255,255,255), RGB(50,50,50),
    RGB(100,50,0), RGB(0,100,50), RGB(50,0,100), RGB(100,0,100)
}

local currentColor = palette[1]
local brushSize = 3
local eraserSize = 5
local eraserOn = false

-- Color bar scroll state (Y offset only)
local colorScrollY = 0
local isColorScrolling = false
local colorScrollStartY = 0
local colorScrollStartOffset = 0

local function drawSidebar()
    WIN_writeRect(win, bigScreen, 0, 0, sidebarW, canvasH, bgColor)

    WIN_writeText(win, bigScreen, 5, 10, "Brush", 1, textColor)
    WIN_writeRect(win, bigScreen, 10, 30, 20, 6, RGB(200, 200, 200))
    local pos = 10 + (brushSize - 1) * 2
    WIN_writeRect(win, bigScreen, pos, 27, 4, 12, textColor)

    WIN_writeText(win, bigScreen, 5, 60, "Eraser", 1, textColor)
    WIN_writeRect(win, bigScreen, 10, 80, 20, 6, RGB(200, 200, 200))
    local posE = 10 + (eraserSize - 1) * 2
    WIN_writeRect(win, bigScreen, posE, 77, 4, 12, textColor)

    WIN_writeText(win, bigScreen, 5, 110, eraserOn and "Eraser" or "Brush", 1, textColor)
end

local function drawCanvas()
    WIN_writeRect(win, bigScreen, canvasX, canvasY, canvasW, canvasH, RGB(255,255,255))
    WIN_writeRect(win, bigScreen, canvasX - 1, canvasY - 1, canvasW + 2, canvasH + 2, textColor)
end

local function drawColorBar()
    WIN_writeRect(win, leftScreen, 0, 0, 12, screenH, RGB(200,200,200))

    local fieldHeight = math.floor((screenH / #palette) * 2)  -- doubled height
    local totalHeight = fieldHeight * #palette

    for i, c in ipairs(palette) do
        local y = (i - 1) * fieldHeight - colorScrollY
        if y + fieldHeight >= 0 and y < screenH then
            WIN_writeRect(win, leftScreen, 0, y, 12, fieldHeight, c)
        end
    end
end

local function paint(x, y)
    if x < canvasX or x >= canvasX + canvasW or y < canvasY or y >= canvasY + canvasH then return end

    local col = eraserOn and RGB(255,255,255) or currentColor
    local size = eraserOn and eraserSize or brushSize
    local half = math.floor(size / 2)

    for dx = -half, half do
        for dy = -half, half do
            if dx*dx + dy*dy <= half*half then
                WIN_writeRect(win, bigScreen, x + dx, y + dy, 1, 1, col)
            end
        end
    end
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

    -- Painting
    if happened then
        if state == 0 and posX >= canvasX then
            isDrawing = true
            paint(posX, posY)
        elseif state == 1 and isDrawing then
            paint(posX, posY)
        elseif state == 2 then
            isDrawing = false
        end

        -- Brush / Eraser UI clicks
        if state == 0 and posX < sidebarW then
            if posY >= 25 and posY <= 45 then
                brushSize = math.max(1, math.min(math.floor((posX - 10 + 2) / 2), 10))
                eraserOn = false
                redrawSidebar = true
            elseif posY >= 75 and posY <= 95 then
                eraserSize = math.max(1, math.min(math.floor((posX - 10 + 2) / 2), 10))
                eraserOn = true
                redrawSidebar = true
            end
        end
    end

    -- Color bar: scrolling and selection
    if happened2 then
        if state2 == 0 then
            isColorScrolling = true
            colorScrollStartY = posY2
            colorScrollStartOffset = colorScrollY
        elseif state2 == 1 and isColorScrolling then
            local dy = posY2 - colorScrollStartY
            colorScrollY = colorScrollStartOffset - dy
            local maxScroll = math.max(0, (#palette * 2 * screenH / #palette) - screenH)
            colorScrollY = math.max(0, math.min(colorScrollY, maxScroll))
            redrawColorBar = true
        elseif state2 == 2 then
            isColorScrolling = false
        end

        -- Color selection
        if state2 == 0 then
            local fieldHeight = math.floor((screenH / #palette) * 2)
            local idx = math.floor((posY2 + colorScrollY) / fieldHeight) + 1
            if idx >= 1 and idx <= #palette then
                currentColor = palette[idx]
                eraserOn = false
                redrawSidebar = true
            end
        end
    end

    if redrawSidebar then drawSidebar() end
    if redrawColorBar then drawColorBar() end

    delay(10)
end

print("APP:EXITED")
