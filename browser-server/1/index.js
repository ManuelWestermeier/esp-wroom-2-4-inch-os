// server.js
// Updated MWOSP test server - improved UI, navigation parsing, visited-sites actions.
// Usage: node server.js

const WebSocket = require('ws');
const http = require('http');

const PORT = process.env.PORT || 3000;

// Layout constants (match device implementation)
const SCREEN_W = 320;
const SCREEN_H = 240;
const TOPBAR_H = 20;
const CARD_Y = 22;
const CARD_H = 60;
const BUTTON_H = 36;
const BUTTON_PADDING = 10;
const VISIT_LIST_Y = 80;
const VISIT_ITEM_H = 30;

// Colors (16-bit values used by client)
const COLORS = {
    BG: 0,         // black
    PRIMARY: 31,   // dark blue
    TEXT: 65535,   // white
    ACCENT: 2016,  // green-ish
    ACCENT2: 63488,// red-ish
    ACCENT3: 31,   // reuse primary
    PRESSED: 1024, // different accent
    DANGER: 63488, // red
    PLACEHOLDER: 8421,
    ACCENT_TEXT: 65535
};

// store session, state, username, and per-client storage + meta
const clients = new Map();

const server = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('MWOSP Test Server Active\n');
});

const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
    console.log('Client connected');

    // Initialize client session
    const clientData = {
        state: "home",
        session: "",
        username: "",
        storage: {}, // per-domain key/value (visited sites etc)
        width: SCREEN_W,
        height: SCREEN_H
    };
    clients.set(ws, clientData);

    ws.on('message', (msg) => {
        const message = msg.toString().trim();
        if (!message) return;
        console.log('Received:', message);

        // --- Handshake: "MWOSP-v1 <session> <w> <h>" ---
        if (message.startsWith('MWOSP-v1')) {
            const parts = message.split(' ');
            if (parts.length >= 2) clientData.session = parts[1];
            if (parts.length >= 4) {
                clientData.width = parseInt(parts[2]) || SCREEN_W;
                clientData.height = parseInt(parts[3]) || SCREEN_H;
            }
            ws.send('MWOSP-v1 OK');
            renderUI(ws, clientData);
            return;
        }

        // Click coordinate: "Click x y"
        if (message.startsWith('Click')) {
            const parts = message.split(' ');
            const x = parseInt(parts[1]) || 0;
            const y = parseInt(parts[2]) || 0;
            // visual feedback
            ws.send(`FillRect ${x - 5} ${y - 5} 10 10 ${COLORS.ACCENT2}`);
            ws.send(`DrawCircle ${x} ${y} 8 ${COLORS.ACCENT}`);
            ws.send(`DrawText ${x - 10} ${y - 15} 2 ${COLORS.TEXT} Clicked!`);
            handleClick(ws, clientData, x, y);
            return;
        }

        // Navigation request from client (some clients may send a raw "Navigate <domain>@<state>"
        // or "Navigate <domain>:<port>@<state>" - parse both)
        if (message.startsWith('Navigate ')) {
            const navStr = message.substring(9).trim();
            // support "domain@state" or "domain:port@state" or just "home"/"settings"
            if (navStr === 'home' || navStr === 'settings' || navStr === 'search' || navStr === 'input') {
                clientData.state = navStr;
                renderUI(ws, clientData);
                return;
            }
            // parse domain & state
            const atIdx = navStr.indexOf('@');
            let domainPort = navStr, state = 'startpage';
            if (atIdx >= 0) {
                domainPort = navStr.substring(0, atIdx);
                state = navStr.substring(atIdx + 1) || state;
            }
            let domain = domainPort;
            let port = 443;
            const colon = domainPort.indexOf(':');
            if (colon >= 0) {
                domain = domainPort.substring(0, colon);
                port = parseInt(domainPort.substring(colon + 1)) || 443;
            }
            // set state to website/startpage for rendering
            clientData.state = state || 'website';
            clientData.currentDomain = domain;
            clientData.currentPort = port;
            // register visited site on server-side storage (timestamp)
            clientData.storage[domain] = String(Date.now());
            // send Title and some content draws
            ws.send(`Title ${domain}`);
            ws.send(`DrawText 10 30 2 ${COLORS.TEXT} Loading ${domain}...`);
            // simulate page content
            ws.send(`DrawText 10 60 1 ${COLORS.TEXT} This is a demo page for ${domain}.`);
            ws.send(`DrawSVG 140 100 40 40 ${COLORS.ACCENT} <svg width="40" height="40"><rect width="40" height="40" fill="red"/></svg>`);
            renderUI(ws, clientData);
            return;
        }

        // Session & state helpers
        if (message.startsWith('SetSession ')) {
            clientData.session = message.substring(11);
            return;
        }
        if (message.startsWith('GetSession ')) {
            const rid = message.split(' ')[1] || '';
            ws.send(`GetBackSession ${rid} ${clientData.session || ''}`);
            return;
        }
        if (message.startsWith('SetState ')) {
            clientData.state = message.substring(9);
            renderUI(ws, clientData);
            return;
        }
        if (message.startsWith('GetState ')) {
            const rid = message.split(' ')[1] || '';
            ws.send(`GetBackState ${rid} ${clientData.state}`);
            return;
        }

        // Clear settings
        if (message === 'ClearSettings') {
            clientData.session = "";
            clientData.state = "home";
            clientData.username = "";
            clientData.storage = {};
            clientData.currentDomain = undefined;
            ws.send("Navigate home");
            renderUI(ws, clientData);
            return;
        }

        // PromptText result from client: "GetBackText <rid> <text...>"
        if (message.startsWith('GetBackText ')) {
            const parts = message.split(' ');
            const rid = parts[1] || '';
            const text = parts.slice(2).join(' ') || '';
            if (rid === 'username') {
                clientData.username = text;
                // acknowledge and re-render
                renderUI(ws, clientData);
            } else if (rid === 'url') {
                // expected format: domain[:port][@state] or just domain
                const input = text.trim();
                if (input.length) {
                    // mirror device Navigate behavior and store visited
                    let domainPort = input;
                    let state = 'startpage';
                    const at = input.indexOf('@');
                    if (at >= 0) {
                        domainPort = input.substring(0, at);
                        state = input.substring(at + 1) || state;
                    }
                    let domain = domainPort;
                    let port = 443;
                    const c = domainPort.indexOf(':');
                    if (c >= 0) {
                        domain = domainPort.substring(0, c);
                        port = parseInt(domainPort.substring(c + 1)) || 443;
                    }
                    clientData.state = state;
                    clientData.currentDomain = domain;
                    clientData.currentPort = port;
                    clientData.storage[domain] = String(Date.now());
                    ws.send(`Title ${domain}`);
                    ws.send(`DrawText 10 60 1 ${COLORS.TEXT} Opened ${domain}`);
                    renderUI(ws, clientData);
                }
            } else if (rid === 'open_site_url') {
                // helper used when server asked user for an alternative URL to open
                const theUrl = text.trim();
                if (theUrl.length) {
                    let domainPort = theUrl;
                    let state = 'startpage';
                    const at = theUrl.indexOf('@');
                    if (at >= 0) {
                        domainPort = theUrl.substring(0, at);
                        state = theUrl.substring(at + 1) || state;
                    }
                    let domain = domainPort;
                    let port = 443;
                    const c = domainPort.indexOf(':');
                    if (c >= 0) {
                        domain = domainPort.substring(0, c);
                        port = parseInt(domainPort.substring(c + 1)) || 443;
                    }
                    clientData.state = state;
                    clientData.currentDomain = domain;
                    clientData.currentPort = port;
                    clientData.storage[domain] = String(Date.now());
                    ws.send(`Title ${domain}`);
                    ws.send(`DrawText 10 60 1 ${COLORS.TEXT} Opened ${domain}`);
                    renderUI(ws, clientData);
                }
            }
            return;
        }

        // DrawSVG example request from client
        if (message.startsWith('DrawSVG')) {
            const exampleSVG = `<svg width="50" height="50"><rect width="50" height="50" fill="red"/></svg>`;
            ws.send(`DrawSVG 50 150 50 50 ${COLORS.ACCENT} ${exampleSVG}`);
            return;
        }

        // Storage operations from client
        // Client may send "SetStorage <key> <val>" to ask server to persist something server-side,
        // or "GetStorage <key>" to request server-value.
        if (message.startsWith('SetStorage ')) {
            const idx = message.indexOf(' ', 11);
            if (idx >= 11) {
                const key = message.substring(11, idx);
                const val = message.substring(idx + 1);
                clientData.storage[key] = val;
            }
            return;
        }
        if (message.startsWith('GetStorage ')) {
            const key = message.substring(11);
            const val = clientData.storage[key] || "";
            ws.send(`GetBackStorage ${key} ${val}`);
            return;
        }

        // GetBackStorage from client (response to server's earlier GetStorage request)
        // Format: "GetBackStorage <key> <val...>"
        if (message.startsWith('GetBackStorage ')) {
            // store/echo it server-side
            const parts = message.split(' ');
            const key = parts[1] || '';
            const val = parts.slice(2).join(' ') || '';
            if (key) clientData.storage[key] = val;
            return;
        }

        // Exit request from client
        if (message.startsWith('Exit')) {
            try { ws.close(); } catch (e) { }
            return;
        }

        // Unknown messages: log
        console.log('Unhandled message:', message);
    });

    ws.on('close', () => {
        clients.delete(ws);
        console.log('Client disconnected');
    });
});

