// File: server.js
// Simple Node.js test server for the Browser WebSocket protocol (MWOSP-v1).
// Usage:
//   npm install ws
//   node server.js
//
// This server accepts WebSocket connections and expects the client to send a
// handshake like: "MWOSP-v1 <session> <width> <height>".
// After handshake, server will send a few demo Draw*/Fill* commands periodically.
// It also responds to GetStorage/SetStorage messages in-memory.

const WebSocket = require('ws');

const wss = new WebSocket.Server({ port: 8080 });
console.log('MWOSP test server listening on ws://0.0.0.0:8080');

const storage = new Map();

wss.on('connection', (ws, req) => {
    console.log('client connected:', req.socket.remoteAddress);

    ws.on('message', (msg) => {
        msg = msg.toString();
        console.log('-> received:', msg);

        // Handle initial handshake tag; we don't strictly need to validate it,
        // but log and send back a theme and some demo commands.
        if (msg.startsWith('MWOSP-v1')) {
            console.log('Handshake:', msg);
            // Send a few commands to draw a demo screen
            setTimeout(() => {
                ws.send('FillRect 0 0 320 240 0'); // black background (color=0)
                ws.send('FillRect 6 26 308 48 63488'); // red card (example 16-bit color)
                ws.send('DrawText 12 36 2 65535 Hello from MWOSP demo'); // white text
                ws.send('DrawCircle 160 140 30 2016'); // green-ish circle
                // Demonstrate an SVG (very small example - client may ignore if unsupported)
                const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><rect width="24" height="24" fill="#0000ff"/></svg>';
                ws.send('DrawSVG 200 100 24 24 31 ' + svg);
            }, 200);
            return;
        }

        // Storage commands
        if (msg.startsWith('SetStorage ')) {
            // Format: SetStorage <key> <value>
            const rest = msg.substring('SetStorage '.length);
            const sp = rest.indexOf(' ');
            if (sp !== -1) {
                const key = rest.substring(0, sp);
                const val = rest.substring(sp + 1);
                storage.set(key, val);
                console.log(`Stored key=${key} len=${val.length}`);
            }
            return;
        }

        if (msg.startsWith('GetStorage ')) {
            const key = msg.substring('GetStorage '.length);
            const val = storage.get(key) || '';
            ws.send(`GetBackStorage ${key} ${val}`);
            console.log(`Returned storage for ${key}`);
            return;
        }

        // Echo other messages for debugging
        ws.send('Echo: ' + msg);
    });

    ws.on('close', () => {
        console.log('client disconnected');
    });

    // Periodically send a heartbeat or random draw commands
    const interval = setInterval(() => {
        if (ws.readyState !== WebSocket.OPEN) {
            clearInterval(interval);
            return;
        }
        // send a small status text at top-left
        const now = new Date().toISOString();
        ws.send(`DrawText 6 3 1 65535 ServerTime: ${now}`);
    }, 5000);
});
