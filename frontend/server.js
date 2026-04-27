// ─── server.js ────────────────────────────────────────────────────────────────
//
// Node.js bridge between the C++ matching engine and the React frontend.
//
// Architecture:
//   C++ engine (TCP:9001, FIX protocol)
//       ↕
//   This bridge (maintains TCP connection to engine, WebSocket server on :8080)
//       ↕
//   React frontend (WebSocket client)
//
// Responsibilities:
//   1. Maintain a persistent TCP connection to the C++ engine
//   2. Send FIX orders to the engine on behalf of the frontend
//   3. Parse FIX execution reports from the engine
//   4. Broadcast order book snapshots and trade feed to all WebSocket clients
//   5. Simulate market data (order book depth) for visualization
//

const net = require('net');
const http = require('http');
const { WebSocketServer } = require('ws');

// ─── Configuration ────────────────────────────────────────────────────────────
const ENGINE_HOST = 'localhost';
const ENGINE_PORT = 9001;
const WS_PORT     = 8080;
const TICK_MS     = 200;   // how often to push order book snapshots

// ─── State ────────────────────────────────────────────────────────────────────

// Simulated order book (we maintain this in Node since the C++ engine
// doesn't yet have a WebSocket feed — that's a Week 7 extension)
let orderBook = {
  symbol: 'AAPL',
  bids: [],   // [{price, qty}]  descending
  asks: [],   // [{price, qty}]  ascending
  lastPrice: 18942,
  midPrice: 18942,
  spread: 4,
  totalVolume: 0,
};

// Price history for sparkline chart
let priceHistory = [];
const MAX_HISTORY = 200;

// Recent trades
let recentTrades = [];
const MAX_TRADES = 50;

// Latency samples from execution reports
let latencySamples = [];
const MAX_LATENCY  = 100;

// Order ID counter
let orderId = 1000;

// Connected WebSocket clients
const wsClients = new Set();

// ─── Market simulator ─────────────────────────────────────────────────────────
// Since the C++ engine only responds to orders we send,
// we simulate realistic market data around the last price.

function simulateMarket() {
  // Random walk on mid price
  const shock = (Math.random() - 0.5) * 4;
  orderBook.midPrice = Math.max(100, Math.round(orderBook.midPrice + shock));
  orderBook.lastPrice = orderBook.midPrice;

  // Rebuild order book around mid price
  const spread = 2 + Math.floor(Math.random() * 4);
  orderBook.spread = spread;

  orderBook.bids = [];
  orderBook.asks = [];

  for (let i = 0; i < 8; i++) {
    const bidPrice = orderBook.midPrice - spread / 2 - i * 2;
    const askPrice = orderBook.midPrice + spread / 2 + i * 2;
    const bidQty   = Math.floor(Math.random() * 500 + 100);
    const askQty   = Math.floor(Math.random() * 500 + 100);
    orderBook.bids.push({ price: Math.round(bidPrice), qty: bidQty });
    orderBook.asks.push({ price: Math.round(askPrice), qty: askQty });
  }

  // Update price history
  priceHistory.push(orderBook.midPrice);
  if (priceHistory.length > MAX_HISTORY) priceHistory.shift();
}

// ─── FIX helpers ─────────────────────────────────────────────────────────────
const SOH = '\x01';

function buildFixOrder(side, qty, price, ordType) {
  if (!price || price <= 0) return null;
  const id = ++orderId;
  let msg = '';
  msg += `35=D${SOH}`;
  msg += `11=${id}${SOH}`;
  msg += `55=AAPL${SOH}`;
  msg += `54=${side === 'BUY' ? 1 : 2}${SOH}`;
  msg += `38=${qty}${SOH}`;
  msg += `40=${ordType === 'MARKET' ? 1 : 2}${SOH}`;
  if (ordType !== 'MARKET') msg += `44=${price}${SOH}`;
  msg += `10=000${SOH}`;

  // Wrap with header
  const body = msg;
  return `8=FIX.4.2${SOH}9=${body.length}${SOH}${body}`;
}

function parseFixExecReport(raw) {
  const fields = {};
  const sep = raw.includes(SOH) ? SOH : '|';
  raw.split(sep).forEach(field => {
    const eq = field.indexOf('=');
    if (eq > 0) {
      const tag = parseInt(field.substring(0, eq));
      fields[tag] = field.substring(eq + 1);
    }
  });

  if (fields[35] !== '8') return null;  // not exec report

  return {
    clordId:   fields[11] || '',
    symbol:    fields[55] || 'AAPL',
    side:      fields[54] === '1' ? 'BUY' : 'SELL',
    execQty:   parseInt(fields[32] || '0'),
    execPrice: parseInt(fields[31] || '0'),
    leavesQty: parseInt(fields[151] || '0'),
  };
}

// ─── TCP connection to C++ engine ────────────────────────────────────────────
let engineSocket = null;
let engineConnected = false;
let recvBuf = '';

