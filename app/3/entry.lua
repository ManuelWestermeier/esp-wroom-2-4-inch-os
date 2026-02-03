-- pixel_painter.lua
-- Improved pixel painting app for ESP32 windowing system
-- Changes requested: start 8x8 (user-adjustable), layout isolation, scrollable (pan) view,
-- mostly white UI, draw only changed pixels, avoid redrawing entire UI on each click.
local win = createWindow(0, 12, 320, 220)
WIN_setName(win, "Pixel Painter")

-- Config
local GRID_MAX = 20
local GRID_MIN = 2
local gridSize = 8 -- start at 8x8 as requested
local scale = 18 -- logical pixel size (will be clamped to fit canvas)
local scale_min, scale_max = 6, 48
local margin = 8
local palette = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF}
local curColorIdx = 3 -- default red
local showGrid = true

-- Pixel storage (row-major)
local pixels = {}
local prevPixels = {}
local function clearPixels(fillColor)
    fillColor = fillColor or 0xFFFF
    for i = 1, gridSize * gridSize do
        pixels[i] = fillColor
        prevPixels[i] = nil
    end
end

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

-- Scroll / Pan state (in pixels)
local offsetX, offsetY = 0, 0
local maxOffsetX, maxOffsetY = 0, 0
local handMode = false -- when true, dragging pans instead of painting

-- UI state
local dragPainting = false
local eraserMode = false
local lastTouchCell = nil
local dragStartX, dragStartY, dragStartOffsetX, dragStartOffsetY

-- Layout and cached flags
local layout = {}
local staticDirty = true -- indicates toolbar/palette/static areas need redraw
local canvasDirtyAll = true -- when true, redraw visible canvas cells

local function ensurePixelsSize(newSize)
    if newSize == gridSize then
        return
    end
    local old = pixels
    gridSize = newSize
    pixels = {}
    prevPixels = {}
    clearPixels(0xFFFF)
end

-- Compute layout and scrolling limits
local function computeLayout()
    local x, y, w, h = WIN_getRect(win)
    layout.winW = w
    layout.winH = h

    -- toolbar at top
    layout.toolbar = {
        x = margin,
        y = margin,
        w = w - margin * 2,
        h = 40
    }

    -- palette under toolbar
    layout.palette = {
        x = margin,
        y = layout.toolbar.y + layout.toolbar.h + 6,
        w = w - margin * 2,
        h = 32
    }

    -- canvas area occupies remaining space without overlapping
    local top = layout.palette.y + layout.palette.h + 6
    local bottom = h - margin
    layout.canvas = {}
    layout.canvas.x = margin
    layout.canvas.y = top
    layout.canvas.w = w - margin * 2
    layout.canvas.h = bottom - top

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

    -- visible cell ranges for optimized redraw
    layout.visibleGX1 = math.floor(offsetX / layout.cellSize) + 1
    layout.visibleGY1 = math.floor(offsetY / layout.cellSize) + 1
    layout.visibleGX2 = math.min(gridSize, math.ceil((offsetX + layout.canvas.w) / layout.cellSize))
    layout.visibleGY2 = math.min(gridSize, math.ceil((offsetY + layout.canvas.h) / layout.cellSize))
end

-- Low-level draw of a single cell (only called when that cell must change)
local function drawCell(gx, gy)
    local c = layout.canvas
    if gx < 1 or gx > gridSize or gy < 1 or gy > gridSize then
        return
    end
    local cell = layout.cellSize
    local contentX = (gx - 1) * cell
    local contentY = (gy - 1) * cell
    local sx = c.x + contentX - offsetX
    local sy = c.y + contentY - offsetY
    local pw = math.ceil(cell)
    local ph = math.ceil(cell)

    -- clip: if outside canvas area, skip
    if sx + pw < c.x or sy + ph < c.y or sx > c.x + c.w or sy > c.y + c.h then
        return
    end

    local v = pixels[idx(gx, gy)] or 0xFFFF
    -- only paint colored fields (non-white) and white when erased
    WIN_fillRect(win, 1, sx, sy, pw, ph, v)
    if showGrid and cell >= 6 then
        WIN_drawRect(win, 1, sx, sy, pw, ph, 0x0000)
    end
end

local function drawCanvasAll()
    -- draw canvas background white once
    local c = layout.canvas
    WIN_fillRect(win, 1, c.x, c.y, c.w, c.h, 0xFFFF)
    WIN_drawRect(win, 1, c.x, c.y, c.w, c.h, 0x0000)

    -- draw only visible cells
    for gy = layout.visibleGY1, layout.visibleGY2 do
        for gx = layout.visibleGX1, layout.visibleGX2 do
            drawCell(gx, gy)
            -- update prevPixels cache
            prevPixels[idx(gx, gy)] = pixels[idx(gx, gy)]
        end
    end
end

