// mwosp_test_server.js
/**
 * MWOSP-v1 Test Server
 * Compatible with the provided Arduino Browser implementation
 */

const WebSocket = require("ws");

const PORT = 6767;
const wss = new WebSocket.Server({ port: PORT });

console.log(`MWOSP test server listening on ws://0.0.0.0:${PORT}`);

wss.on("connection", (ws, req) => {
    console.log("Client connected");

    const client = {
        sessionId: null,
        width: 0,
        height: 0,
        state: "startpage",
    };

    ws.on("message", (data) => {
        const msg = data.toString();
        console.log("RX:", msg);

        /* ================= HANDSHAKE ================= */
        if (msg.startsWith("MWOSP-v1 ")) {
            const parts = msg.split(" ");
            client.sessionId = parts[1];
            client.width = parseInt(parts[2], 10) || 240;
            client.height = parseInt(parts[3], 10) || 200;

            ws.send("MWOSP-v1 OK");
            console.log("Handshake OK:", client);
            return;
        }

        /* ================= CLIENT REQUESTS ================= */

        if (msg === "NeedRender") {
            renderPage(ws, client);
            return;
        }

        if (msg.startsWith("Click ")) {
            const [, x, y] = msg.split(" ");
            console.log(`Click at ${x},${y}`);

            client.state = "clicked";
            ws.send(`DrawString 10 40 65535 "Clicked at ${x},${y}"`);
            return;
        }

        if (msg.startsWith("Input ")) {
            const text = msg.substring(6);
            ws.send(`DrawString 10 60 65535 "Input: ${text}"`);
            return;
        }

        if (msg.startsWith("GetBackSession ")) {
            console.log("Session returned:", msg);
            return;
        }

        if (msg.startsWith("GetBackState ")) {
            console.log("State returned:", msg);
            return;
        }

        if (msg.startsWith("GetBackText ")) {
            console.log("Text returned:", msg);
            return;
        }

        if (msg === "Refresh") {
            renderPage(ws, client);
            return;
        }
    });

    ws.on("close", () => {
        console.log("Client disconnected");
    });
});

/* ================= RENDERING ================= */

function renderPage(ws, client) {
    // Use client's reported size to adapt layout
    const w = client.width || 240;
    const h = client.height || 200;

    // Clear content area
    ws.send("ClearScreen 0");

    // Header
    ws.send(`DrawString 10 10 65535 "MWOSP Test Server"`);

    // Divider line (use client width)
    ws.send(`DrawLine 0 25 ${Math.max(0, w - 1)} 25 65535`);

    // Info box (margins)
    const margin = 5;
    const boxW = Math.max(40, w - margin * 2);
    const boxH = Math.min(80, Math.floor(h * 0.35));
    ws.send(`DrawRect ${margin} 30 ${boxW} ${boxH} 65535`);

    ws.send(`DrawString 10 40 65535 "Session: ${client.sessionId}"`);
    ws.send(`DrawString 10 55 65535 "State: ${client.state}"`);
    ws.send(`DrawString 10 70 65535 "Resolution: ${client.width}x${client.height}"`);

    // Button (size adapts to width)
    const btnW = Math.min(100, Math.max(60, Math.floor(w * 0.3)));
    const btnH = 30;
    const btnX = 10;
    const btnY = 90;
    // Filled button
    ws.send(`FillRect ${btnX} ${btnY} ${btnW} ${btnH} 2016`);
    ws.send(`DrawString ${btnX + 10} ${btnY + 20} 65535 "Click Me"`);

    // Ask client for data (test round-trip)
    ws.send("GetSession 1");
    ws.send("GetState 2");
}

/* ================= SAFETY ================= */

process.on("SIGINT", () => {
    console.log("Shutting down server");
    process.exit(0);
});
