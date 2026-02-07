-- pixel_painter_binary.lua
-- Simple binary (black/white) pixel painter for ESP32 windowing system
-- 10x10 canvas, stores painting as a compact "0/1" string in the filesystem.
-- Draws only changed cells and keeps UI mostly white.
local win = createWindow(0, 12, 320, 220)
WIN_setName(win, "Pixel Painter (Binary 10x10)")

-- Config (fixed 10x10 as requested)
local GRID_SIZE = 10
local gridSize = GRID_SIZE
local scale_min, scale_max = 6, 48
local scale = 18
local margin = 8

-- Binary pixels: 0 = white, 1 = black
local pixels = {}
local prevPixels = {}

local function idx(x, y) return (y - 1) * gridSize + x end

local function clamp(v, a, b)
    if v < a then return a end
    if v > b then return b end
    return v
end

local function clearPixels(fill)
    fill = fill or 0 -- default white
    for i = 1, gridSize * gridSize do
        pixels[i] = fill
        prevPixels[i] = nil
    end
end

clearPixels(0)

-- Pan/scroll state
local offsetX, offsetY = 0, 0
local maxOffsetX, maxOffsetY = 0, 0
local handMode = false

-- UI / interaction state
local dragPainting = false
local eraserMode = false
local dragStartX, dragStartY, dragStartOffsetX, dragStartOffsetY

-- Layout cache
local layout = {}
local staticDirty = true
local canvasDirtyAll = true

local function computeLayout()
    local x, y, w, h = WIN_getRect(win)
    layout.winW = w
    layout.winH = h

    layout.toolbar = {x = margin, y = margin, w = w - margin * 2, h = 40}

    layout.canvas = {}
    layout.canvas.x = margin
    layout.canvas.y = layout.toolbar.y + layout.toolbar.h + 6
    layout.canvas.w = w - margin * 2
    layout.canvas.h = h - margin - layout.canvas.y

    -- choose cell size to try to match requested scale but fit into canvas
    local cs = math.floor(layout.canvas.w / gridSize)
    if cs > math.floor(layout.canvas.h / gridSize) then
        cs = math.floor(layout.canvas.h / gridSize)
    end
    cs = clamp(cs, scale_min, scale_max)
    scale = cs
    layout.cellSize = cs

    local contentW = gridSize * layout.cellSize
    local contentH = gridSize * layout.cellSize
    maxOffsetX = math.max(0, contentW - layout.canvas.w)
    maxOffsetY = math.max(0, contentH - layout.canvas.h)
    offsetX = clamp(offsetX, 0, maxOffsetX)
    offsetY = clamp(offsetY, 0, maxOffsetY)

    layout.visibleGX1 = math.floor(offsetX / layout.cellSize) + 1
    layout.visibleGY1 = math.floor(offsetY / layout.cellSize) + 1
    layout.visibleGX2 = math.min(gridSize, math.ceil(
                                     (offsetX + layout.canvas.w) /
                                         layout.cellSize))
    layout.visibleGY2 = math.min(gridSize, math.ceil(
                                     (offsetY + layout.canvas.h) /
                                         layout.cellSize))
end

-- Draw a single cell (only when it changed)
local function drawCell(gx, gy)
    if gx < 1 or gx > gridSize or gy < 1 or gy > gridSize then return end
    local c = layout.canvas
    local cell = layout.cellSize
    local contentX = (gx - 1) * cell
    local contentY = (gy - 1) * cell
    local sx = c.x + contentX - offsetX
    local sy = c.y + contentY - offsetY
    local pw = math.ceil(cell)
    local ph = math.ceil(cell)

    -- clip within canvas
    if sx + pw < c.x or sy + ph < c.y or sx > c.x + c.w or sy > c.y + c.h then
        return
    end

    local id = idx(gx, gy)
    local v = pixels[id] == 1 and 0x0000 or 0xFFFF
    WIN_fillRect(win, 1, sx, sy, pw, ph, v)
    if cell >= 6 then WIN_drawRect(win, 1, sx, sy, pw, ph, 0x0000) end
end

