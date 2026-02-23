-- entry.lua (Anim Bounce - 150x150)
local win = createWindow(10, 10, 150, 150)
WIN_setName(win, "Bounce")
local x, y, vx, vy, r = 75, 50, 2.2, 1.6, 10
local paused = false

while not WIN_closed(win) do
    local pressed, state, px, py, mx, my, wc, nr = WIN_getLastEvent(win, 1)
    if state > 0 then paused = not paused end

    if not paused then
        x = x + vx; y = y + vy
        if x - r < 0 then x = r; vx = -vx end
        if x + r > 150 then x = 150 - r; vx = -vx end
        if y - r < 0 then y = r; vy = -vy end
        if y + r > 150 then y = 150 - r; vy = -vy end
    end

    -- redraw every loop
    WIN_fillBg(win, 1, 0xFFFF)
    WIN_writeText(win, 1, 6, 6, paused and "Paused" or "Running", 1, 0x0000)
    WIN_drawCircle(win, 1, x, y, r, 0xF800)
    WIN_fillCircle(win, 1, x, y, r-2, 0x07E0)
    WIN_finishFrame(win)
    delay(16)
end