/**
 * @file server.js
 * @brief Zero-Dependency WebSocket & HTTP Telemetry Gateway for QNX-ChaosPulse
 *
 * Implements native RFC-6455 WebSocket framing and static file serving without external npm dependencies.
 * Automatically attempts connection to QNX Neutrino target daemon on TCP 9092;
 * gracefully falls back to mock_engine.js when running in cloud environments (Vercel/Render)
 * or without physical hardware connected.
 *
 * Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
 */

const http = require('http');
const net = require('net');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const QnxMockEngine = require('./mock_engine');

const PORT = process.env.PORT || 3000;
const QNX_TARGET_HOST = process.env.QNX_HOST || '127.0.0.1';
const QNX_TARGET_PORT = parseInt(process.env.QNX_PORT || '9092', 10);
const PUBLIC_DIR = path.join(__dirname, 'public');

/* Active WebSocket Client Sockets */
const wsClients = new Set();

/* QNX Target Socket Connection State */
let qnxSocket = null;
let isQnxConnected = false;
let qnxReconnTimer = null;

/* Mock Engine Instance */
const mockEngine = new QnxMockEngine();

/**
 * @brief RFC 6455 WebSocket Handshake
 */
function handleUpgrade(req, socket, head) {
    if (req.headers['upgrade'] !== 'websocket') {
        socket.destroy();
        return;
    }

    const key = req.headers['sec-websocket-key'];
    if (!key) {
        socket.destroy();
        return;
    }

    const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
    const acceptKey = crypto
        .createHash('sha1')
        .update(key + GUID)
        .digest('base64');

    const responseHeaders = [
        'HTTP/1.1 101 Switching Protocols',
        'Upgrade: websocket',
        'Connection: Upgrade',
        `Sec-WebSocket-Accept: ${acceptKey}`,
        '\r\n'
    ];

    socket.write(responseHeaders.join('\r\n'));
    wsClients.add(socket);
    console.log(`[GATEWAY] WebSocket client connected. Active clients: ${wsClients.size}`);

    /* Send initial system status frame */
    const initialStatus = {
        type: 'STATUS_SYNC',
        target_mode: isQnxConnected ? 'QNX_HARDWARE_TARGET' : 'MOCK_EMULATION_FALLBACK',
        qnx_endpoint: `${QNX_TARGET_HOST}:${QNX_TARGET_PORT}`
    };
    sendWsText(socket, JSON.stringify(initialStatus));

    /* Decode RFC 6455 Frames */
    socket.on('data', (buffer) => {
        handleIncomingWsData(socket, buffer);
    });

    socket.on('close', () => {
        wsClients.delete(socket);
        console.log(`[GATEWAY] WebSocket client disconnected. Remaining: ${wsClients.size}`);
    });

    socket.on('error', (err) => {
        console.error('[GATEWAY] Client socket error:', err.message);
        wsClients.delete(socket);
        socket.destroy();
    });
}

/**
 * @brief Parse RFC 6455 WebSocket frames from client
 */
function handleIncomingWsData(socket, buf) {
    if (buf.length < 2) return;

    const firstByte = buf[0];
    const secondByte = buf[1];
    const opcode = firstByte & 0x0f;

    /* Close frame */
    if (opcode === 0x08) {
        socket.end();
        return;
    }
    /* Ping -> Pong */
    if (opcode === 0x09) {
        const pong = Buffer.from([0x8A, 0x00]);
        socket.write(pong);
        return;
    }
    /* Text frame */
    if (opcode === 0x01) {
        const isMasked = (secondByte & 0x80) !== 0;
        let payloadLen = secondByte & 0x7f;
        let offset = 2;

        if (payloadLen === 126) {
            payloadLen = buf.readUInt16BE(offset);
            offset += 2;
        } else if (payloadLen === 127) {
            payloadLen = Number(buf.readBigUInt64BE(offset));
            offset += 8;
        }

        if (!isMasked) return;

        const maskKey = buf.slice(offset, offset + 4);
        offset += 4;

        const payload = buf.slice(offset, offset + payloadLen);
        for (let i = 0; i < payload.length; i++) {
            payload[i] ^= maskKey[i % 4];
        }

        const msgString = payload.toString('utf8');
        try {
            const directive = JSON.parse(msgString);
            routeDirective(directive);
        } catch (e) {
            console.error('[GATEWAY] Failed to parse inbound JSON directive:', e.message);
        }
    }
}

/**
 * @brief Send Text Frame across WebSocket
 */
function sendWsText(socket, text) {
    if (!socket || socket.destroyed) return;
    const payload = Buffer.from(text, 'utf8');
    const length = payload.length;

    let header;
    if (length < 126) {
        header = Buffer.from([0x81, length]);
    } else if (length < 65536) {
        header = Buffer.alloc(4);
        header[0] = 0x81;
        header[1] = 126;
        header.writeUInt16BE(length, 2);
    } else {
        header = Buffer.alloc(10);
        header[0] = 0x81;
        header[1] = 127;
        header.writeBigUInt64BE(BigInt(length), 2);
    }

    try {
        socket.write(Buffer.concat([header, payload]));
    } catch (e) {
        wsClients.delete(socket);
    }
}

/**
 * @brief Broadcast telemetry to all connected cockpit browsers
 */
