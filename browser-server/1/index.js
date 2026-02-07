const WebSocket = require('ws');
const http = require('http');

const PORT = process.env.PORT || 3000;

const sessions = {}; // store sessions per client

const server = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('MWOSP Server active\n');
});

const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
    console.log('Client connected');
    let clientSession = { state: "startpage", session: "" };

    ws.on('message', (message) => {
        const msg = message.toString();
        console.log('Received:', msg);

        if (msg.startsWith('MWOSP-v1')) {
            ws.send('MWOSP-v1 OK');
            renderUI(ws, clientSession);
        }

        if (msg.startsWith('Click')) {
            const [_, x, y] = msg.split(' ').map(Number);
            ws.send(`FillRect ${x - 5} ${y - 5} 10 10 63488`); // red square
        }

        if (msg.startsWith('Navigate')) {
            clientSession.state = msg.substring(9);
            ws.send(`Navigate ${clientSession.state}`);
        }

        if (msg.startsWith('GetSession')) {
            const rid = msg.split(' ')[1];
            ws.send(`GetBackSession ${rid} ${clientSession.session}`);
        }

        if (msg.startsWith('SetSession')) {
            clientSession.session = msg.substring(11);
        }

        if (msg.startsWith('GetState')) {
            const rid = msg.split(' ')[1];
            ws.send(`GetBackState ${rid} ${clientSession.state}`);
        }

        if (msg.startsWith('SetState')) {
            clientSession.state = msg.substring(9);
        }
    });

    ws.on('close', () => console.log('Client disconnected'));
});

function renderUI(ws, clientSession) {
    ws.send("FillRect 0 0 320 480 0");    // background
    ws.send("FillRect 0 0 320 30 31");    // top bar
    ws.send("FillRect 60 100 200 60 2016"); // example button
}

server.listen(PORT, () => {
    console.log(`Server listening on port ${PORT}`);
});
