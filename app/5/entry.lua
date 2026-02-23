-- entry.lua (Calculator - 150x150)
local win = createWindow(10, 10, 150, 150)
WIN_setName(win, "Calc")
local last = "Tap to enter expression"

local function safe_eval(expr)
    if not expr then
        return nil, "empty"
    end
    expr = expr:gsub("%s+", "")
    if not expr:match("^[0-9%+%-%*/%%%.%(%)]*$") then
        return nil, "invalid chars"
    end
    local loader = load or loadstring
    local f, err = loader("return " .. expr)
    if not f then
        return nil, err
    end
    local ok, res = pcall(f)
    if not ok then
        return nil, res
    end
    return res, nil
end

while not WIN_closed(win) do
    local pressed, state, x, y, mx, my, wc, nr = WIN_getLastEvent(win, 1)
    if nr or state > 0 then
        WIN_finishFrame(win)
        WIN_fillBg(win, 1, 0xFFFF)
        WIN_writeText(win, 1, 10, 8, "Calculator", 2, 0x0000)
        WIN_writeText(win, 1, 8, 32, last, 1, 0x001F)
        WIN_drawRect(win, 1, 10, 60, 130, 70, 0x0000)
        if state > 0 then
            local ok, expr = WIN_readText(win, "Expression (e.g. 2*(3+4))", "")
            if ok then
                local res, err = safe_eval(expr)
                if err then
                    last = "Error: " .. tostring(err)
                else
                    last = tostring(res)
                end
            end
        end
    end
    delay(50)
end
