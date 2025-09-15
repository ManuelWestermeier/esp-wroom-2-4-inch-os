-- Create window and basic setup
local win = createWindow(50, 50, 200, 150)
WIN_setName(win, "My App")

-- Button rectangle (relative to window client area)
local btnX, btnY, btnW, btnH = 20, 50, 160, 50
local videoStarted = false

-- state for smart rendering
local firstRender = true
local prevRect = {x = 0, y = 0, w = 0, h = 0}
local lastRandomX, lastRandomY = 0, 0
local lastRandomColor = nil
local screenId = 1
local rndCheckCounter = 0

-- initial theme
local theme = getTheme()

-- helper: full rerender
local function fullRender(theme, pressedState)
    -- background
    WIN_fillBg(win, screenId, theme.bg)

    -- draw button
    if pressedState then
        WIN_fillRoundRect(win, screenId, btnX + 2, btnY + 4, btnW - 4, btnH - 6,
                          8, theme.primary)
        WIN_writeText(win, screenId, btnX + 12, btnY + 17,
                      "Click to play video", 2, theme.text)
    else
        WIN_fillRoundRect(win, screenId, btnX, btnY, btnW, btnH, 10,
                          theme.primary)
        WIN_writeText(win, screenId, btnX + 10, btnY + 15,
                      "Click to play video", 2, theme.text)
    end
end

-- perform initial full render
fullRender(theme, false)
firstRender = false

-- main loop
while not WIN_closed(win) do
    local rx, ry, rw, rh = WIN_getRect(win)
    local movedOrResized = (rx ~= prevRect.x or ry ~= prevRect.y or rw ~=
                               prevRect.w or rh ~= prevRect.h)
    prevRect.x, prevRect.y, prevRect.w, prevRect.h = rx, ry, rw, rh

    local pressed, state, px, py, moveX, moveY, wasClicked = WIN_getLastEvent(
                                                                 win, 1)
    local pressedInside = px and py and pressed and
                              (px >= btnX and px <= btnX + btnW and py >= btnY and
                                  py <= btnY + btnH)

    local needFullRender = firstRender or movedOrResized or pressedInside

    -- Random pixel check for external changes
    if not videoStarted and not needFullRender then
        rndCheckCounter = rndCheckCounter + 1
        if rndCheckCounter >= 3 then
            rndCheckCounter = 0
            local x = math.random(0, math.max(0, rw - 1))
            local y = math.random(0, math.max(0, rh - 1))
            local c = WIN_readPixel(win, screenId, x, y)
            if lastRandomColor and c ~= lastRandomColor then
                needFullRender = true
            end
            lastRandomColor = c
        end
    end

    -- Theme check
    local curTheme = getTheme()
    if curTheme.bg ~= theme.bg or curTheme.primary ~= theme.primary or
        curTheme.text ~= theme.text then
        theme = curTheme
        needFullRender = true
    end

    -- Rendering
    if videoStarted then
        WIN_drawVideo(win,
                      "https://github.com/ManuelWestermeier/manuelwestermeier.github.io/releases/download/dev/vid-small.rgb565")
    elseif needFullRender then
        fullRender(theme, pressedInside)
    end

    -- Input handling
    if wasClicked and px and py and px >= btnX and px <= btnX + btnW and py >=
        btnY and py <= btnY + btnH then
        videoStarted = true
        needFullRender = true
    end

    delay(16) -- ~60 FPS
end
