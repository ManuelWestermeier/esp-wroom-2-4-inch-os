-- Morse map
morse = {
  A = ".-", B = "-...", C = "-.-.", D = "-..", E = ".", F = "..-.",
  G = "--.", H = "....", I = "..", J = ".---", K = "-.-", L = ".-..",
  M = "--", N = "-.", O = "---", P = ".--.", Q = "--.-", R = ".-.",
  S = "...", T = "-", U = "..-", V = "...-", W = ".--", X = "-..-",
  Y = "-.--", Z = "--.."
}

function flash(symbol)
  if symbol == "." then
    toggle_led(2)
    for _ = 1, 100000 do end
    toggle_led(2)
  elseif symbol == "-" then
    toggle_led(2)
    for _ = 1, 300000 do end
    toggle_led(2)
  end
  for _ = 1, 100000 do end
end

msg = "HELLO"
print_serial("Morse: " .. msg)

for c in msg:gmatch(".") do
  code = morse[c]
  if code then
    print_serial(c .. ": " .. code)
    for i = 1, #code do
      flash(code:sub(i,i))
    end
  end
end
