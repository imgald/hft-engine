import { useState, useEffect, useRef, useCallback } from 'react'

// ─── Constants ────────────────────────────────────────────────────────────────
const WS_URL = 'ws://localhost:8080'
const COLORS = {
  buy:     '#00e5a0',
  sell:    '#ff4466',
  neutral: '#c8d0e0',
  dim:     '#3a4558',
  bg:      '#060a0f',
  panel:   '#0a1018',
  border:  '#1a2535',
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
function percentile(arr, p) {
  if (!arr.length) return 0
  const s = [...arr].sort((a, b) => a - b)
  return s[Math.floor(s.length * p / 100)] ?? 0
}

function fmtPrice(p) {
  return (p / 100).toFixed(2)
}

// ─── PriceChart (Canvas) ──────────────────────────────────────────────────────
function PriceChart({ history }) {
  const ref = useRef(null)
  useEffect(() => {
    const canvas = ref.current
    if (!canvas || history.length < 2) return
    const ctx = canvas.getContext('2d')
    const W = canvas.width, H = canvas.height
    ctx.clearRect(0, 0, W, H)

    const min = Math.min(...history), max = Math.max(...history)
    const range = max - min || 1
    const pad = { t: 8, b: 20, l: 4, r: 52 }

    // Grid lines
    ctx.strokeStyle = '#0d1520'; ctx.lineWidth = 1
    for (let i = 0; i <= 3; i++) {
      const y = pad.t + (i / 3) * (H - pad.t - pad.b)
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(W - pad.r, y); ctx.stroke()
      const val = max - (i / 3) * range
      ctx.fillStyle = '#3a4558'; ctx.font = '9px monospace'; ctx.textAlign = 'right'
      ctx.fillText(fmtPrice(val), W - 2, y + 3)
    }

    const pts = history.map((v, i) => [
      pad.l + (i / (history.length - 1)) * (W - pad.l - pad.r),
      H - pad.b - ((v - min) / range) * (H - pad.t - pad.b)
    ])

    // Gradient fill
    const grad = ctx.createLinearGradient(0, pad.t, 0, H - pad.b)
    grad.addColorStop(0, 'rgba(0,229,160,0.2)')
    grad.addColorStop(1, 'rgba(0,229,160,0.01)')
    ctx.beginPath()
    ctx.moveTo(pts[0][0], H - pad.b)
    pts.forEach(([x, y]) => ctx.lineTo(x, y))
    ctx.lineTo(pts[pts.length - 1][0], H - pad.b)
    ctx.closePath(); ctx.fillStyle = grad; ctx.fill()

    // Line
    ctx.beginPath(); ctx.strokeStyle = '#00e5a0'; ctx.lineWidth = 1.5; ctx.lineJoin = 'round'
    pts.forEach(([x, y], i) => i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y))
    ctx.stroke()

    // Last dot
    const [lx, ly] = pts[pts.length - 1]
    ctx.beginPath(); ctx.arc(lx, ly, 3, 0, Math.PI * 2)
    ctx.fillStyle = '#00e5a0'; ctx.fill()
  }, [history])

  return <canvas ref={ref} width={480} height={90} style={{ width: '100%', height: 90 }} />
}

