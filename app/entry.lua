-- DuckDuckGo minimal search app (clean UI, click + resize, single button with SVG)
-- Requirements: createWindow, WIN_*, getTheme, httpsReq, delay, url encoding available below.
print("APP:STARTED")

-- create window
local win = createWindow(20, 20, 240, 160)
WIN_setName(win, "Search")

-- pull theme from C++
local theme = getTheme() or {}
local BG = theme.bg or 0x0000
local WIN_BG = theme.primary or 0xFFFF
local TITLE_BG = theme.accent or 0x7BEF
local TITLE_FG = theme.accentText or 0x0000
local BOX_BG = theme.bg or 0xFFFF
local TEXT_FG = theme.text or 0x0000
local RESULT_FG = theme.text or 0x0000
local PLACEHOLDER = theme.placeholder or 0x7BEF
local BORDER = theme.accent2 or 0x4208
local ERROR_FG = theme.danger or 0xF800

-- simple URL encode
local function url_encode(str)
    if not str then
        return ""
    end
    str = tostring(str)
    local out = {}
    for i = 1, #str do
        local c = str:sub(i, i)
        if c:match("[%w%-_%.~]") then
            out[#out + 1] = c
        else
            out[#out + 1] = string.format("%%%02X", string.byte(c))
        end
    end
    return table.concat(out)
end

-- crude JSON extract: AbstractText first, then "Text" fields from RelatedTopics
local function extract_results_from_ddg(body, max_items)
    max_items = max_items or 8
    local results = {}

    -- AbstractText
    local abstract = body:match('"AbstractText"%s*:%s*"(.-)"')
    if abstract and #abstract > 0 then
        abstract = abstract:gsub('\\n', '\n'):gsub('\\"', '"'):gsub("\\/", "/")
        table.insert(results, abstract)
    end

    -- "Text" fields
    for text in body:gmatch('"Text"%s*:%s*"(.-)"') do
        text = text:gsub('\\n', '\n'):gsub('\\"', '"'):gsub("\\/', \"/")
        if #text > 0 then
            table.insert(results, text)
            if #results >= max_items then
                break
            end
        end
    end

    -- fallback: short body snippet
    if #results == 0 then
        local s = (body or ""):sub(1, 240):gsub('%s+', ' ')
        table.insert(results, s .. ((#body or 0) > 240 and "..." or ""))
    end

    return results
end

-- perform HTTP GET to DuckDuckGo with fixed headers to avoid 202
local function perform_search(query)
    if not query or query == "" then
        return {"(empty query)"}
    end

    local q = url_encode(query)
    local url = "https://corsproxy.io/?url=https://api.duckduckgo.com/?q=" .. q ..
                    "&format=json&no_html=1&skip_disambig=1"

    local res = httpsReq {
        method = "GET",
        url = url,
        headers = {
            ["User-Agent"] = "ESP32Search/1.0",
            ["Accept"] = "application/json"
        },
        timeout = 8000
    }

    if not res or not res.status then
        return {"Network error"}
    end
    if res.status ~= 200 then
        return {"HTTP error: " .. tostring(res.status)}
    end

    return extract_results_from_ddg(res.body or "", 8)
end

-- minimal magnifier SVG (simple circle+handle path) as string
local MAG_SVG = [[
<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
  <circle cx="11" cy="11" r="6" stroke="black" stroke-width="2" fill="none"/>
  <line x1="16.5" y1="16.5" x2="21" y2="21" stroke="black" stroke-width="2" stroke-linecap="round"/>
</svg>
]]

-- keep last rect to detect resize
local lastRect = {
    x = 0,
    y = 0,
    w = 0,
    h = 0
}

-- UI state
local query = ""
local results = nil
local dirty = true -- force initial draw
local is_searching = false

-- simple layout values (no excessive separate paddings)
local function layout_for(rect)
    -- rect: {x,y,w,h}
    local title_h = 26
    local gap = 6
    local search_h = 22
    local btn_w = 34
    local content_y = title_h + gap + search_h + gap
    return {
        title_h = title_h,
        gap = gap,
        search_h = search_h,
        btn_w = btn_w,
        content_y = content_y,
        content_h = rect.h - content_y - gap
    }
end

-- draw UI (only call when dirty)
local function draw_ui()
    -- query window rect
    local x, y, w, h = WIN_getRect(win)
    local rect = {
        x = x,
        y = y,
        w = w,
        h = h
    }
    lastRect = rect

    local L = layout_for(rect)

    -- solid window background (cover full window)
    -- WIN_fillBg exists and uses screenId & color; using screenId 1 for left sprite
    pcall(function()
        WIN_fillBg(win, 1, BG)
    end)

    -- main card area (slightly inset)
    local card_x, card_y, card_w, card_h = 4, 4, w - 8, h - 8
    WIN_fillRect(win, 1, card_x, card_y, card_w, card_h, WIN_BG)
    WIN_drawRect(win, 1, card_x, card_y, card_w, card_h, BORDER)

    -- title bar
    WIN_fillRect(win, 1, card_x, card_y, card_w, L.title_h, TITLE_BG)
    WIN_writeText(win, 1, card_x + 8, card_y + 6, "DuckDuckGo", 2, TITLE_FG)

    -- search box (left) and button (right)
    local sb_x = card_x + 8
    local sb_y = card_y + L.title_h + L.gap
    local sb_w = card_w - 8 - L.btn_w
    WIN_fillRect(win, 1, sb_x, sb_y, sb_w, L.search_h, BOX_BG)
    WIN_drawRect(win, 1, sb_x, sb_y, sb_w, L.search_h, BORDER)

    -- text in search box
    if query == "" then
        WIN_writeText(win, 1, sb_x + 6, sb_y + 6, "(click to type a query)", 1, PLACEHOLDER)
    else
        WIN_writeText(win, 1, sb_x + 6, sb_y + 6, query, 1, TEXT_FG)
    end

    -- search button
    local btn_x = sb_x + sb_w + 4
    local btn_y = sb_y
    WIN_fillRect(win, 1, btn_x, btn_y, L.btn_w, L.search_h, TITLE_BG)
    WIN_drawRect(win, 1, btn_x, btn_y, L.btn_w, L.search_h, BORDER)
    WIN_writeText(win, 1, btn_x + 6, btn_y + 5, "Search", 1, TITLE_FG) -- text label
    -- draw small SVG icon left of text (try best-effort; color & steps chosen)
    pcall(function()
        WIN_drawSVG(win, 1, MAG_SVG, btn_x + 2, btn_y + 2, 18, 18, TITLE_FG, 6)
    end)

    -- results header
    local content_x = card_x + 8
    local content_y = card_y + L.content_y
    WIN_writeText(win, 1, content_x, content_y - 12, "Results:", 1, TEXT_FG)

    -- results area: naive wrapping and limited lines to fit
    if results == nil then
        -- nothing yet
        WIN_writeText(win, 1, content_x, content_y, "(no results)", 1, PLACEHOLDER)
    else
        local yptr = content_y
        local max_h = card_y + card_h - 8
        local line_h = 10
        local maxchars = 36
        for i = 1, #results do
            local text = tostring(results[i])
            -- color selection
            local color = RESULT_FG
            if text:lower():find("error") or text:lower():find("http") then
                color = ERROR_FG
            end
            -- wrap by words approx
            local start = 1
            while start <= #text and (yptr + line_h) <= max_h do
                local sub = text:sub(start, start + maxchars - 1)
                local cut = sub:match("^(.*)%s") or sub
                if #cut < #sub and #cut > 0 then
                    sub = cut
                end
                WIN_writeText(win, 1, content_x, yptr, sub, 1, color)
                start = start + #sub
                if text:sub(start, start) == " " then
                    start = start + 1
                end
                yptr = yptr + line_h
            end
            yptr = yptr + 4
            if yptr > max_h then
                break
            end
        end
    end

    dirty = false
end

-- helper: detect if a point is inside a rect
local function inside(px, py, rx, ry, rw, rh)
    return px >= rx and py >= ry and px < rx + rw and py < ry + rh
end

-- check button hit given current rect
local function button_rect()
    local x, y, w, h = WIN_getRect(win)
    local card_x, card_y, card_w = 4, 4, w - 8
    local L = layout_for({
        w = w,
        h = h
    })
    local sb_x = card_x + 8
    local sb_y = card_y + L.title_h + L.gap
    local sb_w = card_w - 8 - L.btn_w
    local btn_x = sb_x + sb_w + 4
    local btn_y = sb_y
    return btn_x, btn_y, L.btn_w, L.search_h
end

-- perform search in a coroutine-like non-blocking way (still synchronous on ESP, but we keep flags)
local function do_search(q)
    is_searching = true
    results = {"Searching..."}
    dirty = true
    -- draw immediate searching indicator
    draw_ui()

    local ok, res = pcall(perform_search, q)
    if ok then
        results = res
    else
        results = {"Search failed: " .. tostring(res)}
    end
    is_searching = false
    dirty = true
end

-- main loop: only redraw when dirty; handle clicks, input, resize
draw_ui()

while true do
    -- detect resize
    local x, y, w, h = WIN_getRect(win)
    if w ~= lastRect.w or h ~= lastRect.h then
        dirty = true
    end

    -- try to read text input: WIN_readText returns ok,out when clicked + typed
    local ok, out = WIN_readText(win, "Search DuckDuckGo:", query)
    if ok then
        -- user typed a value and confirmed; trigger search
        query = out or ""
        do_search(query)
    else
        -- no text input; check click events for button manually
        -- WIN_getLastEvent returns (pressed_bool, state, x, y, moveX, moveY, wasClicked?) but its return count is slightly inconsistent;
        -- we only need first three values (pressed flag and coordinates)
        local ev_ok, ppressed, estate, ex, ey = pcall(function()
            return WIN_getLastEvent(win, 1)
        end)

        if ev_ok and ppressed then
            -- if we got a click (pressed true) and coordinates available
            -- if inside button, trigger search
            local bx, by, bw, bh = button_rect()
            if type(ex) == "number" and type(ey) == "number" and inside(ex, ey, bx, by, bw, bh) then
                -- button clicked
                if not is_searching then
                    do_search(query)
                end
            else
                -- if click inside search box area, WIN_readText will handle it next loop; we mark dirty so placeholder/visuals update
                dirty = true
            end
        end
    end

    if dirty then
        draw_ui()
    end

    delay(80)
end

print("APP:EXITED")
