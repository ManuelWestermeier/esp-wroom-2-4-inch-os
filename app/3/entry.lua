-- Simple one-shot Paint: fill white once, then each press draws a 3x3 dot.
print("Paint start")
local win = createWindow(10,10,180,180)
WIN_setName(win,"Paint")
local function int(n) return math.floor(tonumber(n) or 0) end

-- initial white fill (only once)
local _,_,w,h = WIN_getRect(win)
WIN_fillRect(win, 1, 0, 0, int(w), int(h), 0xFFFF)
WIN_finishFrame(win)

-- draw a centered 3x3 at (x,y) with color (black or white)
local function drawDot(x,y,col)
  x = int(x); y = int(y)
  local x0 = math.max(0, x-1)
  local y0 = math.max(0, y-1)
  -- clamp to window size
  local w0 = math.min(3, int(w) - x0)
  local h0 = math.min(3, int(h) - y0)
  if w0>0 and h0>0 then
    WIN_fillRect(win, 1, x0, y0, w0, h0, col)
    WIN_finishFrame(win)
  end
end

-- Main loop: left half tap -> black dot; right half tap -> white dot (erase).
while not WIN_closed(win) do
  local pressed, state, x, y, mx, my, wc, nr = WIN_getLastEvent(win, 1)
  if state == 0 then
    local col = (x >= (w/2)) and 0xFFFF or 0x0000
    drawDot(x, y, col)
  end
  delay(10)
end

print("Paint exit")