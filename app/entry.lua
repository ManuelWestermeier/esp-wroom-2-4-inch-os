-- PixelPaint 12x12 Optimized
local win = createWindow(20, 20, 240, 240)
WIN_setName(win, "PixelPaint")

local GRID_SIZE = 12

-- Load saved canvas or initialize
local saved = FS_get("pixelpaint")
local canvas = {}
if saved then
    canvas = saved -- should be table { [x][y]=color }
else
    for i = 1, GRID_SIZE do
        canvas[i] = {}
        for j = 1, GRID_SIZE do
            canvas[i][j] = 0xFFFF -- white
        end
    end
end

-- Palette
local colors = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0xFFE0, 0xF81F, 0x07FF, 0x0000}
local currentColor = colors[1]

-- Compute pixel size dynamically based on window
local function getPixelSize()
    local x, y, w, h = WIN_getRect(win)
    local sizeX = math.floor(w / GRID_SIZE)
    local sizeY = math.floor((h - 30) / GRID_SIZE) -- leave space for palette
    return math.max(1, math.min(sizeX, sizeY))
end

-- Render canvas only when needed
local dirty = true
local function drawCanvas()
    if not dirty then
        return
    end
    dirty = false

    local pxSize = getPixelSize()
    for x = 1, GRID_SIZE do
        for y = 1, GRID_SIZE do
            local px = (x - 1) * pxSize
            local py = (y - 1) * pxSize
            WIN_fillRect(win, 1, px, py, pxSize - 1, pxSize - 1, canvas[x][y])
        end
    end
end

local function drawPalette()
    local pxSize = getPixelSize()
    for i, color in ipairs(colors) do
        local px = (i - 1) * pxSize
        local py = GRID_SIZE * pxSize + 5
        WIN_fillRect(win, 1, px, py, pxSize - 1, pxSize - 1, color)
    end
end

local function checkPaletteClick(x, y)
    local pxSize = getPixelSize()
    if y >= GRID_SIZE * pxSize + 5 then
        local index = math.floor(x / pxSize) + 1
        if colors[index] then
            currentColor = colors[index]
        end
        return true
    end
    return false
end

-- Main loop
while not WIN_closed(win) do
    local pressed, state, mx, my = WIN_getLastEvent(win, 1)
    if pressed then
        if not checkPaletteClick(mx, my) then
            local pxSize = getPixelSize()
            local gx = math.floor(mx / pxSize) + 1
            local gy = math.floor(my / pxSize) + 1
            if gx >= 1 and gx <= GRID_SIZE and gy >= 1 and gy <= GRID_SIZE then
                if canvas[gx][gy] ~= currentColor then
                    canvas[gx][gy] = currentColor
                    dirty = true
                    FS_set("pixelpaint", canvas) -- save after each change
                end
            end
        end
    end

    if dirty then
        WIN_fillBg(win, 1, 0x0000) -- background only on changes
        drawCanvas()
        drawPalette()
    end

    delay(16)
end