-- Draw whole visible canvas (used rarely)
local function drawCanvasAll()
    local c = layout.canvas
    WIN_fillRect(win, 1, c.x, c.y, c.w, c.h, 0xFFFF)
    WIN_drawRect(win, 1, c.x, c.y, c.w, c.h, 0x0000)
    for gy = layout.visibleGY1, layout.visibleGY2 do
        for gx = layout.visibleGX1, layout.visibleGX2 do
            drawCell(gx, gy)
            prevPixels[idx(gx, gy)] = pixels[idx(gx, gy)]
        end
    end
end

-- Static UI: toolbar (mostly white)
local function drawStatic()
    WIN_fillBg(win, 1, 0xFFFF)
    local tb = layout.toolbar
    WIN_fillRect(win, 1, tb.x, tb.y, tb.w, tb.h, 0xFFFF)
    WIN_drawRect(win, 1, tb.x, tb.y, tb.w, tb.h, 0x0000)
    WIN_writeText(win, 1, tb.x + 6, tb.y + 8, string.format(
                      "Pixel Painter — %dx%d (binary)", gridSize, gridSize),
                  2, 0x0000)

    -- Buttons: New, Save, Load, Hand, Eraser
    local bx = tb.x + tb.w - 10
    local bw, bh = 48, 24
    local spacing = 6

    -- Save
    bx = bx - bw
    WIN_fillRect(win, 1, bx, tb.y + 6, bw, bh, 0xFFFF)
    WIN_drawRect(win, 1, bx, tb.y + 6, bw, bh, 0x0000)
    WIN_writeText(win, 1, bx + 6, tb.y + 10, "Save", 1, 0x0000)
    layout.btnSave = {x = bx, y = tb.y + 6, w = bw, h = bh}
    bx = bx - spacing

    -- Load
    bx = bx - bw
    WIN_fillRect(win, 1, bx, tb.y + 6, bw, bh, 0xFFFF)
    WIN_drawRect(win, 1, bx, tb.y + 6, bw, bh, 0x0000)
    WIN_writeText(win, 1, bx + 6, tb.y + 10, "Load", 1, 0x0000)
    layout.btnLoad = {x = bx, y = tb.y + 6, w = bw, h = bh}
    bx = bx - spacing

    -- New
    bx = bx - bw
    WIN_fillRect(win, 1, bx, tb.y + 6, bw, bh, 0xFFFF)
    WIN_drawRect(win, 1, bx, tb.y + 6, bw, bh, 0x0000)
    WIN_writeText(win, 1, bx + 6, tb.y + 10, "New", 1, 0x0000)
    layout.btnNew = {x = bx, y = tb.y + 6, w = bw, h = bh}
    bx = bx - spacing

    -- Hand toggle
    local handW = 60
    local handX = tb.x + 6
    WIN_fillRect(win, 1, handX, tb.y + 6, handW, bh, 0xFFFF)
    WIN_drawRect(win, 1, handX, tb.y + 6, handW, bh, 0x0000)
    WIN_writeText(win, 1, handX + 6, tb.y + 10,
                  handMode and "Hand:On" or "Hand:Off", 1, 0x0000)
    layout.btnHand = {x = handX, y = tb.y + 6, w = handW, h = bh}

    -- Eraser toggle
    local erX = handX + handW + 8
    local erW = 72
    WIN_fillRect(win, 1, erX, tb.y + 6, erW, bh, 0xFFFF)
    WIN_drawRect(win, 1, erX, tb.y + 6, erW, bh, 0x0000)
    WIN_writeText(win, 1, erX + 6, tb.y + 10,
                  eraserMode and "Eraser:On" or "Eraser:Off", 1, 0x0000)
    layout.btnEraser = {x = erX, y = tb.y + 6, w = erW, h = bh}

    -- Current color preview (black/white)
    local cx = erX + erW + 10
    WIN_fillRect(win, 1, cx, tb.y + 8, 20, 20, eraserMode and 0xFFFF or 0x0000)
    WIN_drawRect(win, 1, cx, tb.y + 8, 20, 20, 0x0000)
    WIN_writeText(win, 1, cx + 28, tb.y + 12, "Color", 1, 0x0000)

    staticDirty = false
    canvasDirtyAll = true
end

