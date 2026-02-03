-- pixel_painter.lua
-- Simple pixel painting app for ESP32 windowing system
-- Features: up to 20x20 pixel canvas, zoom, color palette, save/load via FS, responsive layout
local win = createWindow(20, 20, 300, 200)
WIN_setName(win, "Pixel Painter")

-- Config
local GRID_MAX = 20
local defaultGrid = GRID_MAX
local scale = 12 -- size of one pixel in screen pixels (zoom)
local scale_min, scale_max = 6, 32
local margin = 8
local palette = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF}
local curColorIdx = 3 -- default red (index into palette)
local showGrid = true
local gridSize = defaultGrid

-- Pixel storage (1D table, row-major, Lua indices start at 1)
local pixels = {}
local function clearPixels(fillColor)
    fillColor = fillColor or 0xFFFF
    for i = 1, gridSize * gridSize do
        pixels[i] = fillColor
    end
end

-- Helpers
local function idx(x, y)
    return (y - 1) * gridSize + x
end
local function clamp(v, a, b)
    if v < a then
        return a
    end
    if v > b then
        return b
    end
    return v
end

clearPixels(0xFFFF)

-- Serialization for save/load
local function serializePixels()
    -- join numbers with comma
    local parts = {}
    for i = 1, gridSize * gridSize do
        parts[#parts + 1] = tostring(pixels[i])
    end
    return table.concat(parts, ",")
end

local function deserializePixels(s)
    if not s then
        return false
    end
    local i = 1
    for num in string.gmatch(s, "([^,]+)") do
        local v = tonumber(num)
        if not v then
            return false
        end
        pixels[i] = v
        i = i + 1
        if i > gridSize * gridSize then
            break
        end
    end
    -- if not enough values, fill remainder
    for j = i, gridSize * gridSize do
        pixels[j] = 0xFFFF
    end
    return true
end

-- UI state
local dragPainting = false
local eraserMode = false
local lastTouchCell = nil

-- Layout computed each frame to be responsive
local layout = {}
local function computeLayout()
    local x, y, w, h = WIN_getRect(win)
    -- top-left area reserved for toolbar
    local toolbarH = 40 + margin
    layout.winX = x
    layout.winY = y
    layout.winW = w
    layout.winH = h
    layout.toolbar = {
        x = 0 + margin,
        y = 0 + margin,
        w = w - margin * 2,
        h = 36
    }

    -- compute canvas area: square that fits gridSize*scale
    local maxCanvasW = w - margin * 2
    local maxCanvasH = h - toolbarH - margin * 2
    local maxSize = math.min(maxCanvasW, maxCanvasH)
    layout.canvasSizePixels = clamp(gridSize * scale, 20, maxSize)
    -- center canvas horizontally
    layout.canvas = {}
    layout.canvas.w = layout.canvasSizePixels
    layout.canvas.h = layout.canvasSizePixels
    layout.canvas.x = math.floor((w - layout.canvas.w) / 2)
    layout.canvas.y = toolbarH + math.floor((maxCanvasH - layout.canvas.h) / 2)
    layout.cellSize = layout.canvas.w / gridSize

    -- palette area under toolbar
    layout.palette = {
        x = margin,
        y = layout.toolbar.y + layout.toolbar.h + 4,
        w = w - margin * 2,
        h = 28
    }
end

-- Draw helpers
local function drawButton(x, y, w, h, label)
    WIN_fillRect(win, 1, x, y, w, h, 0xFFFF)
    WIN_drawRect(win, 1, x, y, w, h, 0x0000)
    WIN_writeText(win, 1, x + 4, y + 4, label, 1, 0x0000)
end

local function drawUI()
    WIN_fillBg(win, 1, 0xEEEE) -- light gray background (approx)

    -- Toolbar
    local tb = layout.toolbar
    WIN_fillRect(win, 1, tb.x, tb.y, tb.w, tb.h, 0xFFFF)
    WIN_drawRect(win, 1, tb.x, tb.y, tb.w, tb.h, 0x0000)
    WIN_writeText(win, 1, tb.x + 4, tb.y + 6, "Pixel Painter - " .. gridSize .. "x" .. gridSize, 2, 0x0000)

    -- Buttons in toolbar
    local bx = tb.x + tb.w - 10
    local bw, bh = 44, 24
    local spacing = 6

    -- Save
    bx = bx - bw
    drawButton(bx, tb.y + 6, bw, bh, "Save")
    layout.btnSave = {
        x = bx,
        y = tb.y + 6,
        w = bw,
        h = bh
    }
    bx = bx - spacing

    -- Load
    bx = bx - bw
    drawButton(bx, tb.y + 6, bw, bh, "Load")
    layout.btnLoad = {
        x = bx,
        y = tb.y + 6,
        w = bw,
        h = bh
    }
    bx = bx - spacing

    -- New
    bx = bx - bw
    drawButton(bx, tb.y + 6, bw, bh, "New")
    layout.btnNew = {
        x = bx,
        y = tb.y + 6,
        w = bw,
        h = bh
    }
    bx = bx - spacing

    -- Zoom Out
    local zbw = 34
    bx = bx - zbw
    drawButton(bx, tb.y + 6, zbw, bh, "-")
    layout.btnZoomOut = {
        x = bx,
        y = tb.y + 6,
        w = zbw,
        h = bh
    }
    bx = bx - spacing

    -- Zoom In
    bx = bx - zbw
    drawButton(bx, tb.y + 6, zbw, bh, "+")
    layout.btnZoomIn = {
        x = bx,
        y = tb.y + 6,
        w = zbw,
        h = bh
    }

    -- Toggle Grid
    local tgW = 64
    bx = tb.x + 6
    drawButton(bx, tb.y + 6, tgW, bh, showGrid and "Grid:On" or "Grid:Off")
    layout.btnGrid = {
        x = bx,
        y = tb.y + 6,
        w = tgW,
        h = bh
    }
    bx = bx + tgW + 8

    -- Erase toggle
    drawButton(bx, tb.y + 6, 60, bh, eraserMode and "Eraser:On" or "Eraser:Off")
    layout.btnEraser = {
        x = bx,
        y = tb.y + 6,
        w = 60,
        h = bh
    }

    -- Canvas
    local c = layout.canvas
    WIN_fillRect(win, 1, c.x, c.y, c.w, c.h, 0xFFFF)
    WIN_drawRect(win, 1, c.x, c.y, c.w, c.h, 0x0000)

    -- Draw pixels
    local cell = layout.cellSize
    for gy = 1, gridSize do
        for gx = 1, gridSize do
            local v = pixels[idx(gx, gy)] or 0xFFFF
            local px = math.floor(c.x + (gx - 1) * cell)
            local py = math.floor(c.y + (gy - 1) * cell)
            local pw = math.ceil(cell)
            local ph = math.ceil(cell)
            WIN_fillRect(win, 1, px, py, pw, ph, v)
            if showGrid and cell >= 6 then
                WIN_drawRect(win, 1, px, py, pw, ph, 0x0000)
            end
        end
    end

    -- Palette
    local pal = layout.palette
    local sw = 26
    local px = pal.x
    for i = 1, #palette do
        local col = palette[i]
        WIN_fillRect(win, 1, px, pal.y, sw, sw, col)
        WIN_drawRect(win, 1, px, pal.y, sw, sw, 0x0000)
        if i == curColorIdx then
            WIN_drawRect(win, 1, px - 2, pal.y - 2, sw + 4, sw + 4, 0x07E0)
        end
        layout["pal_" .. i] = {
            x = px,
            y = pal.y,
            w = sw,
            h = sw
        }
        px = px + sw + 6
    end

    -- Quick instructions
    WIN_writeText(win, 1, pal.x + 240, pal.y + 6, "Touch canvas to paint. Save/Load via prompts.", 1, 0x0000)
end

-- Input handling
local function pointInRect(px, py, rect)
    return px >= rect.x and px <= rect.x + rect.w and py >= rect.y and py <= rect.y + rect.h
end

local function handleCanvasTouch(tx, ty)
    local c = layout.canvas
    if tx < c.x or tx > c.x + c.w or ty < c.y or ty > c.y + c.h then
        return false
    end
    local cell = layout.cellSize
    local gx = math.floor((tx - c.x) / cell) + 1
    local gy = math.floor((ty - c.y) / cell) + 1
    if gx < 1 or gx > gridSize or gy < 1 or gy > gridSize then
        return false
    end
    local key = idx(gx, gy)
    if eraserMode then
        pixels[key] = 0xFFFF
    else
        pixels[key] = palette[curColorIdx]
    end
    lastTouchCell = key
    return true
end

-- Button actions
local function onSave()
    local ok, name = WIN_readText(win, "Save painting as:", "mypainting")
    if ok and name and #name > 0 then
        local key = "paint_" .. name
        FS_set(key, serializePixels())
    end
end

local function onLoad()
    local ok, name = WIN_readText(win, "Load painting (name):", "mypainting")
    if ok and name and #name > 0 then
        local key = "paint_" .. name
        local s = FS_get(key)
        if s then
            deserializePixels(s)
        else
            -- not found -> do nothing or notify
        end
    end
end

local function onNew()
    clearPixels(0xFFFF)
end

-- Main loop
while not WIN_closed(win) do
    local pressed, state, x, y, mx, my, wc, nr = WIN_getLastEvent(win, 1)

    if nr or state > 0 then
        WIN_finishFrame(win)
        computeLayout()
        drawUI()

        if state > 0 then
            -- check toolbar buttons
            if pointInRect(x, y, layout.btnSave) then
                onSave()
            elseif pointInRect(x, y, layout.btnLoad) then
                onLoad()
            elseif pointInRect(x, y, layout.btnNew) then
                onNew()
            elseif pointInRect(x, y, layout.btnZoomIn) then
                scale = clamp(scale + 2, scale_min, scale_max)
            elseif pointInRect(x, y, layout.btnZoomOut) then
                scale = clamp(scale - 2, scale_min, scale_max)
            elseif pointInRect(x, y, layout.btnGrid) then
                showGrid = not showGrid
            elseif pointInRect(x, y, layout.btnEraser) then
                eraserMode = not eraserMode
            else
                -- palette selection
                for i = 1, #palette do
                    if pointInRect(x, y, layout["pal_" .. i]) then
                        curColorIdx = i
                        eraserMode = false
                        break
                    end
                end

                -- canvas painting
                handleCanvasTouch(x, y)
                dragPainting = true
            end
        else
            -- touch end
            dragPainting = false
            lastTouchCell = nil
        end

        -- if dragging and moved, paint
        if dragPainting and state > 0 then
            handleCanvasTouch(mx, my)
        end
    end

    delay(16)
end

WIN_close(win)
