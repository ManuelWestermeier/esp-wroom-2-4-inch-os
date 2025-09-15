local win = createWindow(50, 50, 200, 150)
WIN_setName(win, "My App")

-- Button rectangle
local btnX, btnY, btnW, btnH = 20, 50, 160, 50
local videoStarted = false

while not WIN_closed(win) do
    WIN_fillBg(win, 1, 0xFFFF) -- White background

    if not videoStarted then
        -- Draw "button"
        WIN_fillRoundRect(win, 1, btnX, btnY, btnW, btnH, 10, 0x07E0) -- Green button
        WIN_writeText(win, 1, btnX + 10, btnY + 15, "Click to play video", 2, 0x0000)
    else
        -- Video playing
        WIN_drawVideo(win, "https://github.com/ManuelWestermeier/manuelwestermeier.github.io/releases/download/dev/vid-small.rgb565")
    end

    -- Handle input
    local pressed, state, x, y = WIN_getLastEvent(win, 1)
    if pressed and not videoStarted then
        -- Check if click is inside button
        if x >= btnX and x <= (btnX + btnW) and y >= btnY and y <= (btnY + btnH) then
            videoStarted = true
        end
    end

    delay(16) -- ~60 FPS
end