// ─── OrderBook ────────────────────────────────────────────────────────────────
function OrderBook({ bids = [], asks = [], lastPrice }) {
  const maxQty = Math.max(...bids.map(l => l.qty), ...asks.map(l => l.qty), 1)

  return (
    <div style={{ fontFamily: 'monospace', fontSize: 11 }}>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 72px 72px', color: COLORS.dim, fontSize: 9, letterSpacing: '0.1em', marginBottom: 4 }}>
        <span>QTY</span><span style={{ textAlign: 'right' }}>PRICE</span><span style={{ textAlign: 'right' }}>QTY</span>
      </div>

      {[...asks].reverse().map((l, i) => (
        <div key={l.price} style={{ display: 'grid', gridTemplateColumns: '1fr 72px 72px', height: 20, position: 'relative', marginBottom: 1, alignItems: 'center' }}>
          <div style={{ position: 'absolute', right: 72, top: 0, bottom: 0, width: `${(l.qty / maxQty) * 60}%`, background: 'rgba(255,68,102,0.12)', borderRadius: 2 }} />
          <span />
          <span style={{ color: COLORS.sell, textAlign: 'right', fontWeight: i === asks.length - 1 ? 700 : 400 }}>{fmtPrice(l.price)}</span>
          <span style={{ color: '#8892a4', textAlign: 'right' }}>{l.qty}</span>
        </div>
      ))}

      <div style={{ textAlign: 'center', padding: '6px 0', color: '#fff', fontWeight: 700, fontSize: 14, borderTop: `1px solid ${COLORS.border}`, borderBottom: `1px solid ${COLORS.border}`, margin: '4px 0' }}>
        {lastPrice ? fmtPrice(lastPrice) : '—'}
      </div>

      {bids.map((l, i) => (
        <div key={l.price} style={{ display: 'grid', gridTemplateColumns: '1fr 72px 72px', height: 20, position: 'relative', marginBottom: 1, alignItems: 'center' }}>
          <div style={{ position: 'absolute', left: 0, top: 0, bottom: 0, width: `${(l.qty / maxQty) * 60}%`, background: 'rgba(0,229,160,0.1)', borderRadius: 2 }} />
          <span style={{ color: '#8892a4', textAlign: 'right', position: 'relative' }}>{l.qty}</span>
          <span style={{ color: COLORS.buy, textAlign: 'right', position: 'relative', fontWeight: i === 0 ? 700 : 400 }}>{fmtPrice(l.price)}</span>
          <span />
        </div>
      ))}
    </div>
  )
}

// ─── LatencyPanel ─────────────────────────────────────────────────────────────
function LatencyPanel({ samples }) {
  const p50  = percentile(samples, 50).toFixed(0)
  const p99  = percentile(samples, 99).toFixed(0)
  const avg  = samples.length ? (samples.reduce((a, b) => a + b, 0) / samples.length).toFixed(0) : 0
  const bars = samples.slice(-40)
  const maxBar = Math.max(...bars, 1)

  return (
    <div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3,1fr)', gap: 6, marginBottom: 10 }}>
        {[['P50', p50, COLORS.buy], ['P99', p99, '#ff4466'], ['AVG', avg, COLORS.neutral]].map(([k, v, c]) => (
          <div key={k} style={{ background: '#0d1520', borderRadius: 6, padding: '7px 4px', textAlign: 'center' }}>
            <div style={{ color: COLORS.dim, fontSize: 9, letterSpacing: '0.1em', marginBottom: 3 }}>{k}</div>
            <div style={{ color: c, fontSize: 13, fontWeight: 700 }}>{v}ns</div>
          </div>
        ))}
      </div>
      <div style={{ display: 'flex', alignItems: 'flex-end', gap: 2, height: 36 }}>
        {bars.map((v, i) => {
          const h = Math.max(2, (v / maxBar) * 36)
          const c = v > p99 * 0.8 ? '#ff4466' : v > p50 * 1.5 ? '#ffaa44' : '#00e5a0'
          return <div key={i} style={{ flex: 1, height: h, background: c, borderRadius: '1px 1px 0 0', opacity: 0.6 + (i / bars.length) * 0.4 }} />
        })}
      </div>
    </div>
  )
}

// ─── TradesFeed ───────────────────────────────────────────────────────────────
function TradesFeed({ trades }) {
  return (
    <div style={{ fontFamily: 'monospace', fontSize: 11 }}>
      <div style={{ display: 'grid', gridTemplateColumns: '55px 65px 55px auto', color: COLORS.dim, fontSize: 9, letterSpacing: '0.1em', marginBottom: 6 }}>
        <span>TIME</span><span style={{ textAlign: 'right' }}>PRICE</span><span style={{ textAlign: 'right' }}>QTY</span><span style={{ textAlign: 'right' }}>SIDE</span>
      </div>
      {trades.slice(0, 15).map(t => (
        <div key={t.id} style={{ display: 'grid', gridTemplateColumns: '55px 65px 55px auto', height: 22, borderBottom: `1px solid #0d1520`, alignItems: 'center' }}>
          <span style={{ color: COLORS.dim }}>{new Date(t.timestamp).toLocaleTimeString('en', { hour12: false }).slice(0, 8)}</span>
          <span style={{ color: t.side === 'BUY' ? COLORS.buy : COLORS.sell, textAlign: 'right', fontWeight: 600 }}>{fmtPrice(t.price)}</span>
          <span style={{ color: '#8892a4', textAlign: 'right' }}>{t.qty}</span>
          <span style={{ textAlign: 'right', color: t.side === 'BUY' ? COLORS.buy : COLORS.sell, fontSize: 9 }}>{t.side}</span>
        </div>
      ))}
    </div>
  )
}

