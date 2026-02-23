-- entry.lua (Calculator - fixed)
local win = createWindow(10, 10, 150, 150)
WIN_setName(win, "Calc")
local last = "Tap to enter expression"
local function int(n) return math.floor(tonumber(n) or 0) end

-- Tokenize
local function tokenize(s)
    local t = {}
    local i=1
    while i<=#s do
        local c = s:sub(i,i)
        if c:match("%s") then i=i+1
        elseif c:match("[%d%.]") then
            local j=i
            while j<=#s and s:sub(j,j):match("[%d%.]") do j=j+1 end
            t[#t+1]=s:sub(i,j-1); i=j
        else
            t[#t+1]=c; i=i+1
        end
    end
    -- handle unary minus
    local out = {}
    for k,v in ipairs(t) do
        if v == "-" then
            local prev = t[k-1]
            if not prev or prev=="(" or prev=="+" or prev=="-" or prev=="*" or prev=="/" or prev=="%" or prev=="^" then
                out[#out+1] = "u-"
            else out[#out+1]=v end
        else out[#out+1]=v end
    end
    return out
end

-- Shunting-yard -> RPN
local prec = { ["u-"]=4, ["^"]=3, ["*"]=2, ["/"]=2, ["%"]=2, ["+"]=1, ["-"]=1 }
local right_assoc = { ["^"]=true, ["u-"]=true }
local function to_rpn(tokens)
    local out, ops = {}, {}
    for _,tok in ipairs(tokens) do
        if tonumber(tok) then out[#out+1]=tok
        elseif tok=="(" then ops[#ops+1]=tok
        elseif tok==")" then
            while #ops>0 and ops[#ops]~="(" do out[#out+1]=table.remove(ops) end
            if ops[#ops]=="(" then table.remove(ops) end
        else -- operator
            while #ops>0 do
                local top = ops[#ops]
                if top~="(" and ((not right_assoc[tok] and (prec[top] or 0) >= (prec[tok] or 0)) or (right_assoc[tok] and (prec[top] or 0) > (prec[tok] or 0))) then
                    out[#out+1]=table.remove(ops)
                else break end
            end
            ops[#ops+1]=tok
        end
    end
    while #ops>0 do out[#out+1]=table.remove(ops) end
    return out
end

-- Evaluate RPN
local function eval_rpn(rpn)
    local st = {}
    for _,tok in ipairs(rpn) do
        if tonumber(tok) then st[#st+1]=tonumber(tok)
        elseif tok=="u-" then
            local a = table.remove(st); if not a then return nil,"syntax" end
            st[#st+1] = -a
        else
            local b = table.remove(st); local a = table.remove(st)
            if not a or not b then return nil,"syntax" end
            if tok=="+" then st[#st+1]=a+b
            elseif tok=="-" then st[#st+1]=a-b
            elseif tok=="*" then st[#st+1]=a*b
            elseif tok=="/" then if b==0 then return nil,"div0" end; st[#st+1]=a/b
            elseif tok=="%" then st[#st+1]=a%b
            elseif tok=="^" then st[#st+1]=a^b
            else return nil,"op" end
        end
    end
    return st[1]
end

local function safe_eval(expr)
    if not expr or expr:match("^%s*$") then return nil, "empty" end
    expr = expr:gsub("%s+","")
    if not expr:match("^[0-9%.%+%-%*/%%%^(%)]+$") then return nil, "invalid chars" end
    local ok, r = pcall(function()
        local tk = tokenize(expr)
        local rpn = to_rpn(tk)
        return eval_rpn(rpn)
    end)
    if not ok then return nil, "parse error" end
    if r==nil then return nil, "error" end
    return r, nil
end

while not WIN_closed(win) do
    local pressed, state, x, y, mx, my, wc, nr = WIN_getLastEvent(win, 1)
    if nr or state>0 then
        WIN_finishFrame(win)
        WIN_fillBg(win,1,0xFFFF)
        WIN_writeText(win,1,10,8,"Calculator",2,0x0000)
        WIN_writeText(win,1,8,32,last,1,0x001F)
        WIN_drawRect(win,1,int(10),int(60),int(130),int(70),0x0000)
        if state>0 then
            local ok, expr = WIN_readText(win, "Expression (e.g. 2*(3+4))", "")
            if ok then
                local res, err = safe_eval(expr)
                if err then last = "Error: "..tostring(err) else last = tostring(res) end
            end
        end
    end
    delay(50)
end