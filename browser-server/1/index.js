/**
 * MWOSP-v2 HTTP Test Server
 * Render-compatible (HTTP only)
 * Plain Node.js http module
 */

const http = require("http");
const url = require("url");
const crypto = require("crypto");

const PORT = process.env.PORT || 3000;

// In-memory session store
const sessions = new Map();

function createSession() {
    return {
        state: "startpage",
        lastClick: null,
        inputText: "",
        width: 240,
        height: 200,
        sessionId: crypto.randomBytes(4).toString("hex"),
    };
}

function parseCookies(req) {
    const list = {};
    const rc = req.headers.cookie;
    if (rc) {
        rc.split(";").forEach(cookie => {
            const parts = cookie.split("=");
            list[parts.shift().trim()] = decodeURIComponent(parts.join("="));
        });
    }
    return list;
}

function renderPage(session) {
    const w = session.width;
    const h = session.height;
    const lines = [];

    lines.push(`ClearScreen: 0`);
    lines.push(`DrawString: 10 10 65535 "MWOSP HTTP Test Server"`);
    lines.push(`DrawLine: 0 25 ${w - 1} 25 65535`);

    const margin = 5;
    const boxW = Math.max(40, w - margin * 2);
    const boxH = Math.min(80, Math.floor(h * 0.35));
    lines.push(`DrawRect: ${margin} 30 ${boxW} ${boxH} 65535`);

    lines.push(`DrawString: 10 40 65535 "Session: ${session.sessionId}"`);
    lines.push(`DrawString: 10 55 65535 "State: ${session.state}"`);
    lines.push(`DrawString: 10 70 65535 "Resolution: ${w}x${h}"`);

    // Button
    const btnW = Math.min(100, Math.max(60, Math.floor(w * 0.3)));
    const btnH = 30;
    lines.push(`FillRect: 10 90 ${btnW} ${btnH} 2016`);
    lines.push(`DrawString: 20 110 65535 "Click Me"`);

    return lines.join("\n");
}

function collectPostData(req, callback) {
    let body = "";
    req.on("data", chunk => body += chunk.toString());
    req.on("end", () => {
        try {
            callback(JSON.parse(body));
        } catch (e) {
            callback({});
        }
    });
}

// HTTP server
const server = http.createServer((req, res) => {
    const parsedUrl = url.parse(req.url, true);
    const cookies = parseCookies(req);
    let sessionId = cookies.sessionId;

    // Ensure session exists
    if (!sessionId || !sessions.has(sessionId)) {
        const session = createSession();
        sessionId = session.sessionId;
        sessions.set(sessionId, session);
        res.setHeader("Set-Cookie", `sessionId=${sessionId}; Path=/`);
    }

    const session = sessions.get(sessionId);

    // GET /@state -> render page
    if (req.method === "GET" && parsedUrl.pathname === "/@state") {
        res.writeHead(200, { "Content-Type": "text/plain" });
        res.end(renderPage(session));
        return;
    }

    // POST endpoints
    if (req.method === "POST") {
        if (parsedUrl.pathname === "/click") {
            collectPostData(req, data => {
                if (data.x !== undefined && data.y !== undefined) {
                    session.lastClick = { x: data.x, y: data.y };
                    session.state = "clicked";
                }
                res.writeHead(200, { "Content-Type": "text/plain" });
                res.end("OK");
            });
            return;
        }

        if (parsedUrl.pathname === "/input") {
            collectPostData(req, data => {
                session.inputText = data.text || "";
                res.writeHead(200, { "Content-Type": "text/plain" });
                res.end("OK");
            });
            return;
        }

        if (parsedUrl.pathname === "/refresh") {
            res.writeHead(200, { "Content-Type": "text/plain" });
            res.end(renderPage(session));
            return;
        }
    }

    // Unknown endpoint
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("Not Found");
});

// Start server
server.listen(PORT, () => {
    console.log(`MWOSP HTTP test server running on port ${PORT}`);
});