function broadcastTelemetry(obj) {
    if (wsClients.size === 0) return;
    const msg = JSON.stringify(obj);
    for (const client of wsClients) {
        sendWsText(client, msg);
    }
}

/**
 * @brief Route control directive either to real QNX target or Mock Engine
 */
function routeDirective(directive) {
    console.log('[GATEWAY] Dispatching Directive:', directive);

    if (isQnxConnected && qnxSocket) {
        /* Forward directly to QNX daemon socket */
        const payload = JSON.stringify(directive) + '\n';
        qnxSocket.write(payload, (err) => {
            if (err) console.error('[GATEWAY] Failed to forward to QNX target:', err.message);
        });
    } else {
        /* Route to embedded mock engine */
        mockEngine.handleDirective(directive);
    }
}

/**
 * @brief Establish/Maintain Persistent Link with QNX 8.0 Target on TCP 9092
 */
function connectToQnxTarget() {
    if (qnxSocket) {
        qnxSocket.destroy();
        qnxSocket = null;
    }

    qnxSocket = new net.Socket();
    let rxBuffer = '';

    qnxSocket.connect(QNX_TARGET_PORT, QNX_TARGET_HOST, () => {
        isQnxConnected = true;
        console.log(`[GATEWAY] CONNECTED to QNX Neutrino 8.0 hardware target at ${QNX_TARGET_HOST}:${QNX_TARGET_PORT}`);
        mockEngine.stop();
        broadcastTelemetry({
            type: 'TARGET_MODE_CHANGE',
            mode: 'QNX_HARDWARE_TARGET',
            target: `${QNX_TARGET_HOST}:${QNX_TARGET_PORT}`
        });
    });

    qnxSocket.on('data', (data) => {
        rxBuffer += data.toString('utf8');
        const lines = rxBuffer.split('\n');
        rxBuffer = lines.pop(); /* Preserve incomplete line */

        for (const line of lines) {
            const trimmed = line.trim();
            if (!trimmed) continue;
            try {
                const parsed = JSON.parse(trimmed);
                parsed.source = 'QNX_SDP_8_0_HARDWARE';
                broadcastTelemetry(parsed);
            } catch (err) {
                /* Non-JSON raw telemetry handling */
            }
        }
    });

    qnxSocket.on('close', () => {
        if (isQnxConnected) {
            console.log('[GATEWAY] Disconnected from QNX target. Switching to MOCK FALLBACK engine.');
        }
        isQnxConnected = false;
        mockEngine.start();
        scheduleQnxReconnect();
    });

    qnxSocket.on('error', (err) => {
        /* Suppress spam during normal probe failure */
        isQnxConnected = false;
        mockEngine.start();
    });
}

function scheduleQnxReconnect() {
    if (qnxReconnTimer) clearTimeout(qnxReconnTimer);
    qnxReconnTimer = setTimeout(() => {
        connectToQnxTarget();
    }, 4000);
}

/* Connect mock engine event stream */
mockEngine.on('telemetry', (frame) => {
    if (!isQnxConnected) {
        broadcastTelemetry(frame);
    }
});

/**
 * @brief Static HTTP Server
 */
const MIME_TYPES = {
    '.html': 'text/html; charset=UTF-8',
    '.css': 'text/css; charset=UTF-8',
    '.js': 'application/javascript; charset=UTF-8',
    '.json': 'application/json',
    '.png': 'image/png',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon'
};

const server = http.createServer((req, res) => {
    let filePath = path.join(PUBLIC_DIR, req.url === '/' ? 'index.html' : req.url);
    const ext = path.extname(filePath).toLowerCase();
    const contentType = MIME_TYPES[ext] || 'application/octet-stream';

    fs.readFile(filePath, (err, content) => {
        if (err) {
            if (err.code === 'ENOENT') {
                /* SPA fallback to index.html */
                fs.readFile(path.join(PUBLIC_DIR, 'index.html'), (err2, fallback) => {
                    if (err2) {
                        res.writeHead(404, { 'Content-Type': 'text/plain' });
                        res.end('404 Not Found');
                    } else {
                        res.writeHead(200, { 'Content-Type': 'text/html; charset=UTF-8' });
                        res.end(fallback);
                    }
                });
            } else {
                res.writeHead(500);
                res.end(`Server Error: ${err.code}`);
            }
        } else {
            res.writeHead(200, {
                'Content-Type': contentType,
                'Cache-Control': 'no-cache'
            });
            res.end(content);
        }
    });
});

server.on('upgrade', handleUpgrade);

// Only listen if executed directly
if (require.main === module) {
    server.listen(PORT, () => {
        console.log('===================================================================');
        console.log('  QNX-CHAOSPULSE // SDP 8.0 Real-Time Visualization Cockpit');
        console.log(`  Web Cockpit UI: http://localhost:${PORT}`);
        console.log(`  Probing QNX Target on: TCP ${QNX_TARGET_HOST}:${QNX_TARGET_PORT}`);
        console.log('  Auto-Fallback: Mock Engine ready if target is offline.');
        console.log('===================================================================');

        /* Start background mock engine immediately, and probe hardware */
        mockEngine.start();
        connectToQnxTarget();
    });
}

module.exports = { server, mockEngine, handleUpgrade };
