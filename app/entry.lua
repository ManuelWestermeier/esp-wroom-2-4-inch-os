-- Window setup
local win = createWindow(50, 50, 200, 150)
WIN_setName(win, "My App")

-- Main loop
while not WIN_closed(win) do

    -- Render
    WIN_drawVideo(win,
                  "https://github.com/ManuelWestermeier/manuelwestermeier.github.io/releases/download/dev/vid.vam")

    delay(16) -- ~60 FPS
end
