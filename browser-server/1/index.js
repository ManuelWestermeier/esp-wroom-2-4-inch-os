// server.js
// Updated MWOSP test server - with theme color support and viewport handling
// Usage: node server.js

const WebSocket = require('ws');
const http = require('http');

const PORT = process.env.PORT || 3000;

// Layout constants (match device implementation)
const SCREEN_W = 320;
const SCREEN_H = 240;
const TOPBAR_H = 20;
const VIEWPORT_Y = TOPBAR_H;
const VIEWPORT_H = SCREEN_H - TOPBAR_H;
const CARD_Y = 22;
const CARD_H = 60;
const BUTTON_H = 36;
const BUTTON_PADDING = 10;
const VISIT_LIST_Y = 80;
const VISIT_ITEM_H = 30;

// Default theme colors (16-bit values used by client)
let THEME_COLORS = {
    bg: 0,
    text: 65535,
    primary: 31,
    accent: 2016,
    accent2: 63488,
    accent3: 31,
    accentText: 65535,
    pressed: 1024,
    danger: 63488,
    placeholder: 8421
};

// Per-client session/state
const clients = new Map();

const server = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('MWOSP Test Server Active\n');
});

const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
    console.log('Client connected');

    const clientData = {
        state: 'home',
        session: '',
        username: '',
        storage: {},
        width: SCREEN_W,
        height: SCREEN_H,
        theme: { ...THEME_COLORS },
        viewport: {
            x: 0,
            y: VIEWPORT_Y,
            w: SCREEN_W,
            h: VIEWPORT_H
        }
    };

    clients.set(ws, clientData);

    ws.on('message', (msg) => {
        const message = msg.toString().trim();
        if (!message) return;

        console.log('Received:', message);

        // --- Handshake ---
        if (message.startsWith('MWOSP-v1')) {
            const p = message.split(' ');
            clientData.session = p[1] || '';
            clientData.width = parseInt(p[2]) || SCREEN_W;
            clientData.height = parseInt(p[3]) || SCREEN_H;
            ws.send('MWOSP-v1 OK');
            return;
        }

        // --- Theme colors ---
        if (message.startsWith('ThemeColors')) {
            const parts = message.split(' ');
            for (let i = 1; i < parts.length; i++) {
                const [k, v] = parts[i].split(':');
                if (!k || !v) continue;
                const n = parseInt(v, 16);
                if (!isNaN(n)) clientData.theme[k] = n;
            }
            renderUI(ws, clientData);
            return;
        }

        if (message.startsWith('GetThemeColor ')) {
            const key = message.substring(14);
            const val = clientData.theme[key] ?? THEME_COLORS[key] ?? 0;
            ws.send(`ThemeColor ${key} ${val.toString(16).toUpperCase()}`);
            return;
        }

        if (message.startsWith('SetThemeColor ')) {
            const p = message.substring(14).split(' ');
            if (p.length >= 2) {
                const key = p[0];
                let v = p[1];
                if (v.startsWith('0x')) v = v.slice(2);
                const n = parseInt(v, 16);
                if (!isNaN(n)) {
                    clientData.theme[key] = n;
                    if (clientData.state === 'home' || clientData.state === 'settings') {
                        renderUI(ws, clientData);
                    }
                }
            }
            return;
        }

        // --- Click ---
        if (message.startsWith('Click')) {
            const p = message.split(' ');
            const x = parseInt(p[1]) || 0;
            const y = parseInt(p[2]) || 0;

            ws.send(`FillRect ${x - 5} ${y - 5} 10 10 ${clientData.theme.accent2}`);
            ws.send(`DrawCircle ${x} ${y} 8 ${clientData.theme.accent}`);
            handleClick(ws, clientData, x, y);
            return;
        }

        // --- Navigation ---
        if (message.startsWith('Navigate ')) {
            const nav = message.substring(9).trim();
            if (['home', 'settings', 'search', 'input'].includes(nav)) {
                clientData.state = nav;
                renderUI(ws, clientData);
                return;
            }

            let domain = nav;
            let port = 443;
            let state = 'startpage';

            const at = nav.indexOf('@');
            if (at >= 0) {
                domain = nav.slice(0, at);
                state = nav.slice(at + 1) || state;
            }

            const c = domain.indexOf(':');
            if (c >= 0) {
                port = parseInt(domain.slice(c + 1)) || 443;
                domain = domain.slice(0, c);
            }

            clientData.state = state;
            clientData.currentDomain = domain;
            clientData.currentPort = port;
            clientData.storage[domain] = String(Date.now());

            ws.send(`Title ${domain}`);
            ws.send(`FillRect 0 ${VIEWPORT_Y} ${SCREEN_W} ${VIEWPORT_H} ${clientData.theme.bg}`);
            ws.send(`DrawText 10 ${VIEWPORT_Y + 10} 1 ${clientData.theme.text} Loading ${domain}...`);

            renderUI(ws, clientData);
            return;
        }

        // --- Text input ---
        if (message.startsWith('GetBackText ')) {
            const parts = message.split(' ');
            const rid = parts[1];
            const text = parts.slice(2).join(' ');

            if (rid === 'username') {
                clientData.username = text;
                renderUI(ws, clientData);
            }

            if (rid === 'url' || rid === 'open_site_url') {
                if (!text) return;
                ws.send(`Navigate ${text}`);
            }
            return;
        }

        // --- Storage ---
        if (message.startsWith('SetStorage ')) {
            const i = message.indexOf(' ', 11);
            if (i > 0) {
                const k = message.slice(11, i);
                const v = message.slice(i + 1);
                clientData.storage[k] = v;
            }
            return;
        }

        if (message.startsWith('GetStorage ')) {
            const k = message.slice(11);
            ws.send(`GetBackStorage ${k} ${clientData.storage[k] || ''}`);
            return;
        }

        if (message === 'ClearSettings') {
            clientData.state = 'home';
            clientData.session = '';
            clientData.username = '';
            clientData.storage = {};
            clientData.theme = { ...THEME_COLORS };
            renderUI(ws, clientData);
            return;
        }

        if (message.startsWith('Exit')) {
            ws.close();
            return;
        }

        console.log('Unhandled:', message);
    });

    ws.on('close', () => {
        clients.delete(ws);
        console.log('Client disconnected');
    });
});

