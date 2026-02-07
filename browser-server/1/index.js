const express = require("express");
const app = express();
const crypto = require("crypto");

const PORT = process.env.PORT || 80;

const sessions = {};

function generateSessionId() {
    return crypto.randomBytes(8).toString("hex");
}

// Example page content generator
function generatePageContent(sessionId, state) {
    let lines = [];
    lines.push(`DrawString 10 10 65535 "MWOSP HTTP Browser Test"`);
    lines.push(`DrawString 10 30 65535 "Session: ${sessionId}"`);
    lines.push(`DrawString 10 45 65535 "State: ${state}"`);
    lines.push(`FillRect 15 60 50 20 2016`);
    return lines.join("\n");
}

app.get("/@:state", (req, res) => {
    let sessionId = req.cookies?.MWOSP || generateSessionId();
    const state = req.params.state || "startpage";

    // Store session data
    sessions[sessionId] = { state };

    res.setHeader("Content-Type", "text/plain");
    res.setHeader("Set-Cookie", `MWOSP=${sessionId}; HttpOnly; Path=/`);
    res.send(generatePageContent(sessionId, state));
});

app.listen(PORT, () => {
    console.log(`MWOSP HTTP Server listening on port ${PORT}`);
});
