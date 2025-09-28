# ESP32 WROOM 2.4-inch Display OS Documentation

## Overview

This operating system runs on ESP32 WROOM with a 2.4-inch display, supporting Lua-based applications with a windowing system, networking capabilities, and file system access.

11815 lines code currently

---

## Application Structure

Each application consists of several required files hosted on a web server:

### Required Files

- `https://[name].onrender.com/entry.lua` - Main Lua entry script
- `https://[name].onrender.com/icon-20x20.raw` - 20x20 pixel icon (16-bit format)
- `https://[name].onrender.com/pkg.txt` - List of additional files to download
- `https://[name].onrender.com/version.txt` - Version information
- `https://[name].onrender.com/name.txt` - Application name

### Icon Conversion

Use [this converter](https://manuelwestermeier.github.io/to-16-bit/video-audio) to convert videos with audio to the required 16-bit format.

<!-- Use [this converter](https://manuelwestermeier.github.io/to-16-bit/image) to convert images to the required 16-bit RAW format. -->

<!-- Use [this converter](https://manuelwestermeier.github.io/to-16-bit/video) to convert videos to the required 16-bit RAW format. -->

<!-- Use [this converter](https://manuelwestermeier.github.io/to-16-bit/audio) to convert audio to the required RAW format. -->

---

## Lua API Reference

### Window Management

```lua
local windowId = createWindow(50, 50, 200, 150)
WIN_setName(windowId, "My App")
local x, y, width, height = WIN_getRect(windowId)
local pressed, state, posX, posY, moveX, moveY, wasClicked = WIN_getLastEvent(windowId, 1)
local isClosed = WIN_closed(windowId)
WIN_close(windowId)
```

### Drawing Functions

```lua
WIN_fillBg(windowId, 1, 0xFFFF)  -- White background
WIN_writeText(windowId, 1, 10, 10, "Hello World", 2, 0x0000)  -- Black text
WIN_fillRect(windowId, 1, 20, 20, 100, 50, 0xF800)  -- Red rectangle
WIN_drawPixel(windowId, 1, 50, 50, 0x001F)  -- Blue pixel

local pixels = {0xFFFF, 0x0000, 0xFFFF, 0x0000}
WIN_drawImage(windowId, 1, 0, 0, 2, 2, pixels)

local iconPixels = {0xFFFF, 0x0000, ...}
WIN_setIcon(windowId, iconPixels)
```

### Graphics Primitives

```lua
WIN_drawLine(windowId, 1, 0, 0, 100, 100, 0x07E0)
WIN_drawRect(windowId, 1, 10, 10, 80, 60, 0x001F)
WIN_drawCircle(windowId, 1, 50, 50, 30, 0xF800)
WIN_fillCircle(windowId, 1, 50, 50, 30, 0x07E0)
WIN_drawTriangle(windowId, 1, 0, 0, 50, 100, 100, 0, 0xFFFF)
WIN_fillTriangle(windowId, 1, 0, 0, 50, 100, 100, 0, 0xFFE0)
WIN_drawRoundRect(windowId, 1, 10, 10, 80, 60, 10, 0x001F)
WIN_fillRoundRect(windowId, 1, 10, 10, 80, 60, 10, 0x07E0)

local svgData = "<svg><circle cx='50' cy='50' r='40'/></svg>"
WIN_drawSVG(windowId, 1, svgData, 0, 0, 100, 100, 0xF800, 10)
```

### Input Handling

```lua
local ok, text = WIN_readText(windowId, "Enter your name:", "Anonymous")
if ok then
    print("You entered: " .. text)
end
```

### System Functions

```lua
print("Hello", "World", 42)
delay(1000)
setLED(1)
local color = RGB(255, 0, 0)

local theme = getTheme()
print(theme.bg)
print(theme.primary)
print(theme.text)
```

### File System Functions

```lua
local data = FS_get("settings")
FS_set("settings", "user preferences data")
FS_del("temp_data")
```

### Network Functions

```lua
local response = httpReq({ method = "GET", url = "http://example.com/api" })
local response = httpsReq({ method = "POST", url = "https://api.example.com/data", body = '{"key":"value"}' })
local response = net.http_request({ method = "GET", host = "example.com", path = "/api", timeout_ms = 5000 })
local socket = net.tcp_connect("example.com", 80, 5000)
local udp = net.udp_open(1234)
local ip = net.dns_lookup("example.com")
local status = net.wifi_status()
local connected = net.has_internet()
```

### Advanced Execution

```lua
local result = exec(1, "print('Hello World')")
local result = exec(2, "math_utils")
local result = exec(3, "scripts/main.lua")
local result = exec(4, "username/repo/main.lua")
```

---

## Color Constants

- `0x0000` - Black
- `0xFFFF` - White
- `0xF800` - Red
- `0x07E0` - Green
- `0x001F` - Blue
- `0xFFE0` - Yellow
- `0xF81F` - Magenta
- `0x07FF` - Cyan

```lua
local color = RGB(red, green, blue) -- all from 0-255
```

---

## Example Application

```lua
local win = createWindow(50, 50, 200, 150)
WIN_setName(win, "My App")

while not WIN_closed(win) do
    WIN_fillBg(win, 1, 0xFFFF)
    WIN_writeText(win, 1, 10, 10, "Hello World", 2, 0x0000)

    local pressed, state, x, y = WIN_getLastEvent(win, 1)
    if pressed then
        WIN_writeText(win, 1, 10, 30, "Clicked at: " .. x .. "," .. y, 1, 0xF800)
    end

    delay(16) -- ~60 FPS
end
```