-- Draw static UI: toolbar, buttons, palette. Keep mostly white.
local function drawStatic()
    -- background (mostly white)
    WIN_fillBg(win, 1, 0xFFFF)

    -- toolbar background
    local tb = layout.toolbar
    WIN_fillRect(win, 1, tb.x, tb.y, tb.w, tb.h, 0xFFFF)
    WIN_drawRect(win, 1, tb.x, tb.y, tb.w, tb.h, 0x0000)
    WIN_writeText(win, 1, tb.x + 6, tb.y + 8, string.format("Pixel Painter — %dx%d", gridSize, gridSize), 2, 0x0000)

    -- toolbar buttons (light design)
    local bx = tb.x + tb.w - 10
    local bw, bh = 44, 24
    local spacing = 6

    -- Save
    bx = bx - bw
    WIN_fillRect(win, 1, bx, tb.y + 6, bw, bh, 0xFFFF)
    WIN_drawRect(win, 1, bx, tb.y + 6, bw, bh, 0x0000)
    WIN_writeText(win, 1, bx + 6, tb.y + 10, "Save", 1, 0x0000)
    layout.btnSave = {
        x = bx,
        y = tb.y + 6,
        w = bw,
        h = bh
    }
    bx = bx - spacing

    -- Load
    bx = bx - bw
    WIN_fillRect(win, 1, bx, tb.y + 6, bw, bh, 0xFFFF)
    WIN_drawRect(win, 1, bx, tb.y + 6, bw, bh, 0x0000)
    WIN_writeText(win, 1, bx + 6, tb.y + 10, "Load", 1, 0x0000)
    layout.btnLoad = {
        x = bx,
        y = tb.y + 6,
        w = bw,
        h = bh
    }
    bx = bx - spacing

    -- New
    bx = bx - bw
    WIN_fillRect(win, 1, bx, tb.y + 6, bw, bh, 0xFFFF)
    WIN_drawRect(win, 1, bx, tb.y + 6, bw, bh, 0x0000)
    WIN_writeText(win, 1, bx + 6, tb.y + 10, "New", 1, 0x0000)
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
    WIN_fillRect(win, 1, bx, tb.y + 6, zbw, bh, 0xFFFF)
    WIN_drawRect(win, 1, bx, tb.y + 6, zbw, bh, 0x0000)
    WIN_writeText(win, 1, bx + 8, tb.y + 10, "-", 2, 0x0000)
    layout.btnZoomOut = {
        x = bx,
        y = tb.y + 6,
        w = zbw,
        h = bh
    }
    bx = bx - spacing

    -- Zoom In
    bx = bx - zbw
    WIN_fillRect(win, 1, bx, tb.y + 6, zbw, bh, 0xFFFF)
    WIN_drawRect(win, 1, bx, tb.y + 6, zbw, bh, 0x0000)
    WIN_writeText(win, 1, bx + 8, tb.y + 10, "+", 2, 0x0000)
    layout.btnZoomIn = {
        x = bx,
        y = tb.y + 6,
        w = zbw,
        h = bh
    }

    -- Grid size controls (+/-)
    local gsW = 28
    local gsX = tb.x + tb.w - 10 - bw - spacing - bw - spacing - zbw * 2 - 8 - gsW * 2
    if gsX < tb.x + 6 then
        gsX = tb.x + 6
    end
    WIN_fillRect(win, 1, gsX, tb.y + 6, gsW, bh, 0xFFFF)
    WIN_drawRect(win, 1, gsX, tb.y + 6, gsW, bh, 0x0000)
    WIN_writeText(win, 1, gsX + 6, tb.y + 10, "-", 2, 0x0000)
    layout.btnGridDec = {
        x = gsX,
        y = tb.y + 6,
        w = gsW,
        h = bh
    }

    WIN_fillRect(win, 1, gsX + gsW + 4, tb.y + 6, gsW, bh, 0xFFFF)
    WIN_drawRect(win, 1, gsX + gsW + 4, tb.y + 6, gsW, bh, 0x0000)
    WIN_writeText(win, 1, gsX + gsW + 10, tb.y + 10, "+", 2, 0x0000)
    layout.btnGridInc = {
        x = gsX + gsW + 4,
        y = tb.y + 6,
        w = gsW,
        h = bh
    }

    -- Hand (pan) toggle
    local handX = tb.x + 6
    WIN_fillRect(win, 1, handX, tb.y + 6, 60, bh, 0xFFFF)
    WIN_drawRect(win, 1, handX, tb.y + 6, 60, bh, 0x0000)
    WIN_writeText(win, 1, handX + 6, tb.y + 10, handMode and "Hand:On" or "Hand:Off", 1, 0x0000)
    layout.btnHand = {
        x = handX,
        y = tb.y + 6,
        w = 60,
        h = bh
    }

    -- Eraser
    local erX = handX + 68
    WIN_fillRect(win, 1, erX, tb.y + 6, 60, bh, 0xFFFF)
    WIN_drawRect(win, 1, erX, tb.y + 6, 60, bh, 0x0000)
    WIN_writeText(win, 1, erX + 6, tb.y + 10, eraserMode and "Eraser:On" or "Eraser:Off", 1, 0x0000)
    layout.btnEraser = {
        x = erX,
        y = tb.y + 6,
        w = 60,
        h = bh
    }

    -- Palette
    local pal = layout.palette
    local sw = 26
    local px = pal.x
    for i = 1, #palette do
        WIN_fillRect(win, 1, px, pal.y, sw, sw, palette[i])
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
        px = px + sw + 8
    end

    -- instructions
    WIN_writeText(win, 1, pal.x + 200, pal.y + 6, "Tap canvas to paint — hand mode to pan", 1, 0x0000)

    staticDirty = false
    canvasDirtyAll = true