// ----- Helpers -----

function handleClick(ws, clientData, x, y) {
    // Top-bar close: x > 275 && y < TOPBAR_H
    if (y < TOPBAR_H && x > (SCREEN_W - 30)) {
        // close (in client, close returns to menu/home)
        ws.send('Exit');
        return;
    }

    // Top-left title tapped: go home
    if (y < TOPBAR_H && x < 120) {
        clientData.state = 'home';
        renderUI(ws, clientData);
        return;
    }

    // Buttons inside the floating card (three buttons)
    const cardX = BUTTON_PADDING;
    const cardY = CARD_Y;
    const cardW = SCREEN_W - cardX * 2;
    const btnW = Math.floor((cardW - BUTTON_PADDING * 4) / 3);
    const by = cardY + 6;
    if (y >= by && y <= by + BUTTON_H) {
        // which button?
        for (let i = 0; i < 3; ++i) {
            const bx = cardX + BUTTON_PADDING + i * (btnW + BUTTON_PADDING);
            if (x >= bx && x <= bx + btnW) {
                if (i === 0) {
                    // Search -> navigate to known search server
                    const domain = 'mw-search-server-onrender-app.onrender.com';
                    clientData.state = 'startpage';
                    clientData.currentDomain = domain;
                    clientData.currentPort = 443;
                    clientData.storage[domain] = String(Date.now());
                    ws.send(`Title ${domain}`);
                    ws.send(`DrawText 10 60 1 ${COLORS.TEXT} Opened search page`);
                    renderUI(ws, clientData);
                } else if (i === 1) {
                    // Input URL: ask device for URL
                    ws.send('PromptText url Which page do you want to visit?');
                } else if (i === 2) {
                    // Settings
                    clientData.state = 'settings';
                    renderUI(ws, clientData);
                }
                return;
            }
        }
    }

    // Clicks inside visited sites list region
    if (y >= VISIT_LIST_Y) {
        const keys = Object.keys(clientData.storage);
        const idx = Math.floor((y - VISIT_LIST_Y) / VISIT_ITEM_H);
        if (idx >= 0 && idx < keys.length) {
            const domain = keys[idx];
            // compute button X positions (mirrors client layout)
            const btnW = 56;
            const btnGap = 4;
            const xOpen = SCREEN_W - BUTTON_PADDING - btnW;
            const xClear = xOpen - btnGap - btnW;
            const xDelete = xClear - btnGap - btnW;

            // Delete
            if (x >= xDelete && x <= xDelete + btnW) {
                delete clientData.storage[domain];
                renderUI(ws, clientData);
                return;
            }
            // ClearData (set empty)
            if (x >= xClear && x <= xClear + btnW) {
                clientData.storage[domain] = "";
                renderUI(ws, clientData);
                return;
            }
            // Open
            if (x >= xOpen && x <= xOpen + btnW) {
                // Prompt user on device for alt URL or open directly
                // We'll open directly to the stored domain
                clientData.state = 'startpage';
                clientData.currentDomain = domain;
                clientData.currentPort = 443;
                ws.send(`Title ${domain}`);
                ws.send(`DrawText 10 60 1 ${COLORS.TEXT} Opening ${domain}`);
                renderUI(ws, clientData);
                return;
            }

            // Click on text area -> open directly
            const textAreaW = SCREEN_W - BUTTON_PADDING - (3 * (btnW + btnGap));
            if (x >= 0 && x <= textAreaW) {
                clientData.state = 'startpage';
                clientData.currentDomain = domain;
                clientData.currentPort = 443;
                ws.send(`Title ${domain}`);
                ws.send(`DrawText 10 60 1 ${COLORS.TEXT} Opening ${domain}`);
                renderUI(ws, clientData);
                return;
            }
        }
    }

    // Settings page: allow tap top-left area to go back
    if (clientData.state === 'settings' && y > TOPBAR_H && y < TOPBAR_H + 30 && x < 120) {
        clientData.state = 'home';
        renderUI(ws, clientData);
        return;
    }
}