function connectToEngine() {
  console.log(`[bridge] Connecting to C++ engine at ${ENGINE_HOST}:${ENGINE_PORT}...`);

  engineSocket = new net.Socket();

  engineSocket.connect(ENGINE_PORT, ENGINE_HOST, () => {
    engineConnected = true;
    console.log('[bridge] Connected to C++ engine');
    broadcast({ type: 'engine_status', connected: true });
  });

  engineSocket.on('data', (data) => {
    recvBuf += data.toString();

    // Parse complete FIX messages (terminated by tag 10 + SOH)
    while (true) {
      const endIdx = recvBuf.indexOf('\x0110=');
      if (endIdx === -1) break;
      const csEnd = recvBuf.indexOf(SOH, endIdx + 1);
      if (csEnd === -1) break;

      const msg = recvBuf.substring(0, csEnd + 1);
      recvBuf = recvBuf.substring(csEnd + 1);

      const exec = parseFixExecReport(msg);
      if (exec) {
        // Record trade
        const trade = {
          id:        Date.now(),
          price:     exec.execPrice,
          qty:       exec.execQty,
          side:      exec.side,
          timestamp: Date.now(),
        };
        recentTrades.unshift(trade);
        if (recentTrades.length > MAX_TRADES) recentTrades.pop();

        // Update last price
        orderBook.lastPrice  = exec.execPrice;
        orderBook.totalVolume += exec.execQty;

        // Simulate latency (mock — real latency would need rdtsc timestamps)
        const lat = 50 + Math.random() * 150;
        latencySamples.push(lat);
        if (latencySamples.length > MAX_LATENCY) latencySamples.shift();

        broadcast({ type: 'trade', trade });
        console.log(`[engine] Fill: ${exec.symbol} ${exec.execQty} @ ${exec.execPrice}`);
      }
    }
  });

  engineSocket.on('close', () => {
    engineConnected = false;
    console.log('[bridge] Engine disconnected, retrying in 3s...');
    broadcast({ type: 'engine_status', connected: false });
    setTimeout(connectToEngine, 3000);
  });

  engineSocket.on('error', (err) => {
    console.log(`[bridge] Engine connection error: ${err.message}`);
    engineConnected = false;
  });
}

// ─── WebSocket server ─────────────────────────────────────────────────────────
const httpServer = http.createServer();
const wss = new WebSocketServer({ server: httpServer });

function broadcast(data) {
  const json = JSON.stringify(data);
  for (const client of wsClients) {
    if (client.readyState === 1) {  // OPEN
      client.send(json);
    }
  }
}

wss.on('connection', (ws) => {
  wsClients.add(ws);
  console.log(`[ws] Client connected (total: ${wsClients.size})`);

  // Send initial state
  ws.send(JSON.stringify({
    type: 'snapshot',
    orderBook,
    priceHistory,
    recentTrades,
    latencySamples,
    engineConnected,
  }));

  // Handle orders from frontend
  ws.on('message', (raw) => {
    try {
      const msg = JSON.parse(raw.toString());

      if (msg.type === 'new_order') {
        const { side, qty, price, ordType } = msg;  // price 已经是整数
        console.log(`[debug] received: side=${side} qty=${qty} price=${price} ordType=${ordType}`);
        const fixMsg = buildFixOrder(side, qty, price, ordType);

        if (fixMsg && engineConnected && engineSocket) {
          engineSocket.write(fixMsg);
          console.log(`[ws→engine] Order: ${side} ${qty} @ ${price}`);
        } else {
          // Engine not connected: simulate locally
          const trade = {
            id:        Date.now(),
            price:     price || orderBook.midPrice,
            qty,
            side,
            timestamp: Date.now(),
          };
          recentTrades.unshift(trade);
          if (recentTrades.length > MAX_TRADES) recentTrades.pop();
          orderBook.totalVolume += qty;
          broadcast({ type: 'trade', trade });
          console.log(`[ws] Simulated order (engine offline): ${side} ${qty}`);
        }
      }
    } catch (e) {
      console.error('[ws] Bad message:', e.message);
    }
  });

  ws.on('close', () => {
    wsClients.delete(ws);
    console.log(`[ws] Client disconnected (total: ${wsClients.size})`);
  });
});

// ─── Market data tick ─────────────────────────────────────────────────────────
setInterval(() => {
  simulateMarket();
  broadcast({
    type: 'tick',
    orderBook,
    priceHistory: priceHistory.slice(-60),
    latencySamples,
  });
}, TICK_MS);

// ─── Start ───────────────────────────────────────────────────────────────────
httpServer.listen(WS_PORT, () => {
  console.log(`\n${'═'.repeat(50)}`);
  console.log('  HFT Bridge Server');
  console.log(`  WebSocket : ws://localhost:${WS_PORT}`);
  console.log(`  C++ Engine: ${ENGINE_HOST}:${ENGINE_PORT}`);
  console.log(`${'═'.repeat(50)}\n`);
});

connectToEngine();