end

-- Serialization
local function serializePixels()
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
    for j = i, gridSize * gridSize do
        pixels[j] = 0xFFFF
    end
    canvasDirtyAll = true
    return true
end

-- Button helpers
local function pointInRect(px, py, r)
    return px >= r.x and px <= r.x + r.w and py >= r.y and py <= r.y + r.h
end

local function onSave()
    local ok, name = WIN_readText(win, "Save painting as:", "mypainting")
    if ok and name and #name > 0 then
        FS_set("paint_" .. name, serializePixels())
    end
end
local function onLoad()
    local ok, name = WIN_readText(win, "Load painting (name):", "mypainting")
    if ok and name and #name > 0 then
        local s = FS_get("paint_" .. name)
        if s then
            deserializePixels(s)
        end
    end
end
local function onNew()
    clearPixels(0xFFFF)
    canvasDirtyAll = true
end

-- Handle painting / panning input
local function handleCanvasTouch(tx, ty)
    local c = layout.canvas
    -- convert screen -> content coordinates
    local relX = tx - c.x + offsetX
    local relY = ty - c.y + offsetY
    if relX < 0 or relY < 0 then
        return false
    end
    local gx = math.floor(relX / layout.cellSize) + 1
    local gy = math.floor(relY / layout.cellSize) + 1
    if gx < 1 or gx > gridSize or gy < 1 or gy > gridSize then
        return false
    end
    local id = idx(gx, gy)
    local newColor = eraserMode and 0xFFFF or palette[curColorIdx]
    if pixels[id] ~= newColor then
        pixels[id] = newColor
        drawCell(gx, gy)
        prevPixels[id] = pixels[id]
    end
    return true
end

-- Redraw visible cells that changed (only these)
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
        -- recompute layout if window resized or similar
        local oldLayout = layout.winW
        computeLayout()
        if staticDirty or oldLayout ~= layout.winW then
            drawStatic()
        end

        -- if canvas needs full redraw
        if canvasDirtyAll then
            drawCanvasAll()
            canvasDirtyAll = false
        end

        if state > 0 then
            -- press began
            -- check toolbar buttons
            if pointInRect(x, y, layout.btnSave) then
                onSave()
            elseif pointInRect(x, y, layout.btnLoad) then
                onLoad()
            elseif pointInRect(x, y, layout.btnNew) then
                onNew()
            elseif pointInRect(x, y, layout.btnZoomIn) then
                scale = clamp(scale + 2, scale_min, scale_max)
                canvasDirtyAll = true
            elseif pointInRect(x, y, layout.btnZoomOut) then
                scale = clamp(scale - 2, scale_min, scale_max)
                canvasDirtyAll = true
            elseif pointInRect(x, y, layout.btnGridInc) then
                ensurePixelsSize(clamp(gridSize + 1, GRID_MIN, GRID_MAX))
                staticDirty = true
            elseif pointInRect(x, y, layout.btnGridDec) then
                ensurePixelsSize(clamp(gridSize - 1, GRID_MIN, GRID_MAX))
                staticDirty = true
            elseif pointInRect(x, y, layout.btnHand) then
                handMode = not handMode
                staticDirty = true
            elseif pointInRect(x, y, layout.btnEraser) then
                eraserMode = not eraserMode
                staticDirty = true
            else
                -- palette selection
                local pal = layout.palette
                for i = 1, #palette do
                    if pointInRect(x, y, layout["pal_" .. i]) then
                        curColorIdx = i
                        eraserMode = false
                        staticDirty = true
                        break
                    end
                end

                -- canvas interactions
                if x >= layout.canvas.x and x <= layout.canvas.x + layout.canvas.w and y >= layout.canvas.y and y <=
                    layout.canvas.y + layout.canvas.h then
                    if handMode then
                        -- start pan
                        dragPainting = false
                        dragStartX = x
                        dragStartY = y
                        dragStartOffsetX = offsetX
                        dragStartOffsetY = offsetY
                    else
                        -- paint start
                        handleCanvasTouch(x, y)
                        dragPainting = true
                    end
                end
            end
        else
            -- touch end
            dragPainting = false
            lastTouchCell = nil
        end

        -- dragging: paint or pan depending on mode
        if state > 0 and dragPainting then
            handleCanvasTouch(mx, my)
        elseif state > 0 and dragStartX and handMode then
            -- pan: compute delta
            local dx = dragStartX - mx
            local dy = dragStartY - my
            offsetX = clamp(dragStartOffsetX + dx, 0, maxOffsetX)
            offsetY = clamp(dragStartOffsetY + dy, 0, maxOffsetY)
            -- when panning, redraw visible canvas (fast: only visible cells)
            drawCanvasAll()
        end

        -- periodically check for changed cells to repaint minimal set
        redrawChangedCells()
    end

    delay(16)
end

WIN_close(win)
