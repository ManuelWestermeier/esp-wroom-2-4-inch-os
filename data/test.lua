local WHITE = 0xFFFF
local BLACK = 0x0000
local WIN_SIZE = 150

local win = createWindow(10, 10, WIN_SIZE, WIN_SIZE)
WIN_setName(win, "Paint")
WIN_fillBg(win, 1, WHITE)

while not WIN_closed(win) do
    local pressed, _, gx, gy = WIN_getLastEvent(win, 1)
    
    if pressed then
        WIN_fillRect(win, 1, gx, gy, 10, 10, BLACK)
    end

    delay(10) -- lightweight input polling
end
