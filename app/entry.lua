-- Window setup
local win = createWindow(50, 50, 200, 150)
WIN_setName(win, "My App")

-- Button rectangle (relative to client area)
local btnX, btnY, btnW, btnH = 20, 50, 160, 50
local videoStarted = false

-- Smart rendering state
local screenId = 1
local prevRect = {x = 0, y = 0, w = 0, h = 0}
local lastPixelX, lastPixelY = 50, 20
local lastPixelColor = nil
local rndCheckCounter = 0

-- Theme
local theme = getTheme()

-- Full render helper (background + button)
local function fullRender(pressed)
    -- background
    WIN_fillBg(win, screenId, theme.bg)

    -- button
    if pressed then
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

-- Initial full render
fullRender(false)

-- Main loop
while not WIN_closed(win) do
    -- Detect move/resize
    local rx, ry, rw, rh = WIN_getRect(win)
    local movedOrResized = (rx ~= prevRect.x or ry ~= prevRect.y or rw ~=
                               prevRect.w or rh ~= prevRect.h)
    prevRect.x, prevRect.y, prevRect.w, prevRect.h = rx, ry, rw, rh

    -- Get input
    local pressed, state, px, py, moveX, moveY, wasClicked = WIN_getLastEvent(
                                                                 win, 1)
    local pressedInside = px and py and pressed and
                              (px >= btnX and px <= btnX + btnW and py >= btnY and
                                  py <= btnY + btnH)

    -- Pixel-based smart render
    rndCheckCounter = rndCheckCounter + 1
    local needFullRender = movedOrResized or pressedInside

    if not videoStarted then
        if rndCheckCounter >= 1 then -- every frame
            rndCheckCounter = 0
            -- draw a random pixel
            local r = math.random(0, 31)
            local g = math.random(0, 63)
            local b = math.random(0, 31)
            local color = (r << 11) | (g << 5) | b
            WIN_drawPixel(win, screenId, lastPixelX, lastPixelY, color)

            -- read it back to detect changes
            local readColor = WIN_readPixel(win, screenId, lastPixelX,
                                            lastPixelY)
            if lastPixelColor ~= nil and readColor ~= lastPixelColor then
                needFullRender = true
            end
            lastPixelColor = color
        end
    end

    -- Theme check
    local curTheme = getTheme()
    if curTheme.bg ~= theme.bg or curTheme.primary ~= theme.primary or
        curTheme.text ~= theme.text then
        theme = curTheme
        needFullRender = true
    end

    -- Render
    if videoStarted then
        WIN_drawVideo(win,
                      "https://github.com/ManuelWestermeier/manuelwestermeier.github.io/releases/download/dev/vid-small.rgb565")
    elseif needFullRender then
        fullRender(pressedInside)
    end

    -- Button click handling
    if wasClicked and px and py and px >= btnX and px <= btnX + btnW and py >=
        btnY and py <= btnY + btnH then
        videoStarted = true
        needFullRender = true
    end

    delay(16) -- ~60 FPS
end
