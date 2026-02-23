-- entry.lua (Mini Jump & Run - fixed)
local win = createWindow(10, 10, 150, 150)
WIN_setName(win, "Jump Run")
local function int(n) return math.floor(tonumber(n) or 0) end

local groundY = 120
local px, py = 20, groundY
local vy = 0
local gravity = 0.6
local jumpPower = -7
local onGround = true

local obstacles = {}
local spawnTimer = 0
local speed = 2
local score = 0
local alive = true

local function spawnObstacle()
    obstacles[#obstacles+1] = { x = 150, w = 10 + math.random(10), h = 10 + math.random(15) }
end

local function resetGame()
    px, py = 20, groundY; vy = 0; obstacles = {}; score=0; speed=2; alive=true
end

while not WIN_closed(win) do
    local pressed, state = WIN_getLastEvent(win, 1)
    if alive and state>0 and onGround then vy = jumpPower; onGround=false
    elseif not alive and state>0 then resetGame() end

    if alive then
        vy = vy + gravity; py = py + vy
        if py >= groundY then py = groundY; vy = 0; onGround = true end
        spawnTimer = spawnTimer + 1
        if spawnTimer > 70 then spawnObstacle(); spawnTimer = 0 end

        for i = #obstacles,1,-1 do
            local o = obstacles[i]
            o.x = o.x - speed
            if px+8 > o.x and px < o.x+o.w and py+8 > groundY-o.h then alive=false end
            if o.x + o.w < 0 then table.remove(obstacles,i); score = score + 1; speed = speed + 0.05 end
        end
    end

    WIN_fillBg(win,1,0xFFFF)
    WIN_fillRect(win,1,int(0),int(groundY+8),int(150),int(30),0x07E0)
    WIN_fillRect(win,1,int(px),int(py),int(8),int(8),0x001F)
    for _,o in ipairs(obstacles) do
        WIN_fillRect(win,1,int(o.x),int(groundY-o.h+8),int(o.w),int(o.h),0xF800)
    end
    WIN_writeText(win,1,4,4,"Score: "..score,1,0x0000)
    if not alive then
        WIN_writeText(win,1,25,60,"GAME OVER",2,0xF800)
        WIN_writeText(win,1,18,80,"Tap to restart",1,0x0000)
    end
    WIN_finishFrame(win)
    delay(16)
end