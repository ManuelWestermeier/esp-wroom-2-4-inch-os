local win = createWindow(50, 50, 200, 150)
WIN_setName(win, "My App")

-- Button rectangle (relative to window client area)
local btnX, btnY, btnW, btnH = 20, 50, 160, 50
local videoStarted = false

-- state for smart rendering
local firstRender = true
local prevRect = {x=0,y=0,w=0,h=0}
local lastRandomX, lastRandomY = 0, 0
local lastRandomColor = nil
local screenId = 1          -- using screen 1 for client drawing
local rndCheckCounter = 0   -- throttle random-check slightly

-- helper: full rerender
local function fullRender(theme, pressedState)
    -- background
    WIN_fillBg(win, screenId, theme.bg)

    -- draw button (normal / pressed)
    if pressedState then
        -- pressed: draw inset smaller rect to simulate press
        WIN_fillRoundRect(win, screenId, btnX+2, btnY+4, btnW-4, btnH-6, 8, theme.primary)
        WIN_writeText(win, screenId, btnX + 12, btnY + 17, "Click to play video", 2, theme.text)
    else
        -- normal
        WIN_fillRoundRect(win, screenId, btnX, btnY, btnW, btnH, 10, theme.primary)
        WIN_writeText(win, screenId, btnX + 10, btnY + 15, "Click to play video", 2, theme.text)
    end
end

-- initial theme pull
local theme = getTheme()

-- main loop
while not WIN_closed(win) do
    -- get current rect
    local rx, ry, rw, rh = WIN_getRect(win)
    -- detect resize or move by comparing to prevRect
    local movedOrResized = (rx ~= prevRect.x) or (ry ~= prevRect.y) or (rw ~= prevRect.w) or (rh ~= prevRect.h)

    -- get input event (pressed, state, posX, posY, moveX, moveY, wasClicked)
    local pressed, state, px, py, moveX, moveY, wasClicked = WIN_getLastEvent(win, 1)

    -- detect movement using moveX/moveY or the rect diff
    local movedByEvent = (moveX ~= nil and moveX ~= 0) or (moveY ~= nil and moveY ~= 0)
    if movedOrResized or movedByEvent then
        -- update prevRect values for next loop
        prevRect.x, prevRect.y, prevRect.w, prevRect.h = rx, ry, rw, rh
    end

    local needFullRender = false
    if firstRender then
        needFullRender = true
    elseif movedOrResized or movedByEvent then
        needFullRender = true
    end

    -- If video is not running: perform a randomized pixel-check to detect external changes.
    -- We throttle the checks every few frames to save cycles.
    if not videoStarted and not needFullRender then
        rndCheckCounter = rndCheckCounter + 1
        if rndCheckCounter >= 3 then
            rndCheckCounter = 0
            -- pick a pixel inside the window client area (avoid edges)
            lastRandomX = (math.random(0, math.max(0, rw-1)))
            lastRandomY = (math.random(0, math.max(0, rh-1)))
            -- read pixel from the window area using the new API
            local okColor = WIN_readPixel(win, screenId, lastRandomX, lastRandomY)
            if lastRandomColor == nil then
                lastRandomColor = okColor
                -- first-time set -> don't force a rerender (unless firstRender)
            elseif okColor ~= lastRandomColor then
                -- pixel changed externally -> full rerender
                needFullRender = true
                lastRandomColor = okColor
            end
        end
    end

    -- if theme may have changed, refresh theme and trigger rerender
    -- (getTheme is cheap; compare values)
    local curTheme = getTheme()
    if curTheme.bg ~= theme.bg or curTheme.primary ~= theme.primary or curTheme.text ~= theme.text then
        theme = curTheme
        needFullRender = true
    end

    -- Render logic
    if videoStarted then
        -- while video plays, let WIN_drawVideo handle frame updates (no UI full rerender)
        WIN_drawVideo(win, "https://github.com/ManuelWestermeier/manuelwestermeier.github.io/releases/download/dev/vid-small.rgb565")
    else
        -- draw UI only when needed (or when pressed state for immediate feedback)
        -- give pressed a quick immediate visual even if we wouldn't otherwise full render
        local pressedInside = false
        if pressed then
            -- click coordinates are within the window client area; check button hit
            if px >= btnX and px <= (btnX + btnW) and py >= btnY and py <= (btnY + btnH) then
                pressedInside = true
            end
        end

        if needFullRender or pressedInside then
            fullRender(theme, pressedInside)
            firstRender = false
        end
    end

    -- Input handling: start video on click inside button
    if wasClicked and (not videoStarted) then
        -- Use wasClicked (preferred) but fallback to pressed detection if needed
        local clickX, clickY = px, py
        if clickX >= btnX and clickX <= (btnX + btnW) and clickY >= btnY and clickY <= (btnY + btnH) then
            videoStarted = true
            -- mark full render on next loop so theme/background clears before video starts
            needFullRender = true
        end
    end

    -- small delay for responsiveness; ~60 FPS loop
    delay(16)
end
