if args then
    for i, arg in ipairs(args) do
        print("Arg " .. i .. ": " .. arg)
    end
end

-- Morse map
morse = {
    ["A"] = ".-",
    ["B"] = "-...",
    ["C"] = "-.-.",
    ["D"] = "-..",
    ["E"] = ".",
    ["F"] = "..-.",
    ["G"] = "--.",
    ["H"] = "....",
    ["I"] = "..",
    ["J"] = ".---",
    ["K"] = "-.-",
    ["L"] = ".-..",
    ["M"] = "--",
    ["N"] = "-.",
    ["O"] = "---",
    ["P"] = ".--.",
    ["Q"] = "--.-",
    ["R"] = ".-.",
    ["S"] = "...",
    ["T"] = "-",
    ["U"] = "..-",
    ["V"] = "...-",
    ["W"] = ".--",
    ["X"] = "-..-",
    ["Y"] = "-.--",
    ["Z"] = "--.."
}

function flash(symbol)
    if symbol == "." then
        setLED(1)
        delay(100)
        setLED(0)
    elseif symbol == "-" then
        setLED(1)
        delay(300)
        setLED(0)
    end
    delay(100) -- pause between symbols
end

if ConnectToWifi("io", "hhhhhh90") then
    print("WLAN verbunden!")
else
    print("WLAN-Verbindung fehlgeschlagen!")
end

msg = httpsReq({
    method = "GET",
    url = "https://manuelwestermeier.github.io/test.txt",
}).body

print("Body", msg)

for c in msg:gmatch(".") do
    c = c:upper()
    code = morse[c]
    if code then
        print(c .. ": " .. code)
        for i = 1, #code do
            flash(code:sub(i, i))
        end
        delay(300) -- pause between letters
    end
end
