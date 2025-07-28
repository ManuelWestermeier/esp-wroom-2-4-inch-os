print("APP:STARTED")

win = createWindow(20, 20, 180, 100)

WIN_setName(win, "Test App :)")

bigScreen = 1
leftScreen = 2

bgColor = RGB(230, 230, 230)
textColor = RGB(20, 20, 20)

-- Beispiel Icon 12x12 (alle Pixel weiß)
iconPixels = {}
local i = 1
for y = 1, 12 do
    for x = 1, 12 do
        local r = math.floor(27 + (x - 1) * (255 - 27) / 11)  -- Verläuft von 27 bis 255
        local g = math.floor(27 + (x - 1) * (27 - 27) / 11)   -- bleibt 27
        local b = math.floor(38 + (x - 1) * (100 - 38) / 11) -- Verläuft von 38 bis 100
        iconPixels[i] = RGB(r, g, b)
        i = i + 1
    end
end

WIN_setIcon(win, iconPixels)

WIN_fillBg(win, bigScreen, bgColor)

while not WIN_closed(win) do
    local x, y, w, h = WIN_getRect(win, bigScreen)
    local happened, state, posX, posY, moveX, moveY = WIN_getLastEvent(win, bigScreen)

    WIN_writeRect(win, bigScreen, 0, 0, w, 50, bgColor)
    WIN_fillBg(win, leftScreen, bgColor)

    WIN_drawImage(win, leftScreen, 0, 0, 12, 12, iconPixels)

    WIN_writeText(win, bigScreen, 10, 10, "!Hello World!", 2, textColor)
    WIN_writeText(win, bigScreen, 10, 30, string.format("x=%d y=%d w=%d h=%d", x, y, w, h), 1, textColor)
    WIN_writeText(win, leftScreen, 0, 15, tostring(state), 1, textColor)

    if happened and state < 2 and posY > 50 then
        WIN_writeRect(win, bigScreen, posX, posY, 2, 2, textColor)
    end

    delay(10)
end

print("APP:EXITED")
