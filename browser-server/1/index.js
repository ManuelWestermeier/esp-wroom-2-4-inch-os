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
            client.width = parseInt(parts[2], 10);
            client.height = parseInt(parts[3], 10);

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
    // Clear content area
    ws.send("ClearScreen 0");

    // Header
    ws.send('DrawString 10 10 65535 "MWOSP Test Server"');

    // Divider line
    ws.send("DrawLine 0 25 240 25 65535");

    // Info box
    ws.send("DrawRect 5 30 230 70 65535");

    ws.send(
        `DrawString 10 40 65535 "Session: ${client.sessionId}"`
    );
    ws.send(
        `DrawString 10 55 65535 "State: ${client.state}"`
    );
    ws.send(
        `DrawString 10 70 65535 "Resolution: ${client.width}x${client.height}"`
    );

    // Button
    ws.send("FillRect 10 90 100 30 2016");
    ws.send('DrawString 20 110 65535 "Click Me"');

    // Ask client for data (test round-trip)
    ws.send("GetSession 1");
    ws.send("GetState 2");
}

/* ================= SAFETY ================= */

process.on("SIGINT", () => {
    console.log("Shutting down server");
    process.exit(0);
});
