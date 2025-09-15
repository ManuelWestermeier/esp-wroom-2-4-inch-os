-- always full render at start
fullRender(theme, false)
firstRender = false

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

    -- theme check
    local curTheme = getTheme()
    if curTheme.bg ~= theme.bg or curTheme.primary ~= theme.primary or
        curTheme.text ~= theme.text then
        theme = curTheme
        needFullRender = true
    end

    if videoStarted then
        WIN_drawVideo(win,
                      "https://github.com/ManuelWestermeier/manuelwestermeier.github.io/releases/download/dev/vid-small.rgb565")
    elseif needFullRender then
        fullRender(theme, pressedInside)
    end

    if wasClicked and px and py and px >= btnX and px <= btnX + btnW and py >=
        btnY and py <= btnY + btnH then
        videoStarted = true
        needFullRender = true
    end

    delay(16)
end
