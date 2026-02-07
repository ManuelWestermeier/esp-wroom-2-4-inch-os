/**
 * MWOSP-v1 Test Server
 * Render-compatible (WSS via Render TLS)
 */

const WebSocket = require("ws");

/* ================= CONFIG ================= */

const PORT = process.env.PORT || 80;

/* ================= WEBSOCKET ================= */

const wss = new WebSocket.Server({ port: PORT });

console.log(`MWOSP test server listening (Render TLS) on port ${PORT}`);

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
            return;
        }

        /* ================= CLIENT REQUESTS ================= */

        if (msg === "NeedRender") {
            renderPage(ws, client);
            return;
        }

        if (msg.startsWith("Click ")) {
            const [, x, y] = msg.split(" ");
            client.state = "clicked";
            ws.send(`DrawString 10 40 65535 "Clicked at ${x},${y}"`);
            return;
        }

        if (msg.startsWith("Input ")) {
            const text = msg.substring(6);
            ws.send(`DrawString 10 60 65535 "Input: ${text}"`);
            return;
        }

        if (msg === "Refresh") {
            renderPage(ws, client);
            return;
        }

        if (
            msg.startsWith("GetBackSession ") ||
            msg.startsWith("GetBackState ") ||
            msg.startsWith("GetBackText ")
        ) {
            console.log("Client response:", msg);
            return;
        }
    });

    ws.on("close", () => {
        console.log("Client disconnected");
    });

    renderPage(ws, client);
});

/* ================= RENDERING ================= */

function renderPage(ws, client) {
    const w = client.width || 240;
    const h = client.height || 200;

    ws.send("ClearScreen 0");
    ws.send(`DrawString 10 10 65535 "MWOSP Test Server"`);

    ws.send(`DrawLine 0 25 ${Math.max(0, w - 1)} 25 65535`);

    const margin = 5;
    const boxW = Math.max(40, w - margin * 2);
    const boxH = Math.min(80, Math.floor(h * 0.35));

    ws.send(`DrawRect ${margin} 30 ${boxW} ${boxH} 65535`);
    ws.send(`DrawString 10 40 65535 "Session: ${client.sessionId}"`);
    ws.send(`DrawString 10 55 65535 "State: ${client.state}"`);
    ws.send(`DrawString 10 70 65535 "Resolution: ${client.width}x${client.height}"`);

    const btnW = Math.min(100, Math.max(60, Math.floor(w * 0.3)));
    const btnH = 30;

    ws.send(`FillRect 10 90 ${btnW} ${btnH} 2016`);
    ws.send(`DrawString 20 110 65535 "Click Me"`);

    ws.send("GetSession 1");
    ws.send("GetState 2");
}
