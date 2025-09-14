local win = createWindow(50, 50, 200, 150)
WIN_setName(win, "My App")

while not WIN_closed(win) do
    WIN_drawVideo(win, "https://github.com/ManuelWestermeier/manuelwestermeier.github.io/raw/refs/heads/main/video.rgb565")
    delay(1000)
end
