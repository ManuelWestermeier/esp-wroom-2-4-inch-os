const WebSocket = require('ws');
const http = require('http');

const PORT = process.env.PORT || 3000;

// store session, state, username, and per-client storage
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
        storage: {} // per-domain key/value
    };
    clients.set(ws, clientData);

    ws.on('message', (msg) => {
        const message = msg.toString();
        console.log('Received:', message);

        // Initial handshake
        if (message.startsWith('MWOSP-v1')) {
            ws.send('MWOSP-v1 OK');
            renderUI(ws, clientData);
        }

        // User clicks
        if (message.startsWith('Click')) {
            const [_, x, y] = message.split(' ').map(Number);
            ws.send(`FillRect ${x - 5} ${y - 5} 10 10 63488`);
            ws.send(`DrawCircle ${x} ${y} 8 2016`);
            ws.send(`DrawText ${x - 10} ${y - 15} 2 65535 Clicked!`);
            handleClick(ws, x, y);
        }

        // Navigation
        if (message.startsWith('Navigate')) {
            clientData.state = message.substring(9);
            ws.send(`Navigate ${clientData.state}`);
            renderUI(ws, clientData);
        }

        // Session & state
        if (message.startsWith('SetSession')) clientData.session = message.substring(11);
        if (message.startsWith('GetSession')) {
            const rid = message.split(' ')[1];
            ws.send(`GetBackSession ${rid} ${clientData.session}`);
        }

        if (message.startsWith('SetState')) clientData.state = message.substring(9);
        if (message.startsWith('GetState')) {
            const rid = message.split(' ')[1];
            ws.send(`GetBackState ${rid} ${clientData.state}`);
        }

        // Clear settings
        if (message.startsWith('ClearSettings')) {
            clientData.session = "";
            clientData.state = "home";
            clientData.username = "";
            clientData.storage = {};
            ws.send("Navigate home");
            renderUI(ws, clientData);
        }

        // PromptText results (for username or other inputs)
        if (message.startsWith('GetBackText')) {
            const parts = message.split(' ');
            const rid = parts[1];
            const text = parts.slice(2).join(' ');
            if (rid === 'username') {
                clientData.username = text;
                renderUI(ws, clientData);
            }
        }

        // Exit
        if (message.startsWith('Exit')) ws.close();

        // DrawSVG example
        if (message.startsWith('DrawSVG')) {
            const exampleSVG = `<svg width="50" height="50"><rect width="50" height="50" fill="red"/></svg>`;
            ws.send(`DrawSVG 50 150 50 50 63488 ${exampleSVG}`);
        }

        // Storage (set/get)
        if (message.startsWith('SetStorage')) {
            const idx = message.indexOf(' ', 11);
            const key = message.substring(11, idx);
            const val = message.substring(idx + 1);
            clientData.storage[key] = val;
        }

        if (message.startsWith('GetStorage')) {
            const key = message.substring(11);
            const val = clientData.storage[key] || "";
            ws.send(`GetBackStorage ${key} ${val}`);
        }
    });

    ws.on('close', () => {
        clients.delete(ws);
        console.log('Client disconnected');
    });
});

// ------------------- Helpers -------------------

function handleClick(ws, x, y) {
    const clientData = clients.get(ws);

    // Top buttons on home page
    if (clientData.state === 'home') {
        // Settings button
        if (x >= 10 && x <= 150 && y >= 50 && y <= 90) {
            clientData.state = 'settings';
            renderUI(ws, clientData);
        }
        // OS-Search-Page button
        else if (x >= 160 && x <= 310 && y >= 50 && y <= 90) {
            clientData.state = 'search';
            renderUI(ws, clientData);
        }
        // Input Page button (enter username)
        else if (x >= 10 && x <= 300 && y >= 100 && y <= 140) {
            ws.send('PromptText username Enter your name:');
        }
    }
}

// Render UI depending on state and username
function renderUI(ws, clientData) {
    // Clear screen
    ws.send("FillRect 0 0 320 480 0");

    // Top bar
    ws.send("FillRect 0 0 320 20 31"); // blue
    ws.send(`DrawText 5 3 1 65535 ${clientData.state}`);

    // Home page
    if (clientData.state === 'home') {
        // Buttons
        ws.send("FillRect 10 50 140 40 2016"); // Settings
        ws.send("DrawText 20 60 2 65535 Settings");

        ws.send("FillRect 160 50 140 40 2016"); // OS-Search
        ws.send("DrawText 170 60 2 65535 OS-Search-Page");

        ws.send("FillRect 10 100 290 40 63488"); // Input page
        ws.send("DrawText 20 110 2 65535 Input Page");

        if (clientData.username) {
            ws.send(`DrawText 10 160 2 65535 Hello, ${clientData.username}!`);
        } else {
            ws.send("DrawText 10 160 2 65535 Please enter your name.");
        }
    }

    // Settings page: show storage keys
    if (clientData.state === 'settings') {
        ws.send("DrawText 10 30 2 65535 Settings Page:");
        let y = 60;
        for (const key in clientData.storage) {
            ws.send(`DrawText 10 ${y} 2 65535 ${key}: ${clientData.storage[key]}`);
            y += 20;
        }
        ws.send("DrawText 10 400 2 65535 Press ClearSettings to reset.");
    }

    // OS-Search page
    if (clientData.state === 'search') {
        ws.send("DrawText 10 50 2 65535 Welcome to OS-Search-Page!");
        ws.send("DrawCircle 160 150 40 63488");
        const svg = `<svg width="40" height="40"><circle cx="20" cy="20" r="20" fill="yellow"/></svg>`;
        ws.send(`DrawSVG 140 220 40 40 2016 ${svg}`);
    }

    // Input page: ask username if empty
    if (clientData.state === 'input' && !clientData.username) {
        ws.send('PromptText username Enter your name:');
    }
}

server.listen(PORT, () => console.log(`Server listening on port ${PORT}`));
