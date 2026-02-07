const WebSocket = require('ws');
const http = require('http');

// Render sets the PORT environment variable automatically
const PORT = process.env.PORT || 3000;

const server = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('MWOSP Server is active\n');
});

const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
    console.log('Client connected');

    ws.on('message', (message) => {
        const msg = message.toString();
        console.log('Received:', msg);

        if (msg.startsWith('MWOSP-v1')) {
            ws.send('MWOSP-v1 OK');
            renderUI(ws);
        }

        if (msg.startsWith('Click')) {
            const parts = msg.split(' ');
            const x = parseInt(parts[1]);
            const y = parseInt(parts[2]);

            // Draw a red square (63488 is Red in RGB565) where clicked
            ws.send(`FillRect ${x - 5} ${y - 5} 10 10 63488`);
            console.log(`Rendered click at ${x},${y}`);
        }
    });

    ws.on('close', () => console.log('Client disconnected'));
});

function renderUI(ws) {
    // Fill background (Black: 0)
    ws.send("FillRect 0 0 320 480 0");
    // Draw Top Bar (Blue: 31)
    ws.send("FillRect 0 0 320 30 31");
    // Draw a "Button" (Green: 2016)
    ws.send("FillRect 60 100 200 60 2016");
}

server.listen(PORT, () => {
    console.log(`Server listening on port ${PORT}`);
});