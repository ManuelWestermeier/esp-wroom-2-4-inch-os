local win = createWindow(50, 50, 200, 150)
WIN_setName(win, "Hello World")

while not WIN_closed(win) do
    local pressed, state, x, y, mx, my, wc, nr = WIN_getLastEvent(win, 1)

    if nr then
        WIN_finishFrame(win)
    end

    if nr then
        WIN_fillBg(win, 1, 0xFFFF)
        WIN_writeText(win, 1, 10, 10, "Hello World", 2, 0x0000)
    end

    if pressed then
        WIN_writeText(win, 1, 10, 30, "Clicked at: " .. x .. "," .. y, 1, 0xF800)
    end

    delay(16) -- ~60 FPS

    if nr then
        WIN_writeText(win, 1, 20, 30, "R", 1, 0xF800)
    else
        WIN_writeText(win, 1, 20, 30, "N", 1, 0xF800)
    end
end
