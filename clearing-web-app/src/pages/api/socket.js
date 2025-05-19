import { createServer } from 'http';
import { parse } from 'url';
import WebSocket from 'ws';
import { WebSocketServer } from 'ws';

// Create a WebSocket server that will act as a proxy
const wss = new WebSocketServer({ noServer: true });
let connections = [];

// This handles our API route
export default function handler(req, res) {
  res.status(200).json({ message: 'WebSocket proxy is running' });
}

// This gets the custom server instance used by Next.js
// You'll need to add this to your next.config.js
if (typeof window === 'undefined' && !global.wssInitialized) {
  console.log('Initializing WebSocket Proxy');

  // This will be the http server that Next.js uses
  const server = global.server;

  if (server) {
    server.on('upgrade', function upgrade(request, socket, head) {
      const { pathname } = parse(request.url);

      // Only handle WebSocket connections to /api/socket route
      if (pathname === '/api/socket') {
        wss.handleUpgrade(request, socket, head, function done(ws) {
          wss.emit('connection', ws, request);
        });
      }
    });

    // When a client connects to our proxy
    wss.on('connection', function connection(ws) {
      console.log('Client connected to proxy');

      // Create a connection to the actual WebSocket server
      const targetWs = new WebSocket('ws://localhost:9002');
      connections.push({ client: ws, target: targetWs });

      // When our proxy receives a message, send it to the target server
      ws.on('message', function message(data) {
        if (targetWs.readyState === WebSocket.OPEN) {
          targetWs.send(data);
        }
      });

      // When the target server sends a message, forward it to the client
      targetWs.on('message', function message(data) {
        if (ws.readyState === WebSocket.OPEN) {
          ws.send(data);
        }
      });

      // Handle closing connections
      ws.on('close', function close() {
        console.log('Client disconnected from proxy');
        targetWs.close();
        connections = connections.filter(conn => conn.client !== ws);
      });

      targetWs.on('close', function close() {
        console.log('Target disconnected from proxy');
        ws.close();
        connections = connections.filter(conn => conn.target !== targetWs);
      });

      // Handle errors
      ws.on('error', console.error);
      targetWs.on('error', console.error);
    });

    global.wssInitialized = true;
  }
}
