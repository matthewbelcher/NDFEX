'use client'

import { useEffect, useState, useRef, useMemo, useCallback } from "react";

// Display names for each symbol id the exchange trades.
// Must match matching_engine/matching_engine.cpp.
const SYMBOL_NAMES = {
  1: "GOLD",
  2: "BLUE",
  3: "KNAN",  // Keenan Hall
  4: "STED",  // St. Edward's Hall
  5: "FISH",  // Fisher Hall
  6: "DILN",  // Dillon Hall
  7: "SORN",  // Sorin Hall
  8: "RYAN",  // Ryan Hall
  9: "LYON",  // Lyons Hall
  10: "WLSH", // Walsh Hall
  11: "LEWI", // Lewis Hall
  12: "BDIN", // Badin Hall
  13: "UNDY", // Notre Dame Dorm ETF
};

// Map client_id to display name (from matching_engine/users.txt)
const TEAM_NAMES = {
  1: "Team 1",
  2: "Team 2",
  3: "Team 3",
  4: "Team 4",
  5: "Team 5",
  6: "Team 6",
  7: "Group 7",
  8: "Group 8",
  9: "Team 9",
  10: "JEVT",
  11: "HFT",
  95: "RuleBreaker",
};

// Filter out test accounts
const HIDDEN_CLIENTS = new Set([98, 99]);

// Logical symbol groupings for chart layout
const SYMBOL_GROUPS = [
  { label: "Indices", symbols: [1, 2] },
  { label: "Dorms", symbols: [3, 4, 5, 6, 7, 8, 9, 10, 11, 12] },
  { label: "ETF", symbols: [13] },
];

// Stable color per team
const TEAM_COLORS = {
  1: "#FF6384",
  2: "#36A2EB",
  3: "#FFCE56",
  4: "#4BC0C0",
  5: "#9966FF",
  6: "#FF9F40",
  7: "#C9CBCF",
  8: "#7BC8A4",
  9: "#E7E9ED",
  10: "#F77825",
  11: "#5DA5DA",
  95: "#FF4444",
};

// All symbol IDs sorted
const ALL_SYMBOLS = Object.keys(SYMBOL_NAMES).map(Number).sort((a, b) => a - b);