function renderUI(ws, clientData) {
    // Clear screen with BG
    ws.send(`FillRect 0 0 ${SCREEN_W} ${SCREEN_H} ${COLORS.BG}`);

    // Top bar
    ws.send(`FillRect 0 0 ${SCREEN_W} ${TOPBAR_H} ${COLORS.PRIMARY}`);
    ws.send(`DrawText 6 3 2 ${COLORS.ACCENT_TEXT} MW-OS-Browser`);
    ws.send(`DrawText ${SCREEN_W - 22} 3 2 ${COLORS.DANGER} X`);

    // Home page
    if (!clientData.state || clientData.state === 'home') {
        // floating card
        const cardX = BUTTON_PADDING;
        const cardY = CARD_Y;
        const cardW = SCREEN_W - cardX * 2;
        const cardH = CARD_H;
        // outer card (primary)
        ws.send(`FillRect ${cardX} ${cardY} ${cardW} ${cardH} ${COLORS.PRIMARY}`);
        // inner floating area (bg)
        ws.send(`FillRect ${cardX + 2} ${cardY + 2} ${cardW - 4} ${cardH - 4} ${COLORS.BG}`);

        // draw three buttons
        const btnW = Math.floor((cardW - BUTTON_PADDING * 4) / 3);
        let bx = cardX + BUTTON_PADDING;
        const by = cardY + 6;

        // Button 0: Search
        ws.send(`FillRect ${bx} ${by} ${btnW} ${BUTTON_H} ${COLORS.ACCENT}`);
        ws.send(`DrawText ${bx + 8} ${by + 8} 2 ${COLORS.ACCENT_TEXT} Search`);

        // Button 1: Input URL
        bx += btnW + BUTTON_PADDING;
        ws.send(`FillRect ${bx} ${by} ${btnW} ${BUTTON_H} ${COLORS.ACCENT2}`);
        ws.send(`DrawText ${bx + 8} ${by + 8} 2 ${COLORS.ACCENT_TEXT} Input URL`);

        // Button 2: Settings
        bx += btnW + BUTTON_PADDING;
        ws.send(`FillRect ${bx} ${by} ${btnW} ${BUTTON_H} ${COLORS.ACCENT3}`);
        ws.send(`DrawText ${bx + 8} ${by + 8} 2 ${COLORS.ACCENT_TEXT} Settings`);

        // Visited sites header
        ws.send(`DrawText 10 ${VISIT_LIST_Y - 18} 2 ${COLORS.TEXT} Visited Sites`);

        // Show prompt or username
        if (clientData.username) {
            ws.send(`DrawText 10 160 2 ${COLORS.TEXT} Hello, ${clientData.username}!`);
        } else {
            ws.send(`DrawText 10 160 2 ${COLORS.PLACEHOLDER} Please enter your name.`);
        }

        // list sites
        const sites = Object.keys(clientData.storage);
        for (let i = 0; i < sites.length; ++i) {
            const y = VISIT_LIST_Y + i * VISIT_ITEM_H;
            const domain = sites[i];
            // alternating bg
            const bgColor = (i % 2 === 0) ? COLORS.PRIMARY : COLORS.BG;
            ws.send(`FillRect 0 ${y} ${SCREEN_W} ${VISIT_ITEM_H - 2} ${bgColor}`);
            ws.send(`DrawText 10 ${y + 6} 1 ${COLORS.TEXT} ${domain}`);

            // right-side buttons: Delete, Clear, Open
            const btnW = 56;
            const btnGap = 4;
            const xOpen = SCREEN_W - BUTTON_PADDING - btnW;
            const xClear = xOpen - btnGap - btnW;
            const xDelete = xClear - btnGap - btnW;

            ws.send(`FillRect ${xDelete} ${y + 4} ${btnW} ${VISIT_ITEM_H - 10} ${COLORS.DANGER}`);
            ws.send(`DrawText ${xDelete + 8} ${y + 6} 1 ${COLORS.ACCENT_TEXT} Delete`);

            ws.send(`FillRect ${xClear} ${y + 4} ${btnW} ${VISIT_ITEM_H - 10} ${COLORS.PRESSED}`);
            ws.send(`DrawText ${xClear + 6} ${y + 6} 1 ${COLORS.ACCENT_TEXT} Clear`);

            ws.send(`FillRect ${xOpen} ${y + 4} ${btnW} ${VISIT_ITEM_H - 10} ${COLORS.ACCENT}`);
            ws.send(`DrawText ${xOpen + 10} ${y + 6} 1 ${COLORS.ACCENT_TEXT} Open`);
        }
        return;
    }

    // Settings page
    if (clientData.state === 'settings') {
        ws.send(`DrawText 10 30 2 ${COLORS.TEXT} Visited Sites & Storage`);
        ws.send(`DrawText 10 56 1 ${COLORS.PLACEHOLDER} Tap a site to open. Use buttons to manage data.`);

        // list keys & values
        let y = 80;
        for (const key of Object.keys(clientData.storage)) {
            ws.send(`DrawText 10 ${y} 1 ${COLORS.TEXT} ${key}: ${clientData.storage[key]}`);
            y += 18;
            if (y > SCREEN_H - 30) break;
        }

        ws.send(`DrawText 10 ${SCREEN_H - 20} 1 ${COLORS.PLACEHOLDER} Press ClearSettings to reset.`);
        return;
    }

    // Search or website page (simple demo content)
    if (clientData.state === 'search' || clientData.state === 'startpage' || clientData.state === 'website') {
        const title = clientData.currentDomain || 'Website';
        // Top bar title override
        ws.send(`DrawText 6 3 2 ${COLORS.ACCENT_TEXT} ${title}`);
        ws.send(`DrawText ${SCREEN_W - 22} 3 2 ${COLORS.DANGER} X`);
        // content area
        ws.send(`FillRect 0 ${TOPBAR_H} ${SCREEN_W} ${SCREEN_H - TOPBAR_H} ${COLORS.BG}`);
        ws.send(`DrawText 10 ${TOPBAR_H + 6} 1 ${COLORS.TEXT} Welcome to ${title}.`);
        ws.send(`DrawText 10 ${TOPBAR_H + 24} 1 ${COLORS.TEXT} (Demo page rendered by test server)`);
        ws.send(`DrawSVG 140 ${TOPBAR_H + 40} 40 40 ${COLORS.ACCENT} <svg width="40" height="40"><circle cx="20" cy="20" r="20" fill="yellow"/></svg>`);
        return;
    }

    // fallback to home
    clientData.state = 'home';
    renderUI(ws, clientData);
}

server.listen(PORT, () => console.log(`Server listening on port ${PORT}`));