// ─── OrderForm ────────────────────────────────────────────────────────────────
function OrderForm({ onSubmit, midPrice }) {
  const [side, setSide]     = useState('BUY')
  const [ordType, setType]  = useState('LIMIT')
  const [price, setPrice]   = useState('')
  const [qty, setQty]       = useState('100')
  const [flash, setFlash]   = useState(null)

  const submit = () => {
    const p = ordType === 'MARKET' 
    ? midPrice 
    : Math.round(parseFloat(price) * 100)
    const q = parseInt(qty) || 100
    console.log('submit:', { side, ordType, price, p, q })  // debug
    onSubmit({ side, ordType, price: p, qty: q })
    setFlash({ side, qty: q, price: p })
    setTimeout(() => setFlash(null), 2500)
  }

  const btn = (label, val, cur, setter, activeColor) => (
    <button onClick={() => setter(val)} style={{
      flex: 1, padding: '6px 0', borderRadius: 4, cursor: 'pointer', fontSize: 11, fontWeight: 700,
      background: cur === val ? (activeColor === 'buy' ? '#0a2518' : activeColor === 'sell' ? '#200a14' : '#0f1e2e') : '#0d1520',
      border: `1px solid ${cur === val ? (activeColor === 'buy' ? COLORS.buy : activeColor === 'sell' ? COLORS.sell : '#1e4060') : COLORS.border}`,
      color: cur === val ? (activeColor === 'buy' ? COLORS.buy : activeColor === 'sell' ? COLORS.sell : '#8ab8d8') : COLORS.dim,
    }}>{label}</button>
  )

  return (
    <div>
      <div style={{ fontSize: 10, color: COLORS.dim, letterSpacing: '0.15em', marginBottom: 10 }}>PLACE ORDER</div>
      <div style={{ display: 'flex', gap: 4, marginBottom: 8 }}>
        {btn('BUY',  'BUY',  side, setSide, 'buy')}
        {btn('SELL', 'SELL', side, setSide, 'sell')}
      </div>
      <div style={{ display: 'flex', gap: 4, marginBottom: 8 }}>
        {btn('LIMIT',  'LIMIT',  ordType, setType, 'neutral')}
        {btn('MARKET', 'MARKET', ordType, setType, 'neutral')}
      </div>
      {ordType === 'LIMIT' && (
        <input type="number" placeholder={`Price (${fmtPrice(midPrice)})`}
          value={price} onChange={e => setPrice(e.target.value)}
          style={{ width: '100%', background: '#0d1520', border: `1px solid ${COLORS.border}`, borderRadius: 4, color: COLORS.neutral, padding: '7px 8px', fontSize: 12, marginBottom: 6, boxSizing: 'border-box', outline: 'none', fontFamily: 'monospace' }} />
      )}
      <input type="number" placeholder="Qty (100)" value={qty} onChange={e => setQty(e.target.value)}
        style={{ width: '100%', background: '#0d1520', border: `1px solid ${COLORS.border}`, borderRadius: 4, color: COLORS.neutral, padding: '7px 8px', fontSize: 12, marginBottom: 8, boxSizing: 'border-box', outline: 'none', fontFamily: 'monospace' }} />
      <button onClick={submit} style={{
        width: '100%', padding: '9px 0', borderRadius: 4, cursor: 'pointer', fontSize: 12, fontWeight: 700,
        background: side === 'BUY' ? '#0a2518' : '#200a14',
        border: `1px solid ${side === 'BUY' ? COLORS.buy : COLORS.sell}`,
        color: side === 'BUY' ? COLORS.buy : COLORS.sell, letterSpacing: '0.08em',
      }}>
        {side} AAPL
      </button>
      {flash && (
        <div style={{ marginTop: 8, padding: '6px 8px', borderRadius: 4, background: '#0a1e14', border: '1px solid #00e5a040', color: '#00e5a0', fontSize: 10 }}>
          ✓ Submitted {flash.side} {flash.qty} @ {fmtPrice(flash.price)}
        </div>
      )}
    </div>
  )
}

