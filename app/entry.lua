-- entry.lua (Search - fixed)
local win = createWindow(10, 10, 150, 150)
WIN_setName(win, "DDG Search")
local function urlencode(s)
    if not s then return "" end
    s = tostring(s)
    s = s:gsub("\n"," "):gsub(" ","+")
    s = s:gsub("([^%w%+%-_%.~])", function(c) return string.format("%%%02X", string.byte(c)) end)
    return s
end
local function int(n) return math.floor(tonumber(n) or 0) end
local lastText = "Tap to search (click)"
local function doSearch(q)
    local url = "https://api.duckduckgo.com/?q="..urlencode(q).."&format=json&no_html=1&skip_disambig=1"
    local res = httpsReq{ method="GET", url=url }
    if not res or not res.body then lastText = "Network error"; return end
    local body = res.body
    local abstract = body:match('"AbstractText"%s*:%s*"(.-)"') or ""
    if abstract ~= "" then lastText = abstract; return end
    local rel = body:match('"Text"%s*:%s*"(.-)"') or ""
    if rel ~= "" then lastText = rel; return end
    lastText = "No quick answer."
end

while not WIN_closed(win) do
    local pressed, state, x, y, mx, my, wc, nr = WIN_getLastEvent(win, 1)
    if nr or state > 0 then
        WIN_finishFrame(win)
        WIN_fillBg(win, 1, 0xFFFF)
        WIN_writeText(win, 1, 6, 6, "DuckDuckGo Search", 2, 0x001F)
        WIN_drawRect(win, 1, int(6), int(26), int(138), int(12), 0x0000)
        WIN_writeText(win, 1, 8, 28, lastText:sub(1,120), 1, 0x0000)
        if state > 0 then
            local ok, q = WIN_readText(win, "Search query:", "")
            if ok and q and q:match("%S") then
                lastText = "Searching..."
                doSearch(q)
            end
        end
    end
    delay(60)
end