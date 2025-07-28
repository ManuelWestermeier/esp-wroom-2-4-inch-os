win = createWindow(0, 0, 100, 100)
WIN_setName(win, "Test App :)")

bigScreen = 1 
leftScreen = 2 

bgColor = RGB(230, 230, 230)
textColor = RGB(20, 20, 20)

while not WIN_closed(win) do
    x, y, w, h = WIN_getRect(win, bigScreen)
    happened, state, posX, posY, moveX, moveY = WIN_getLastEvent(win, bigScreen)

    WIN_fillBg(win, bigScreen, bgColor)
    WIN_fillBg(win, leftScreen, RGB(100,200,100))
    WIN_writeText(win, bigScreen, 10, 10, "!Hello World!", 2, textColor)

    WIN_writeText(win, bigScreen, 10, 30, 
        string.format("x=%d y=%d w=%d h=%d", x, y, w, h), 1, textColor)

    if happened then
        WIN_writeRect(win, bigScreen, posX, posY, 5, 5, textColor)
    end

    delay(10)
end
