print("APP:STARTED")

local win = createWindow(20, 20, 240, 160)
WIN_setName(win, "Paint")

local bigScreen = 1
local leftScreen = 2

local totalW, totalH = 240, 160
local sidebarW    = math.floor(totalW * 0.15)
local colorBarW   = 24
local canvasX     = sidebarW
local canvasW     = totalW - sidebarW - colorBarW
local canvasH     = totalH

local bgColor      = RGB(230,230,230)
local textColor    = RGB(20,20,20)
local white        = RGB(255,255,255)

local brushModes = { "brush", "eraser", "square" }
local brushMode  = "brush"

local brushSize = 10
local minSize, maxSize = 1, 30

local palette = {
  RGB(0,0,0), RGB(255,0,0), RGB(0,255,0), RGB(0,0,255),
  RGB(255,255,0), RGB(255,165,0), RGB(128,0,128), RGB(255,192,203),
  RGB(0,255,255), RGB(128,128,128)
}
local currentColor = palette[1]

local colorScrollY = 0
local isColorScrolling = false
local colorScrollStartY, colorScrollStartOffset = 0, 0

local lastX, lastY = nil, nil
local isDrawing = false

local function clearCanvas()
  WIN_writeRect(win, bigScreen, canvasX, 0, canvasW, canvasH, white)
end

local function drawSidebar()
  WIN_writeRect(win, bigScreen, 0, 0, sidebarW, canvasH, bgColor)
  WIN_writeRect(win, bigScreen, 2, 2, sidebarW-4, 16, RGB(200,200,200))
  WIN_writeText(win, bigScreen, 4, 4, "mode: " .. brushMode, 1, textColor)

  WIN_writeRect(win, bigScreen, 2, 20, sidebarW-4, 16, RGB(220,80,80))
  WIN_writeText(win, bigScreen, 4, 22, "clear", 1, white)

  local sliderX = math.floor(sidebarW/2)
  local sliderY = 40
  local sliderH = canvasH - sliderY - 4
  WIN_writeRect(win, bigScreen, sliderX-2, sliderY, 4, sliderH, RGB(180,180,180))

  local pct = (brushSize - minSize) / (maxSize - minSize)
  local cursorY = sliderY + math.floor(pct * (sliderH-1))
  local r = math.floor(brushSize/2)
  for dx=-r,r do
    for dy=-r,r do
      if dx*dx + dy*dy <= r*r then
        WIN_writeRect(win, bigScreen, sliderX + dx, cursorY + dy, 1, 1, currentColor)
      end
    end
  end
end

local function drawCanvas()
  WIN_writeRect(win, bigScreen, canvasX, 0, canvasW, canvasH, white)
  WIN_writeRect(win, bigScreen, canvasX-1, -1, canvasW+2, canvasH+2, textColor)
end

local function drawColorBar()
  WIN_writeRect(win, leftScreen, 0, 0, colorBarW, canvasH, RGB(200,200,200))
  local fieldH = 30
  for i, c in ipairs(palette) do
    local y = (i-1)*fieldH - colorScrollY
    if y+fieldH >= 0 and y < canvasH then
      WIN_writeRect(win, leftScreen, 0, y, colorBarW, fieldH, c)
    end
  end
end

local function paintAt(x,y)
  if x < canvasX or x >= canvasX+canvasW or y < 0 or y >= canvasH then return end
  local col = (brushMode=="eraser") and white or currentColor
  local half = math.floor(brushSize/2)

  if brushMode=="brush" or brushMode=="eraser" then
    for dx=-half,half do
      for dy=-half,half do
        if dx*dx+dy*dy <= half*half then
          WIN_writeRect(win, bigScreen, x+dx, y+dy,1,1,col)
        end
      end
    end
  elseif brushMode=="square" then
    WIN_writeRect(win, bigScreen, x-half, y-half, brushSize, brushSize, col)
  end
end

drawSidebar()
drawCanvas()
drawColorBar()

while not WIN_closed(win) do
  local redrawSide, redrawColor = false, false

  local h,s,px,py = WIN_getLastEvent(win, bigScreen)
  if h then
    if s==0 then
      if px>=2 and px<=sidebarW-2 and py>=2 and py<=18 then
        -- Korrekte Modus-Umschaltung
        for i,m in ipairs(brushModes) do
          if m == brushMode then
            brushMode = brushModes[(i % #brushModes) + 1]
            break
          end
        end
        redrawSide = true

      elseif px>=2 and px<=sidebarW-2 and py>=20 and py<=36 then
        clearCanvas()

      else
        local sliderX, sliderY, sliderH = math.floor(sidebarW/2), 40, canvasH-40-4
        if px>=sliderX-5 and px<=sliderX+5 and py>=sliderY and py<=sliderY+sliderH then
          local rel = py-sliderY; local pct = rel/sliderH
          brushSize = math.floor(minSize + pct*(maxSize-minSize) +0.5)
          brushSize = math.max(minSize, math.min(maxSize, brushSize))
          redrawSide = true
        end
        if px>=canvasX then isDrawing=true; lastX, lastY = px,py; paintAt(px,py) end
      end
    elseif s==1 and isDrawing then
      paintAt(px,py)
    elseif s==2 then isDrawing=false; lastX,lastY=nil,nil end
  end

  local h2,s2,px2,py2 = WIN_getLastEvent(win, leftScreen)
  if h2 then
    if s2==0 then isColorScrolling=true; colorScrollStartY=py2; colorScrollStartOffset=colorScrollY
    elseif s2==1 and isColorScrolling then
      local dy=py2-colorScrollStartY; colorScrollY = colorScrollStartOffset - dy
      local maxScroll = math.max(0,#palette*30 - canvasH)
      colorScrollY = math.max(0, math.min(maxScroll, colorScrollY))
      redrawColor = true
    elseif s2==2 then isColorScrolling=false end
    if s2==0 then
      local idx=math.floor((py2+colorScrollY)/30)+1
      if idx>=1 and idx<=#palette then currentColor=palette[idx]; brushMode="brush"; redrawSide=true end
    end
  end

  if redrawSide then drawSidebar() end
  if redrawColor then drawColorBar() end

  delay(10)
end

print("APP:EXITED")
