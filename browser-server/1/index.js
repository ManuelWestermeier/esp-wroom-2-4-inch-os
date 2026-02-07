const WebSocket = require('ws');
const http = require('http');

const PORT = process.env.PORT || 6767;
const server = http.createServer((req, res) => {
    res.writeHead(200);
    res.end("MWOSP Server Running");
});

const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
    console.log('New client connected');

    ws.on('message', (message) => {
        const msg = message.toString();
        console.log('Client:', msg);

        if (msg.startsWith('MWOSP-v1')) {
            ws.send('MWOSP-v1 OK');
            // Draw a simple UI on connection
            renderHome(ws);
        }

        if (msg.startsWith('Click')) {
            const [_, x, y] = msg.split(' ');
            console.log(`User clicked at ${x}, ${y}`);
            // Feedback: Draw a red square where user clicked
            ws.send(`FillRect ${x} ${y} 10 10 63488`); // 63488 is Red in RGB565
        }
    });
});

function renderHome(ws) {
    // Clear Screen (Black)
    ws.send("FillRect 0 0 320 480 0");
    // Draw Header Bar (Blue)
    ws.send("FillRect 0 0 320 40 31");
    // Draw a Button (Green)
    ws.send("FillRect 50 100 220 50 2016");
}

server.listen(PORT, () => {
    console.log(`Server started on port ${PORT}`);
});