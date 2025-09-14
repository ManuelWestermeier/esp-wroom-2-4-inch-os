local win = createWindow(50, 50, 200, 150)
WIN_setName(win, "My App")

while not WIN_closed(win) do
    WIN_drawVideo(win, "https://manuelwestermeier.github.io/video.rgb565")
    delay(1000)
end