-- Serialization (binary string of '0' and '1')
local function serializePixels()
    local parts = {}
    for i = 1, gridSize * gridSize do
        parts[#parts + 1] = pixels[i] == 1 and "1" or "0"
    end
    return table.concat(parts, "")
end

local function deserializePixels(s)
    if not s or #s < 1 then return false end
    local i = 1
    for c = 1, #s do
        if i > gridSize * gridSize then break end
        local ch = s:sub(c, c)
        if ch == "1" then
            pixels[i] = 1
        else
            pixels[i] = 0
        end
        i = i + 1
    end
    -- fill remainder with white
    for j = i, gridSize * gridSize do pixels[j] = 0 end
    canvasDirtyAll = true
    return true
end

-- Button hit test
local function pointInRect(px, py, r)
    return px >= r.x and px <= r.x + r.w and py >= r.y and py <= r.y + r.h
end

local function onSave()
    local ok, name = WIN_readText(win, "Save painting as:", "mypainting")
    if ok and name and #name > 0 then
        local data = serializePixels()
        FS_set("paint_" .. name, data)
    end
end

local function onLoad()
    local ok, name = WIN_readText(win, "Load painting (name):", "mypainting")
    if ok and name and #name > 0 then
        local s = FS_get("paint_" .. name)
        if s then deserializePixels(s) end
    end
end

local function onNew()
    clearPixels(0)
    canvasDirtyAll = true
end

-- Handle touch on canvas: paints or pans
local function handleCanvasTouch(tx, ty)
    local c = layout.canvas
    local relX = tx - c.x + offsetX
    local relY = ty - c.y + offsetY
    if relX < 0 or relY < 0 then return false end
    local gx = math.floor(relX / layout.cellSize) + 1
    local gy = math.floor(relY / layout.cellSize) + 1
    if gx < 1 or gx > gridSize or gy < 1 or gy > gridSize then return false end
    local id = idx(gx, gy)
    local newVal = eraserMode and 0 or 1
    if pixels[id] ~= newVal then
        pixels[id] = newVal
        drawCell(gx, gy)
        prevPixels[id] = pixels[id]
    end
    return true
end

-- Redraw only changed visible cells
local function redrawChangedCells()
    for gy = layout.visibleGY1, layout.visibleGY2 do
        for gx = layout.visibleGX1, layout.visibleGX2 do
            local id = idx(gx, gy)
            if prevPixels[id] ~= pixels[id] then
                drawCell(gx, gy)
                prevPixels[id] = pixels[id]
            end
        end
    end
end

-- Main loop
computeLayout()
staticDirty = true

while not WIN_closed(win) do
    local pressed, state, x, y, mx, my, wc, nr = WIN_getLastEvent(win, 1)

    if nr or state > 0 then
        WIN_finishFrame(win)

        local oldW = layout.winW
        computeLayout()
        if staticDirty or oldW ~= layout.winW then drawStatic() end

        if canvasDirtyAll then
            drawCanvasAll()
            canvasDirtyAll = false
        end

        if state > 0 then
            -- press began
            if pointInRect(x, y, layout.btnSave) then
                onSave()
            elseif pointInRect(x, y, layout.btnLoad) then
                onLoad()
            elseif pointInRect(x, y, layout.btnNew) then
                onNew()
            elseif pointInRect(x, y, layout.btnHand) then
                handMode = not handMode
                staticDirty = true
            elseif pointInRect(x, y, layout.btnEraser) then
                eraserMode = not eraserMode
                staticDirty = true
            else
                if x >= layout.canvas.x and x <= layout.canvas.x +
                    layout.canvas.w and y >= layout.canvas.y and y <=
                    layout.canvas.y + layout.canvas.h then
                    if handMode then
                        dragPainting = false
                        dragStartX = x
                        dragStartY = y
                        dragStartOffsetX = offsetX
                        dragStartOffsetY = offsetY
                    else
                        handleCanvasTouch(x, y)
                        dragPainting = true
                    end
                end
            end
        else
            -- touch end
            dragPainting = false
            dragStartX = nil
            dragStartY = nil
        end

        -- dragging: paint or pan
        if state > 0 and dragPainting then
            handleCanvasTouch(mx, my)
        elseif state > 0 and dragStartX and handMode then
            local dx = dragStartX - mx
            local dy = dragStartY - my
            offsetX = clamp(dragStartOffsetX + dx, 0, maxOffsetX)
            offsetY = clamp(dragStartOffsetY + dy, 0, maxOffsetY)
            drawCanvasAll()
        end

        -- repaint changed cells only
        redrawChangedCells()
    end

    delay(16)
end

WIN_close(win)
