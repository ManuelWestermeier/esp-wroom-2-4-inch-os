-- Simple Paint (black & white)
print("Paint start")
local win=createWindow(10,10,180,180)
WIN_setName(win,"Paint")

local canvasX,canvasY,canvasW,canvasH=0,0,180,180
local color=0x0000
local drawing=false
local lastX,lastY

-- draw full canvas
local function drawCanvas()
 WIN_fillRect(win,1,canvasX,canvasY,canvasW,canvasH,0xFFFF)
end

local function stamp(x,y)
 if x<canvasX or x>=canvasX+canvasW or y<canvasY or y>=canvasY+canvasH then return end
 WIN_fillRect(win,1,x-2,y-2,4,4,color)
end

local function line(x1,y1,x2,y2)
 local dx,dy=x2-x1,y2-y1
 local dist=math.sqrt(dx*dx+dy*dy)
 local n=math.ceil(dist/2)
 for i=0,n do local t=i/n; stamp(x1+dx*t,y1+dy*t) end
end

drawCanvas()
WIN_finishFrame(win)

while not WIN_closed(win) do
 local pressed,s,x,y,_,_,_,nr=WIN_getLastEvent(win,1)
 if nr or s>0 then
  if s>=0 then
   if x<10 and y<10 then drawCanvas(); WIN_finishFrame(win) -- clear
   else
    if s==0 then drawing=true; lastX,lastY=x,y; stamp(x,y); WIN_finishFrame(win)
    elseif s==1 and drawing then line(lastX,lastY,x,y); lastX,lastY=x,y; WIN_finishFrame(win)
    else drawing=false end
   end
  end
 end
 delay(10)
end
print("Paint exit")