function getOrCreateChart(timeSeries, symbolId) {
  const chartId = `chart-${symbolId}`;
  const canvas = document.getElementById(chartId);
  if (!canvas) return null;

  const ctx = canvas.getContext("2d");
  if (!window.chartInstances) window.chartInstances = {};

  const bidData = timeSeries.map(([timestamp, { best_bid }]) => ({
    x: new Date(timestamp),
    y: best_bid,
  }));
  const askData = timeSeries.map(([timestamp, { best_ask }]) => ({
    x: new Date(timestamp),
    y: best_ask,
  }));
  const volData = timeSeries.map(([timestamp, { volume }]) => ({
    x: new Date(timestamp),
    y: volume || 0,
  }));

  if (window.chartInstances[chartId]) {
    const chart = window.chartInstances[chartId];
    chart.data.datasets[0].data = bidData;
    chart.data.datasets[1].data = askData;
    chart.data.datasets[2].data = volData;
    chart.update('none');
    return chart;
  }

  const myChart = new Chart(ctx, {
    type: "line",
    data: {
      datasets: [
        {
          label: "Bid",
          data: bidData,
          borderColor: "#42A5F5",
          borderWidth: 1.5,
          fill: false,
          pointRadius: 0,
          yAxisID: 'y',
        },
        {
          label: "Ask",
          data: askData,
          borderColor: "#EF5350",
          borderWidth: 1.5,
          fill: false,
          pointRadius: 0,
          yAxisID: 'y',
        },
        {
          label: "Volume",
          data: volData,
          type: 'bar',
          backgroundColor: "rgba(100, 149, 237, 0.3)",
          borderColor: "rgba(100, 149, 237, 0.5)",
          borderWidth: 1,
          yAxisID: 'y1',
          barPercentage: 0.8,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      elements: {
        point: { radius: 0, hoverRadius: 3 },
        line: { tension: 0.1 },
      },
      scales: {
        x: {
          type: "time",
          time: { unit: "second" },
          grid: { color: "rgba(255,255,255,0.08)" },
          ticks: { color: "#999", maxTicksLimit: 6, font: { size: 10 } },
        },
        y: {
          position: 'left',
          grace: '5%',
          ticks: { precision: 0, color: "#ccc", font: { size: 10 } },
          grid: { color: "rgba(255,255,255,0.08)" },
        },
        y1: {
          position: 'right',
          beginAtZero: true,
          suggestedMax: Math.max(1, ...volData.map(d => d.y)) * 5,
          grid: { drawOnChartArea: false },
          ticks: { display: false },
        },
      },
      plugins: {
        legend: { display: false },
        tooltip: { mode: 'index', intersect: false },
      },
    },
  });

  window.chartInstances[chartId] = myChart;
  return myChart;
}

export default function Home() {
  const [symbolToTimeSeries, setSymbolToTimeSeries] = useState(new Map());
  const [positions, setPositions] = useState([]);
  const [connectionStatus, setConnectionStatus] = useState('disconnected');
  const [errorMessage, setErrorMessage] = useState('');
  const socketRef = useRef(null);
  const reconnectTimeoutRef = useRef(null);
  const maxReconnectAttempts = 5;
  const [reconnectAttempts, setReconnectAttempts] = useState(0);
  const lastChartUpdateRef = useRef(0);
  const pendingUpdateRef = useRef(null);
  const lastSeqNumRef = useRef(0);

  // Derived state: aggregate PnL/volume per team, sorted by PnL descending
  const teamAggregates = useMemo(() => {
    if (!Array.isArray(positions) || positions.length === 0) return [];
    const agg = {};
    for (const p of positions) {
      if (HIDDEN_CLIENTS.has(p.client_id)) continue;
      if (!agg[p.client_id]) {
        agg[p.client_id] = {
          clientId: p.client_id,
          totalPnl: 0,
          // Live (would-be) total PnL with the existing per-symbol clamping
          // still applied but BEFORE the drawdown freeze override. Stays in
          // sync with totalPnl on non-PnL-breached teams; diverges from
          // frozenPnl on teams that hit the −5000 floor.
          liveTotalPnl: 0,
          // Per-symbol unclamped PnL summed across all symbols — what the
          // total would be if neither the position-limit clamp nor the
          // drawdown freeze applied.
          unclampedTotalPnl: 0,
          totalVolume: 0,
          hasPositionBreach: false,
          hasPnlBreach: false,
          frozenPnl: null,
        };
      }
      agg[p.client_id].totalPnl += p.pnl || 0;
      agg[p.client_id].liveTotalPnl += p.pnl || 0;
      // unclamped_pnl is only present on rows where the per-symbol position
      // limit was breached; for all other symbols the unclamped value equals
      // the locked one.
      const unclamped = p.unclamped_pnl !== undefined ? p.unclamped_pnl : (p.pnl || 0);
      agg[p.client_id].unclampedTotalPnl += unclamped;
      agg[p.client_id].totalVolume += p.volume || 0;
      if (p.position_breach) agg[p.client_id].hasPositionBreach = true;
      if (p.pnl_breach) {
        agg[p.client_id].hasPnlBreach = true;
        if (p.frozen_pnl !== undefined) agg[p.client_id].frozenPnl = p.frozen_pnl;
      }
    }
    // Use frozen PnL for breached teams (totalPnl only — liveTotalPnl stays
    // at the unfrozen sum so the leaderboard can show "would be …" hint).
    for (const a of Object.values(agg)) {
      if (a.hasPnlBreach && a.frozenPnl !== null) {
        a.totalPnl = a.frozenPnl;
      }
    }
    return Object.values(agg).sort((a, b) => b.totalPnl - a.totalPnl);
  }, [positions]);

  // Derived state: positions indexed by symbol
  const positionsBySymbol = useMemo(() => {
    if (!Array.isArray(positions)) return {};
    const bySymbol = {};
    for (const p of positions) {
      if (HIDDEN_CLIENTS.has(p.client_id)) continue;
      const sym = p.symbol;
      if (!bySymbol[sym]) bySymbol[sym] = [];
      bySymbol[sym].push(p);
    }
    return bySymbol;
  }, [positions]);

  // Derived state: position lookup map (client_id -> symbol -> position data)
  const positionMatrix = useMemo(() => {
    if (!Array.isArray(positions)) return {};
    const matrix = {};
    for (const p of positions) {
      if (HIDDEN_CLIENTS.has(p.client_id)) continue;
      if (!matrix[p.client_id]) matrix[p.client_id] = {};
      matrix[p.client_id][p.symbol] = p;
    }
    return matrix;
  }, [positions]);

  const triggerChartUpdate = useCallback(() => {
    const now = Date.now();
    if (now - lastChartUpdateRef.current < 500) {
      // Throttle: schedule an update for later
      if (!pendingUpdateRef.current) {
        pendingUpdateRef.current = setTimeout(() => {
          pendingUpdateRef.current = null;
          lastChartUpdateRef.current = Date.now();
          // Force re-render by setting state
          setSymbolToTimeSeries(prev => new Map(prev));
        }, 500 - (now - lastChartUpdateRef.current));
      }
      return;
    }
    lastChartUpdateRef.current = now;
    setSymbolToTimeSeries(prev => new Map(prev));
  }, []);

  const connectWebSocket = useCallback(() => {
    if (socketRef.current && socketRef.current.readyState !== WebSocket.CLOSED) {
      socketRef.current.close();
    }

    try {
      setConnectionStatus('connecting');
      const wsHost = process.env.NEXT_PUBLIC_WS_HOST || (typeof window !== 'undefined' ? window.location.hostname : 'localhost') || 'localhost';
      const wsPort = process.env.NEXT_PUBLIC_WS_PORT || '9002';
      socketRef.current = new WebSocket(`ws://${wsHost}:${wsPort}`);

      socketRef.current.addEventListener("open", () => {
        setConnectionStatus('connected');
        setErrorMessage('');
        setReconnectAttempts(0);
      });

      socketRef.current.addEventListener("error", () => {
        setConnectionStatus('error');
        const host = process.env.NEXT_PUBLIC_WS_HOST || (typeof window !== 'undefined' ? window.location.hostname : 'localhost') || 'localhost';
        const port = process.env.NEXT_PUBLIC_WS_PORT || '9002';
        setErrorMessage(`Connection failed. Check if server is running at ws://${host}:${port}`);
      });

      socketRef.current.addEventListener("message", event => {
        const message = JSON.parse(event.data);
        const { timestamp, snapshot, positions: posData, trades } = message;

        if (posData) {
          setPositions(posData);
        }

        // Aggregate trade volume per symbol, only counting new trades since last message
        const tradeVolume = {};
        if (trades && trades.length > 0) {
          let maxSeq = lastSeqNumRef.current;
          for (const trade of trades) {
            if (trade.seq_num > lastSeqNumRef.current) {
              const { symbol, quantity } = trade;
              tradeVolume[symbol] = (tradeVolume[symbol] || 0) + (quantity || 1);
              if (trade.seq_num > maxSeq) maxSeq = trade.seq_num;
            }
          }
          lastSeqNumRef.current = maxSeq;
        }

        // Update time series using functional setState to avoid stale closures
        setSymbolToTimeSeries(prev => {
          const stots = new Map(prev);
          for (const sym in snapshot) {
            const { symbol, best_bid, best_ask } = snapshot[sym];
            if (!stots.has(symbol)) stots.set(symbol, []);
            const ts = stots.get(symbol);
            ts.push([timestamp / 1000000, { best_bid, best_ask, volume: tradeVolume[symbol] || 0 }]);
            if (ts.length > 1000) ts.shift();
          }
          return stots;
        });
      });

      socketRef.current.addEventListener("close", () => {
        setConnectionStatus('disconnected');
        // Auto-reconnect
        setReconnectAttempts(prev => {
          if (prev < maxReconnectAttempts) {
            reconnectTimeoutRef.current = setTimeout(() => {
              connectWebSocket();
            }, 2000);
          }
          return prev + 1;
        });
      });
    } catch (err) {
      setConnectionStatus('error');
      setErrorMessage(`Failed to create WebSocket: ${err.message}`);
    }
  }, []);

  // Load Chart.js
  const chartJsLoadedRef = useRef(false);
  useEffect(() => {
    if (typeof Chart !== 'undefined' && chartJsLoadedRef.current) return;
    const loadChartJs = async () => {
      try {
        const chartScript = document.createElement('script');
        chartScript.src = 'https://cdn.jsdelivr.net/npm/chart.js';
        chartScript.async = true;
        await new Promise((resolve) => {
          chartScript.onload = resolve;
          document.body.appendChild(chartScript);
        });
        const adapterScript = document.createElement('script');
        adapterScript.src = 'https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns';
        adapterScript.async = true;
        await new Promise((resolve) => {
          adapterScript.onload = resolve;
          document.body.appendChild(adapterScript);
        });
        chartJsLoadedRef.current = true;
        setSymbolToTimeSeries(prev => new Map(prev));
      } catch (error) {
        console.error('Failed to load Chart.js:', error);
      }
    };
    loadChartJs();
    return () => {
      if (window.chartInstances) {
        Object.values(window.chartInstances).forEach(chart => chart.destroy());
        window.chartInstances = {};
      }
    };
  }, []);

  // Update charts when data changes
  useEffect(() => {
    if (!chartJsLoadedRef.current || typeof Chart === 'undefined') return;
    const timer = setTimeout(() => {
      symbolToTimeSeries.forEach((timeSeries, symbol) => {
        if (timeSeries.length > 0) {
          getOrCreateChart(timeSeries, symbol);
        }
      });
    }, 20);
    return () => clearTimeout(timer);
  }, [symbolToTimeSeries]);

  // Connect WebSocket on mount
  useEffect(() => {
    connectWebSocket();
    return () => {
      if (socketRef.current) {
        socketRef.current.close();
        socketRef.current = null;
      }
      clearTimeout(reconnectTimeoutRef.current);
      if (pendingUpdateRef.current) clearTimeout(pendingUpdateRef.current);
    };
  }, []);

  // Get latest bid/ask for a symbol
  const getLatestPrice = (symbolId) => {
    const ts = symbolToTimeSeries.get(symbolId);
    if (!ts || ts.length === 0) return null;
    return ts[ts.length - 1][1];
  };

  return (
    <div style={{ padding: '16px 24px', minHeight: '100vh' }}>
      {/* Header Bar */}
      <div style={{
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        marginBottom: 24,
        borderBottom: '1px solid #333',
        paddingBottom: 12,
      }}>
        <h1 style={{ fontSize: 28, fontWeight: 700, letterSpacing: '-0.5px' }}>NDFEX Scoreboard</h1>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <div style={{
            width: 10,
            height: 10,
            borderRadius: '50%',
            backgroundColor: connectionStatus === 'connected' ? '#4CAF50' : '#EF5350',
          }} />
          <span style={{
            fontSize: 14,
            fontWeight: 600,
            color: connectionStatus === 'connected' ? '#4CAF50' : '#EF5350',
            fontFamily: 'var(--font-geist-mono), monospace',
          }}>
            {connectionStatus === 'connected' ? 'LIVE' : 'DISCONNECTED'}
          </span>
          {connectionStatus === 'error' && reconnectAttempts >= maxReconnectAttempts && (
            <button
              onClick={() => { setReconnectAttempts(0); connectWebSocket(); }}
              style={{
                marginLeft: 8,
                padding: '4px 12px',
                backgroundColor: '#333',
                color: '#fff',
                border: '1px solid #555',
                borderRadius: 4,
                cursor: 'pointer',
                fontSize: 12,
              }}
            >
              Retry
            </button>
          )}
        </div>
      </div>
      {errorMessage && (
        <div style={{ color: '#EF5350', fontSize: 13, marginBottom: 12 }}>{errorMessage}</div>
      )}

      {/* Section 1: Leaderboard Table */}
      <div style={{ marginBottom: 32 }}>
        <h2 style={{ fontSize: 20, marginBottom: 12, color: '#ccc' }}>Leaderboard</h2>
        <table style={{
          width: '100%',
          borderCollapse: 'collapse',
          fontSize: 20,
          fontFamily: 'var(--font-geist-mono), monospace',
        }}>
          <thead>
            <tr style={{ borderBottom: '2px solid #444' }}>
              <th style={{ padding: '10px 16px', textAlign: 'left', color: '#999', fontSize: 14, fontWeight: 600 }}>RANK</th>
              <th style={{ padding: '10px 16px', textAlign: 'left', color: '#999', fontSize: 14, fontWeight: 600 }}>TEAM</th>
              <th style={{ padding: '10px 16px', textAlign: 'right', color: '#999', fontSize: 14, fontWeight: 600 }}>TOTAL PNL</th>
              <th style={{ padding: '10px 16px', textAlign: 'right', color: '#999', fontSize: 14, fontWeight: 600 }}>VOLUME</th>
            </tr>
          </thead>
          <tbody>
            {teamAggregates.map((team, idx) => {
              const rank = idx + 1;
              const isTop3 = rank <= 3;
              const medalColors = { 1: '#FFD700', 2: '#C0C0C0', 3: '#CD7F32' };
              return (
                <tr key={team.clientId} style={{
                  borderBottom: '1px solid #222',
                  backgroundColor: isTop3 ? 'rgba(255,215,0,0.04)' : 'transparent',
                }}>
                  <td style={{
                    padding: '10px 16px',
                    fontWeight: 700,
                    fontSize: 22,
                    color: medalColors[rank] || '#666',
                  }}>
                    {rank}
                  </td>
                  <td style={{
                    padding: '10px 16px',
                    fontWeight: 600,
                    fontSize: 20,
                    color: isTop3 ? '#fff' : '#ccc',
                  }}>
                    <span style={{
                      display: 'inline-block',
                      width: 10,
                      height: 10,
                      borderRadius: '50%',
                      backgroundColor: TEAM_COLORS[team.clientId] || '#666',
                      marginRight: 10,
                    }} />
                    {TEAM_NAMES[team.clientId] || `Client ${team.clientId}`}
                    {team.hasPositionBreach && (
                      <span style={{
                        marginLeft: 8,
                        padding: '2px 6px',
                        fontSize: 11,
                        fontWeight: 700,
                        backgroundColor: 'rgba(255, 152, 0, 0.2)',
                        color: '#FF9800',
                        borderRadius: 3,
                        border: '1px solid rgba(255, 152, 0, 0.4)',
                      }}>POS</span>
                    )}
                    {team.hasPnlBreach && (
                      <span style={{
                        marginLeft: 8,
                        padding: '2px 6px',
                        fontSize: 11,
                        fontWeight: 700,
                        backgroundColor: 'rgba(239, 83, 80, 0.2)',
                        color: '#EF5350',
                        borderRadius: 3,
                        border: '1px solid rgba(239, 83, 80, 0.4)',
                      }}>PNL</span>
                    )}
                    <span style={{
                      marginLeft: 8,
                      padding: '2px 6px',
                      fontSize: 11,
                      fontWeight: 700,
                      backgroundColor: team.totalVolume >= 2000
                        ? 'rgba(66, 165, 245, 0.2)'
                        : 'rgba(150, 150, 150, 0.2)',
                      color: team.totalVolume >= 2000
                        ? '#42A5F5'
                        : '#888',
                      borderRadius: 3,
                      border: team.totalVolume >= 2000
                        ? '1px solid rgba(66, 165, 245, 0.4)'
                        : '1px solid rgba(150, 150, 150, 0.4)',
                    }}>VOL</span>
                  </td>
                  <td style={{
                    padding: '10px 16px',
                    textAlign: 'right',
                    fontWeight: 700,
                    fontSize: 22,
                    color: team.totalPnl >= 0 ? '#42A5F5' : '#EF5350',
                  }}>
                    {team.totalPnl >= 0 ? '+' : ''}{team.totalPnl.toFixed(2)}
                    {/*
                      Two yellow "would have been" hints, mutually exclusive:
                      - PnL drawdown freeze: show the live (unfrozen) running total.
                      - Position limit (no freeze): show the per-symbol unclamped total.
                      The drawdown hint takes precedence since it's the cap that
                      actually overrides totalPnl in the aggregator.
                    */}
                    {team.hasPnlBreach
                      && Math.abs(team.liveTotalPnl - team.totalPnl) > 0.005 && (
                      <span style={{
                        marginLeft: 6,
                        fontSize: 13,
                        fontWeight: 500,
                        color: '#FF9800',
                      }}>
                        ({team.liveTotalPnl >= 0 ? '+' : ''}{team.liveTotalPnl.toFixed(2)})
                      </span>
                    )}
                    {!team.hasPnlBreach && team.hasPositionBreach
                      && Math.abs(team.unclampedTotalPnl - team.totalPnl) > 0.005 && (
                      <span style={{
                        marginLeft: 6,
                        fontSize: 13,
                        fontWeight: 500,
                        color: '#FF9800',
                      }}>
                        ({team.unclampedTotalPnl >= 0 ? '+' : ''}{team.unclampedTotalPnl.toFixed(2)})
                      </span>
                    )}
                  </td>
                  <td style={{
                    padding: '10px 16px',
                    textAlign: 'right',
                    fontSize: 18,
                    color: '#999',
                  }}>
                    {team.totalVolume.toLocaleString()}
                  </td>
                </tr>
              );
            })}
            {teamAggregates.length === 0 && (
              <tr>
                <td colSpan={4} style={{ padding: '20px 16px', color: '#666', textAlign: 'center', fontSize: 16 }}>
                  Waiting for position data...
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>

      {/* Section 2: Position Matrix */}
      <div style={{ marginBottom: 32, overflowX: 'auto' }}>
        <h2 style={{ fontSize: 20, marginBottom: 12, color: '#ccc' }}>Position Matrix</h2>
        <table style={{
          borderCollapse: 'collapse',
          fontSize: 13,
          fontFamily: 'var(--font-geist-mono), monospace',
          width: '100%',
        }}>
          <thead>
            <tr style={{ borderBottom: '2px solid #444' }}>
              <th style={{
                padding: '6px 10px',
                textAlign: 'left',
                color: '#999',
                fontSize: 11,
                fontWeight: 600,
                position: 'sticky',
                left: 0,
                backgroundColor: '#0a0a0a',
                zIndex: 1,
                minWidth: 90,
              }}>TEAM</th>
              {ALL_SYMBOLS.map(sym => (
                <th key={sym} style={{
                  padding: '6px 6px',
                  textAlign: 'center',
                  color: '#999',
                  fontSize: 11,
                  fontWeight: 600,
                  minWidth: 52,
                }}>
                  {SYMBOL_NAMES[sym]}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {teamAggregates.map((team) => (
              <tr key={team.clientId} style={{ borderBottom: '1px solid #1a1a1a' }}>
                <td style={{
                  padding: '5px 10px',
                  fontWeight: 600,
                  color: '#ccc',
                  position: 'sticky',
                  left: 0,
                  backgroundColor: '#0a0a0a',
                  zIndex: 1,
                  fontSize: 12,
                }}>
                  {TEAM_NAMES[team.clientId] || `Client ${team.clientId}`}
                </td>
                {ALL_SYMBOLS.map(sym => {
                  const posData = positionMatrix[team.clientId]?.[sym];
                  const pos = posData?.position || 0;
                  const symPosBreach = posData?.position_breach || false;
                  let bgColor = 'transparent';
                  let textColor = '#555';
                  let display = '\u2014'; // em dash
                  if (symPosBreach) {
                    bgColor = 'rgba(255, 152, 0, 0.25)';
                    textColor = '#FF9800';
                    display = pos > 0 ? `+${pos}` : pos < 0 ? `${pos}` : '\u2014';
                  } else if (pos > 0) {
                    bgColor = 'rgba(76, 175, 80, 0.15)';
                    textColor = '#4CAF50';
                    display = `+${pos}`;
                  } else if (pos < 0) {
                    bgColor = 'rgba(239, 83, 80, 0.15)';
                    textColor = '#EF5350';
                    display = `${pos}`;
                  }
                  return (
                    <td key={sym} style={{
                      padding: '5px 6px',
                      textAlign: 'center',
                      backgroundColor: bgColor,
                      color: textColor,
                      fontWeight: pos !== 0 ? 600 : 400,
                      fontSize: 12,
                    }}>
                      {display}
                    </td>
                  );
                })}
              </tr>
            ))}
            {teamAggregates.length === 0 && (
              <tr>
                <td colSpan={ALL_SYMBOLS.length + 1} style={{ padding: '16px', color: '#666', textAlign: 'center' }}>
                  Waiting for position data...
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>

      {/* Section 3: Market Charts */}
      {SYMBOL_GROUPS.map(group => {
        // Only render groups that have data or always render for structure
        return (
          <div key={group.label} style={{ marginBottom: 28 }}>
            <h2 style={{ fontSize: 18, marginBottom: 12, color: '#888', borderBottom: '1px solid #222', paddingBottom: 6 }}>
              {group.label}
            </h2>
            <div style={{
              display: 'grid',
              gridTemplateColumns: 'repeat(auto-fill, minmax(400px, 1fr))',
              gap: 16,
            }}>
              {group.symbols.map(symbolId => {
                const latestPrice = getLatestPrice(symbolId);
                const symPositions = (positionsBySymbol[symbolId] || [])
                  .sort((a, b) => Math.abs(b.position) - Math.abs(a.position))
                  .slice(0, 6);

                return (
                  <div key={symbolId} style={{
                    backgroundColor: '#111',
                    borderRadius: 8,
                    padding: 12,
                    border: '1px solid #222',
                  }}>
                    {/* Chart Header */}
                    <div style={{
                      display: 'flex',
                      justifyContent: 'space-between',
                      alignItems: 'baseline',
                      marginBottom: 8,
                    }}>
                      <span style={{ fontSize: 16, fontWeight: 700, color: '#eee' }}>
                        {SYMBOL_NAMES[symbolId]}
                      </span>
                      {latestPrice && (
                        <span style={{
                          fontSize: 13,
                          fontFamily: 'var(--font-geist-mono), monospace',
                          color: '#888',
                        }}>
                          <span style={{ color: '#42A5F5' }}>{latestPrice.best_bid}</span>
                          {' / '}
                          <span style={{ color: '#EF5350' }}>{latestPrice.best_ask}</span>
                        </span>
                      )}
                    </div>

                    {/* Chart Canvas */}
                    <div style={{ height: 180 }}>
                      <canvas id={`chart-${symbolId}`} />
                    </div>

                    {/* Mini Position Grid */}
                    {symPositions.length > 0 && (
                      <div style={{
                        marginTop: 8,
                        display: 'grid',
                        gridTemplateColumns: 'repeat(3, 1fr)',
                        gap: '2px 8px',
                        fontSize: 11,
                        fontFamily: 'var(--font-geist-mono), monospace',
                      }}>
                        {symPositions.map(p => (
                          <div key={p.client_id} style={{
                            display: 'flex',
                            justifyContent: 'space-between',
                            padding: '2px 4px',
                            borderRadius: 2,
                            backgroundColor: p.position > 0
                              ? 'rgba(76, 175, 80, 0.1)'
                              : p.position < 0
                                ? 'rgba(239, 83, 80, 0.1)'
                                : 'transparent',
                          }}>
                            <span style={{ color: '#888' }}>
                              {TEAM_NAMES[p.client_id] || `C${p.client_id}`}
                            </span>
                            <span style={{
                              color: p.position > 0 ? '#4CAF50' : p.position < 0 ? '#EF5350' : '#555',
                              fontWeight: 600,
                            }}>
                              {p.position > 0 ? '+' : ''}{p.position}
                            </span>
                          </div>
                        ))}
                      </div>
                    )}
                  </div>
                );
              })}
            </div>
          </div>
        );
      })}
    </div>
  );
}