// ─── App ──────────────────────────────────────────────────────────────────────
export default function App() {
  const wsRef = useRef(null)
  const [connected, setConnected]         = useState(false)
  const [engineOnline, setEngineOnline]   = useState(false)
  const [orderBook, setOrderBook]         = useState({ bids: [], asks: [], lastPrice: 18942, midPrice: 18942, totalVolume: 0 })
  const [priceHistory, setPriceHistory]   = useState([])
  const [trades, setTrades]               = useState([])
  const [latency, setLatency]             = useState([])

  useEffect(() => {
    function connect() {
      const ws = new WebSocket(WS_URL)
      wsRef.current = ws

      ws.onopen = () => { setConnected(true); console.log('[ws] connected') }

      ws.onmessage = (e) => {
        const msg = JSON.parse(e.data)
        switch (msg.type) {
          case 'snapshot':
            setOrderBook(msg.orderBook)
            setPriceHistory(msg.priceHistory || [])
            setTrades(msg.recentTrades || [])
            setLatency(msg.latencySamples || [])
            setEngineOnline(msg.engineConnected)
            break
          case 'tick':
            setOrderBook(msg.orderBook)
            setPriceHistory(msg.priceHistory || [])
            setLatency(msg.latencySamples || [])
            break
          case 'trade':
            setTrades(prev => [msg.trade, ...prev].slice(0, 50))
            break
          case 'engine_status':
            setEngineOnline(msg.connected)
            break
        }
      }

      ws.onclose = () => {
        setConnected(false)
        setTimeout(connect, 2000)
      }
    }
    connect()
    return () => wsRef.current?.close()
  }, [])

  const sendOrder = useCallback((order) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify({ type: 'new_order', ...order }))
    }
  }, [])

  const prevPrice = priceHistory.length > 1 ? priceHistory[priceHistory.length - 2] : orderBook.lastPrice
  const priceColor = orderBook.lastPrice > prevPrice ? COLORS.buy : orderBook.lastPrice < prevPrice ? COLORS.sell : COLORS.neutral

  return (
    <div style={{ background: COLORS.bg, minHeight: '100vh', fontFamily: 'monospace' }}>

      {/* Header */}
      <div style={{ background: '#0a1018', borderBottom: `1px solid ${COLORS.border}`, padding: '10px 20px', display: 'flex', alignItems: 'center', gap: 20, flexWrap: 'wrap' }}>
        <div>
          <div style={{ fontSize: 11, color: COLORS.dim, letterSpacing: '0.2em' }}>LOW LATENCY TRADING SYSTEM</div>
          <div style={{ fontSize: 9, color: '#2a3548', letterSpacing: '0.15em' }}>C++ Matching Engine + epoll + FIX 4.2</div>
        </div>
        <div style={{ display: 'flex', gap: 16, marginLeft: 'auto', alignItems: 'center' }}>
          <div style={{ textAlign: 'right' }}>
            <div style={{ fontSize: 9, color: COLORS.dim }}>VOLUME</div>
            <div style={{ fontSize: 13, color: COLORS.neutral, fontWeight: 700 }}>{(orderBook.totalVolume || 0).toLocaleString()}</div>
          </div>
          <div style={{ padding: '4px 10px', borderRadius: 4, fontSize: 10, fontWeight: 700, letterSpacing: '0.1em',
            background: engineOnline ? '#0a2518' : '#200a14',
            border: `1px solid ${engineOnline ? COLORS.buy : COLORS.sell}`,
            color: engineOnline ? COLORS.buy : COLORS.sell }}>
            ENGINE {engineOnline ? 'ONLINE' : 'OFFLINE'}
          </div>
          <div style={{ padding: '4px 10px', borderRadius: 4, fontSize: 10,
            background: connected ? '#0a2518' : '#200a14',
            border: `1px solid ${connected ? COLORS.buy : COLORS.sell}`,
            color: connected ? COLORS.buy : COLORS.sell }}>
            WS {connected ? '●' : '○'}
          </div>
        </div>
      </div>

      {/* Main grid */}
      <div style={{ display: 'grid', gridTemplateColumns: '240px 1fr 240px', gap: 1, padding: 1, height: 'calc(100vh - 60px)' }}>

        {/* Left: Order Book + Order Form */}
        <div style={{ background: COLORS.panel, borderRight: `1px solid ${COLORS.border}`, padding: 14, overflowY: 'auto' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 10 }}>
            <span style={{ fontSize: 10, color: COLORS.dim, letterSpacing: '0.15em' }}>ORDER BOOK</span>
            <span style={{ fontSize: 10, color: COLORS.dim }}>
              SPR: <span style={{ color: '#ffaa44' }}>{orderBook.spread ? fmtPrice(orderBook.spread) : '—'}</span>
            </span>
          </div>
          <OrderBook bids={orderBook.bids} asks={orderBook.asks} lastPrice={orderBook.lastPrice} />
          <div style={{ marginTop: 20, paddingTop: 16, borderTop: `1px solid ${COLORS.border}` }}>
            <OrderForm onSubmit={sendOrder} midPrice={orderBook.midPrice || 18942} />
          </div>
        </div>

        {/* Center: Chart + Latency */}
        <div style={{ background: '#08111a', padding: 16, overflowY: 'auto' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', marginBottom: 8 }}>
            <div>
              <span style={{ fontSize: 26, fontWeight: 700, color: priceColor, letterSpacing: '-0.02em' }}>
                {fmtPrice(orderBook.lastPrice || 18942)}
              </span>
              <span style={{ fontSize: 11, color: COLORS.dim, marginLeft: 10 }}>AAPL / USD</span>
            </div>
            <div style={{ fontSize: 10, color: COLORS.dim }}>
              MID: <span style={{ color: '#8892a4' }}>{fmtPrice(orderBook.midPrice || 18942)}</span>
            </div>
          </div>

          <PriceChart history={priceHistory} />

          <div style={{ marginTop: 14, background: COLORS.panel, borderRadius: 8, padding: 14, border: `1px solid ${COLORS.border}` }}>
            <div style={{ fontSize: 10, color: COLORS.dim, letterSpacing: '0.15em', marginBottom: 10 }}>MATCHING ENGINE LATENCY</div>
            <LatencyPanel samples={latency} />
          </div>

          <div style={{ marginTop: 10, display: 'grid', gridTemplateColumns: 'repeat(3,1fr)', gap: 8 }}>
            {[
              ['ALGO',    'Price-Time FIFO'],
              ['NETWORK', 'epoll + FIX 4.2'],
              ['ALLOC',   'Object Pool'],
            ].map(([k, v]) => (
              <div key={k} style={{ background: COLORS.panel, borderRadius: 6, padding: 10, border: `1px solid ${COLORS.border}` }}>
                <div style={{ fontSize: 8, color: '#3a4568', letterSpacing: '0.12em', marginBottom: 3 }}>{k}</div>
                <div style={{ fontSize: 11, color: '#8892a4', fontWeight: 600 }}>{v}</div>
              </div>
            ))}
          </div>
        </div>

        {/* Right: Trade Feed */}
        <div style={{ background: COLORS.panel, borderLeft: `1px solid ${COLORS.border}`, padding: 14, overflowY: 'auto' }}>
          <div style={{ fontSize: 10, color: COLORS.dim, letterSpacing: '0.15em', marginBottom: 12 }}>EXECUTION FEED</div>
          <TradesFeed trades={trades} />

          <div style={{ marginTop: 16, paddingTop: 14, borderTop: `1px solid ${COLORS.border}` }}>
            <div style={{ fontSize: 10, color: COLORS.dim, letterSpacing: '0.15em', marginBottom: 8 }}>TECH STACK</div>
            {[
              'C++20 matching engine',
              'Price-time priority FIFO',
              'Object pool allocator',
              'epoll non-blocking I/O',
              'FIX 4.2 protocol',
              '[[likely]] branch hints',
              'rdtscp latency measurement',
            ].map((item, i) => (
              <div key={i} style={{ display: 'flex', gap: 6, alignItems: 'flex-start', marginBottom: 5 }}>
                <span style={{ color: COLORS.buy, fontSize: 9, marginTop: 1 }}>▸</span>
                <span style={{ fontSize: 9, color: '#4a5a70', lineHeight: 1.4 }}>{item}</span>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  )
}
