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
        delay(200)
        setLED(0)
    elseif symbol == "-" then
        setLED(1)
        delay(600)
        setLED(0)
    end
    delay(200) -- pause between symbols
end

msg = "HELLO"
print("Morse: " .. msg)

for c in msg:gmatch(".") do
    c = c:upper()
    code = morse[c]
    if code then
        print(c .. ": " .. code)
        for i = 1, #code do
            flash(code:sub(i, i))
        end
        delay(600) -- pause between letters
    end
end
