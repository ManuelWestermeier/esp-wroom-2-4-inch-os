-- DuckDuckGo search app for your Lua/ESP32 window system
print("APP:STARTED")

local win = createWindow(20, 20, 240, 160)
WIN_setName(win, "Search")

-- pull style palette from C++ (lua_getTheme)
local theme = getTheme()

-- semantic color roles
local BG         = theme.bg
local WIN_BG     = theme.primary
local TITLE_BG   = theme.accent
local TITLE_FG   = theme.accentText
local BOX_BG     = theme.bg
local TEXT_FG    = theme.text
local RESULT_FG  = theme.text
local PLACEHOLDER= theme.placeholder
local BORDER     = theme.accent2
local ERROR_FG   = theme.danger

-- url encoding helper
local function url_encode(str)
    if (str == nil) then
        return ""
    end
    str = tostring(str)
    local out = {}
    for i = 1, #str do
        local c = str:sub(i, i)
        if c:match("[%w%-_%.~]") then
            table.insert(out, c)
        else
            table.insert(out, string.format("%%%02X", string.byte(c)))
        end
    end
    return table.concat(out)
end

-- crude JSON extraction: get AbstractText then any "Text" fields from RelatedTopics
local function extract_results_from_ddg(body, max_items)
    max_items = max_items or 8
    local results = {}

    -- try AbstractText first
    local abstract = body:match('"AbstractText"%s*:%s*"(.-)"')
    if abstract and #abstract > 0 then
        abstract = abstract:gsub('\\n', '\n'):gsub('\\"', '"'):gsub("\\/", "/")
        table.insert(results, abstract)
    end

    -- gather "Text" entries (many RelatedTopics entries contain "Text")
    for text in body:gmatch('"Text"%s*:%s*"(.-)"') do
        text = text:gsub('\\n', '\n'):gsub('\\"', '"'):gsub("\\/", "/")
        if #text > 0 then
            table.insert(results, text)
            if #results >= max_items then
                break
            end
        end
    end

    -- fallback: short raw body if nothing found
    if #results == 0 then
        local s = body:sub(1, 240)
        s = s:gsub('%s+', ' ')
        table.insert(results, s .. (#body > 240 and "..." or ""))
    end

    return results
end

-- UI renderer
local function draw_ui(query, results)
    -- full background
    WIN_fillRect(win, 1, 0, 0, 240, 160, BG)

    -- main card
    WIN_fillRect(win, 1, 8, 8, 224, 144, WIN_BG)
    WIN_drawRect(win, 1, 8, 8, 224, 144, BORDER)

    -- title bar
    WIN_fillRect(win, 1, 8, 8, 224, 26, TITLE_BG)
    WIN_writeText(win, 1, 16, 14, "DuckDuckGo Search", 2, TITLE_FG)

    -- search box label and box
    WIN_writeText(win, 1, 16, 40, "Search:", 1, TEXT_FG)
    WIN_fillRect(win, 1, 64, 36, 152, 22, BOX_BG)
    WIN_drawRect(win, 1, 64, 36, 152, 22, BORDER)

    if query and #query > 0 then
        WIN_writeText(win, 1, 68, 40, query, 1, RESULT_FG)
    else
        WIN_writeText(win, 1, 68, 40, "(click box to type a query)", 1, PLACEHOLDER)
    end

    -- results header
    WIN_writeText(win, 1, 16, 64, "Results:", 1, TEXT_FG)

    -- results area
    local y = 78
    local line_h = 10
    if results then
        for i = 1, #results do
            local text = results[i]
            local color = RESULT_FG
            if text:match("^%u") and (text:find("error") or text:find("Error") or text:find("HTTP")) then
                color = ERROR_FG
            end
            -- naive wrap: split by words so it fits approx into 30 chars per line
            local maxchars = 30
            local start = 1
            while start <= #text and y + line_h <= 8 + 144 do
                local sub = text:sub(start, start + maxchars - 1)
                local cut = sub:match("^(.*)%s") or sub
                if #cut < #sub and #cut > 0 then
                    sub = cut
                end
                WIN_writeText(win, 1, 16, y, sub, 1, color)
                start = start + #sub
                if text:sub(start, start) == " " then
                    start = start + 1
                end
                y = y + line_h
            end
            y = y + 4
            if y > 8 + 144 then break end
        end
    end
end

-- search function
local function perform_search(query)
    if not query or query == "" then
        return {"(empty query)"}
    end
    local q = url_encode(query)
    local url = "https://api.duckduckgo.com/?q=" .. q .. "&format=json&no_html=1&skip_disambig=1"
    local res = httpsReq {
        method = "GET",
        url = url
    }
    if not res or not res.status then
        return {"Network error"}
    end
    if res.status ~= 200 then
        return {"HTTP error: " .. tostring(res.status)}
    end
    return extract_results_from_ddg(res.body, 8)
end

-- main loop
local query = ""
local results = nil

draw_ui(query, results)

while true do
    local ok, out = WIN_readText(win, "Search DuckDuckGo:", query)
    if ok then
        query = out or ""
        draw_ui(query, {"Searching..."})
        results = perform_search(query)
        draw_ui(query, results)
    else
        draw_ui(query, results)
    end
    delay(100)
end

print("APP:EXITED")
