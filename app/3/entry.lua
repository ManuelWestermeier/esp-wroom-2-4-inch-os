-- Simple one-shot Paint: fill white once, then each press draws a 3x3 dot.
print("Paint start")
local win = createWindow(10,10,180,180)
WIN_setName(win,"Paint")
local function int(n) return math.floor(tonumber(n) or 0) end

-- initial white fill (only once)
local _,_,w,h = WIN_getRect(win)
WIN_fillRect(win, 1, 0, 0, int(w), int(h), 0xFFFF)

-- draw a centered 3x3 at (x,y) with color (black or white)
local function drawDot(x,y,col)
    WIN_fillRect(win, 1, x-1, y-1, 3, 3, col)
end

-- Main loop: left half tap -> black dot; right half tap -> white dot (erase).
while not WIN_closed(win) do
  local pressed, state, x, y, mx, my, wc, nr = WIN_getLastEvent(win, 1)
  if state > 0 then
    local col = state == 1 and 0xFFFF or 0x0000
    drawDot(x, y, col)
  end
  delay(10)
end

print("Paint exit")