// ---------------- helpers ----------------

function handleClick(ws, c, x, y) {
    if (y < TOPBAR_H && x > SCREEN_W - 30) {
        ws.send('Exit');
        return;
    }

    if (y < TOPBAR_H && x < 120) {
        c.state = 'home';
        renderUI(ws, c);
        return;
    }

    const by = CARD_Y + 6;
    if (y >= by && y <= by + BUTTON_H) {
        const cardW = SCREEN_W - BUTTON_PADDING * 2;
        const btnW = Math.floor((cardW - BUTTON_PADDING * 4) / 3);
        for (let i = 0; i < 3; i++) {
            const bx = BUTTON_PADDING + BUTTON_PADDING + i * (btnW + BUTTON_PADDING);
            if (x >= bx && x <= bx + btnW) {
                if (i === 0) ws.send('Navigate mw-search-server-onrender-app.onrender.com');
                if (i === 1) ws.send('PromptText url Which page do you want to visit?');
                if (i === 2) {
                    c.state = 'settings';
                    renderUI(ws, c);
                }
                return;
            }
        }
    }
}

function renderUI(ws, c) {
    const t = c.theme;

    ws.send(`FillRect 0 0 ${SCREEN_W} ${SCREEN_H} ${t.bg}`);
    ws.send(`FillRect 0 0 ${SCREEN_W} ${TOPBAR_H} ${t.primary}`);
    ws.send(`DrawText 6 3 2 ${t.accentText} MW-OS-Browser`);
    ws.send(`DrawText ${SCREEN_W - 22} 3 2 ${t.danger} X`);

    if (c.state === 'home') {
        ws.send(`DrawText 10 160 2 ${t.text} ${c.username ? 'Hello, ' + c.username : 'Please enter your name.'}`);
        return;
    }

    if (c.state === 'settings') {
        ws.send(`DrawText 10 30 2 ${t.text} Settings`);
        return;
    }

    ws.send(`DrawText 10 ${VIEWPORT_Y + 10} 1 ${t.text} Demo content`);
}

server.listen(PORT, () => {
    console.log(`Server listening on port ${PORT}`);
});
