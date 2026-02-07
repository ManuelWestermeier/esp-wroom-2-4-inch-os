const WebSocket = require('ws');
const http = require('http');

const PORT = process.env.PORT || 3000;

const clients = new Map(); // store session/state per client

const server = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('MWOSP Test Server Active\n');
});

const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
    console.log('Client connected');

    // initialize client session
    const clientData = { state: "startpage", session: "" };
    clients.set(ws, clientData);

    ws.on('message', (msg) => {
        const message = msg.toString();
        console.log('Received:', message);

        // Initial handshake
        if (message.startsWith('MWOSP-v1')) {
            ws.send('MWOSP-v1 OK');
            renderUI(ws, clientData);
        }

        // User click
        if (message.startsWith('Click')) {
            const [_, x, y] = message.split(' ').map(Number);
            ws.send(`FillRect ${x - 5} ${y - 5} 10 10 63488`); // red square
            ws.send(`DrawCircle ${x} ${y} 8 2016`); // green circle
            ws.send(`DrawText ${x - 10} ${y - 15} 2 65535 Clicked!`);
            console.log(`Click at ${x},${y}`);
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
            clientData.state = "startpage";
            ws.send("Navigate startpage");
        }

        // PromptText
        if (message.startsWith('PromptText')) {
            const rid = message.substring(11);
            ws.send(`GetBackText ${rid} HelloFromServer`);
        }

        // Exit
        if (message.startsWith('Exit')) {
            ws.close();
        }

        // DrawSVG example (simple rectangle SVG)
        if (message.startsWith('DrawSVG')) {
            const exampleSVG = `<svg width="50" height="50"><rect width="50" height="50" fill="red"/></svg>`;
            ws.send(`DrawSVG 50 150 50 50 63488 ${exampleSVG}`);
        }
    });

    ws.on('close', () => {
        clients.delete(ws);
        console.log('Client disconnected');
    });
});

function renderUI(ws, clientData) {
    // Full screen background
    ws.send("FillRect 0 0 320 480 0"); // black
    // Top bar
    ws.send("FillRect 0 0 320 30 31"); // blue
    // Example button
    ws.send("FillRect 60 100 200 60 2016"); // green
    ws.send("DrawText 100 120 2 65535 Press Me"); // white text
    // Draw SVG example
    const svg = `<svg width="40" height="40"><circle cx="20" cy="20" r="20" fill="yellow"/></svg>`;
    ws.send(`DrawSVG 100 200 40 40 2016 ${svg}`);
    // Draw circle
    ws.send("DrawCircle 160 300 30 63488"); // red
}

server.listen(PORT, () => console.log(`Server listening on port ${PORT}`));
