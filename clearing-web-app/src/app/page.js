'use client'

import { useEffect, useState, useRef } from "react";
import styles from "./page.module.css";

// Symbol configuration
const SYMBOLS = {
  1: { name: "GOLD", icon: "Au" },
  2: { name: "BLUE", icon: "Bl" }
};

// Chart configuration helper
function getChartColors(symbolId) {
  const isGold = symbolId === 1 || symbolId === "1";
  return {
    bid: isGold ? "#d97706" : "#3b82f6",
    ask: isGold ? "#fbbf24" : "#60a5fa"
  };
}

function getOrCreateChart(timeSeries, symbolId) {
  const chartId = `chart-${symbolId}`;
  const canvas = document.getElementById(chartId);

  if (!canvas) {
    console.error(`Canvas with id ${chartId} not found`);
    return null;
  }

  const ctx = canvas.getContext("2d");

  if (!window.chartInstances) {
    window.chartInstances = {};
  }

  const colors = getChartColors(symbolId);

  const data = {
    datasets: [
      {
        label: "Best Bid",
        data: timeSeries.map(([timestamp, { best_bid }]) => ({
          x: new Date(timestamp),
          y: best_bid,
        })),
        borderColor: colors.bid,
        backgroundColor: colors.bid + "20",
        fill: false,
        pointRadius: 0,
        borderWidth: 2,
      },
      {
        label: "Best Ask",
        data: timeSeries.map(([timestamp, { best_ask }]) => ({
          x: new Date(timestamp),
          y: best_ask,
        })),
        borderColor: colors.ask,
        backgroundColor: colors.ask + "20",
        fill: false,
        pointRadius: 0,
        borderWidth: 2,
      },
    ],
  };

  if (window.chartInstances[chartId]) {
    const chart = window.chartInstances[chartId];
    chart.data.datasets[0].data = data.datasets[0].data;
    chart.data.datasets[1].data = data.datasets[1].data;
    chart.update('none');
    return chart;
  } else {
    const myChart = new Chart(ctx, {
      type: "line",
      data: data,
      options: {
        responsive: true,
        maintainAspectRatio: false,
        elements: {
          point: {
            radius: 0,
            hoverRadius: 4,
          },
          line: {
            tension: 0.3,
          }
        },
        scales: {
          x: {
            type: "time",
            time: {
              unit: "second",
            },
            grid: {
              color: "rgba(0, 0, 0, 0.05)",
            },
            ticks: {
              font: {
                size: 11,
              },
              color: "#64748b",
            },
          },
          y: {
            grace: '5%',
            ticks: {
              precision: 0,
              font: {
                size: 11,
              },
              color: "#64748b",
            },
            grid: {
              color: "rgba(0, 0, 0, 0.05)",
            },
            suggestedMin: Math.min(...data.datasets[0].data.map(d => d.y),
                                    ...data.datasets[1].data.map(d => d.y)) - 1,
            suggestedMax: Math.max(...data.datasets[0].data.map(d => d.y),
                                    ...data.datasets[1].data.map(d => d.y)) + 1
          },
        },
        animation: {
          duration: 300
        },
        plugins: {
          legend: {
            position: 'top',
            align: 'end',
            labels: {
              boxWidth: 12,
              boxHeight: 12,
              padding: 16,
              font: {
                size: 12,
              },
              usePointStyle: true,
              pointStyle: 'circle',
            },
          },
          tooltip: {
            mode: 'index',
            intersect: false,
            backgroundColor: 'rgba(15, 23, 42, 0.9)',
            titleFont: {
              size: 12,
            },
            bodyFont: {
              size: 12,
            },
            padding: 10,
            cornerRadius: 6,
          },
        },
      },
    });

    window.chartInstances[chartId] = myChart;
    return myChart;
  }
}

// Connection status badge component
function ConnectionBadge({ status }) {
  const statusConfig = {
    connected: { label: "Connected", className: styles.statusConnected },
    connecting: { label: "Connecting...", className: styles.statusConnecting },
    disconnected: { label: "Disconnected", className: styles.statusDisconnected },
    error: { label: "Error", className: styles.statusError },
  };

  const config = statusConfig[status] || statusConfig.disconnected;

  return (
    <div className={`${styles.connectionStatus} ${config.className}`}>
      <span className={styles.statusDot}></span>
      <span>{config.label}</span>
    </div>
  );
}

// Symbol card component
function SymbolCard({ symbol, timeSeries, symbolPositions }) {
  const symbolInfo = SYMBOLS[symbol] || { name: `Symbol ${symbol}`, icon: "?" };
  const isGold = symbol == 1;
  const cardClass = isGold ? styles.goldCard : styles.blueCard;

  return (
    <div className={`${styles.symbolCard} ${cardClass}`}>
      <div className={styles.cardHeader}>
        <div className={styles.symbolIcon}>{symbolInfo.icon}</div>
        <h2 className={styles.symbolTitle}>{symbolInfo.name}</h2>
      </div>
      <div className={styles.cardBody}>
        <div className={styles.chartSection}>
          <div className={styles.chartWrapper}>
            <canvas id={`chart-${symbol}`} className={styles.chartCanvas}></canvas>
          </div>
        </div>
        <div className={styles.positionSection}>
          <h3 className={styles.sectionTitle}>Position Data</h3>
          <div className={styles.tableWrapper}>
            {symbolPositions.length > 0 ? (
              <table className={styles.positionTable}>
                <thead>
                  <tr>
                    <th>Client</th>
                    <th>Position</th>
                    <th>P&L</th>
                    <th>Volume</th>
                  </tr>
                </thead>
                <tbody>
                  {symbolPositions.map((posData, idx) => (
                    <tr key={idx}>
                      <td>{posData.client_id}</td>
                      <td>{posData.position}</td>
                      <td>
                        <div className={`${styles.pnlCell} ${posData.pnl >= 0 ? styles.pnlPositive : styles.pnlNegative}`}>
                          <span className={styles.pnlArrow}>{posData.pnl >= 0 ? "▲" : "▼"}</span>
                          <span>{posData.pnl.toFixed(2)}</span>
                        </div>
                      </td>
                      <td>{posData.volume}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            ) : (
              <div className={styles.emptyState}>No position data available</div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

export default function Home() {
  const [symbolToTimeSeries, setSymbolToTimeSeries] = useState(new Map());
  const [positions, setPositions] = useState(new Map());
  const [currentTime, setCurrentTime] = useState(null);
  const [connectionStatus, setConnectionStatus] = useState('disconnected');
  const [errorMessage, setErrorMessage] = useState('');
  const socketRef = useRef(null);
  const reconnectTimeoutRef = useRef(null);
  const maxReconnectAttempts = 1;
  const [reconnectAttempts, setReconnectAttempts] = useState(0);

  // Update current time every second (client-side only to avoid hydration mismatch)
  useEffect(() => {
    setCurrentTime(new Date());
    const timer = setInterval(() => {
      setCurrentTime(new Date());
    }, 1000);
    return () => clearInterval(timer);
  }, []);

  const getWebSocketUrl = () => {
    const wsHost = process.env.NEXT_PUBLIC_WS_HOST || (typeof window !== 'undefined' ? window.location.hostname : 'localhost');
    const wsPort = process.env.NEXT_PUBLIC_WS_PORT || '9002';
    return { wsHost, wsPort, url: `ws://${wsHost}:${wsPort}` };
  };

  const connectWebSocket = () => {
    if (socketRef.current && socketRef.current.readyState !== WebSocket.CLOSED) {
      socketRef.current.close();
    }

    const { wsHost, wsPort, url } = getWebSocketUrl();

    try {
      console.log(`Attempting to connect to WebSocket (Attempt ${reconnectAttempts + 1}/${maxReconnectAttempts})`);
      setConnectionStatus('connecting');

      socketRef.current = new WebSocket(url);

      socketRef.current.addEventListener("open", event => {
        console.log("Connection established");
        setConnectionStatus('connected');
        setErrorMessage('');
        setReconnectAttempts(0);
      });

      socketRef.current.addEventListener("error", event => {
        console.error("WebSocket error:", event);
        setConnectionStatus('error');
        setErrorMessage(`Connection failed. Check if server is running at ws://${wsHost}:${wsPort}`);

        if (reconnectAttempts < maxReconnectAttempts) {
          clearTimeout(reconnectTimeoutRef.current);
          reconnectTimeoutRef.current = setTimeout(() => {
            setReconnectAttempts(prev => prev + 1);
            connectWebSocket();
          }, 2000);
        }
      });

      socketRef.current.addEventListener("message", event => {
        const stots = symbolToTimeSeries;
        const message = JSON.parse(event.data)
        const { timestamp, snapshot, positions } = message

        if (positions) {
          setPositions(positions);
        }

        for (const sym in snapshot) {
          const { symbol, best_bid, best_ask } = snapshot[sym]

          if (!stots.has(symbol)) {
            stots.set(symbol, [])
          }

          stots.get(symbol).push([timestamp/ 1000000, { best_bid, best_ask }])
        }

        for (const symbol of stots.keys()) {
          const timeSeries = stots.get(symbol)
          if (timeSeries.length > 1000) {
            timeSeries.shift()
          }
        }

        setSymbolToTimeSeries(new Map(stots));
      });

      socketRef.current.addEventListener("close", event => {
        console.log("Connection closed");
        setConnectionStatus('disconnected');
      });
    } catch (err) {
      console.error("Error creating WebSocket:", err);
      setConnectionStatus('error');
      setErrorMessage(`Failed to create WebSocket: ${err.message}`);
    }
  };

  const chartJsLoadedRef = useRef(false);

  useEffect(() => {
    if (typeof Chart !== 'undefined' && chartJsLoadedRef.current) {
      return;
    }

    const loadChartJs = async () => {
      try {
        const chartScript = document.createElement('script');
        chartScript.src = 'https://cdn.jsdelivr.net/npm/chart.js';
        chartScript.async = true;

        const adapterScript = document.createElement('script');
        adapterScript.src = 'https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns';
        adapterScript.async = true;

        await new Promise((resolve) => {
          chartScript.onload = resolve;
          document.body.appendChild(chartScript);
        });

        await new Promise((resolve) => {
          adapterScript.onload = resolve;
          document.body.appendChild(adapterScript);
        });

        console.log('Chart.js and date adapter loaded');
        chartJsLoadedRef.current = true;
        setSymbolToTimeSeries(symbolToTimeSeries);
      } catch (error) {
        console.error('Failed to load Chart.js or adapter:', error);
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

  useEffect(() => {
    if (!chartJsLoadedRef.current || typeof Chart === 'undefined') {
      return;
    }

    const timer = setTimeout(() => {
      symbolToTimeSeries.forEach((timeSeries, symbol) => {
        if (timeSeries.length > 0) {
          getOrCreateChart(timeSeries, symbol);
        }
      });
    }, 20);

    return () => clearTimeout(timer);
  }, [symbolToTimeSeries]);

  useEffect(() => {
    connectWebSocket();

    return () => {
      if (socketRef.current) {
        socketRef.current.close();
        socketRef.current = null;
      }
      clearTimeout(reconnectTimeoutRef.current);
    };
  }, []);

  const formatTime = (date) => {
    return date.toLocaleTimeString('en-US', {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
  };

  return (
    <div className={styles.page}>
      {/* Header */}
      <header className={styles.header}>
        <div className={`${styles.container} ${styles.headerContent}`}>
          <div className={styles.logo}>
            <div className={styles.logoIcon}>NX</div>
            <h1 className={styles.title}>NDFEX Scoreboard</h1>
          </div>
          <div className={styles.headerRight}>
            {currentTime && <span className={styles.timestamp}>{formatTime(currentTime)}</span>}
            <ConnectionBadge status={connectionStatus} />
          </div>
        </div>
      </header>

      {/* Main Content */}
      <main className={styles.main}>
        <div className={styles.container}>
          {/* Error Alert */}
          {errorMessage && (
            <div className={styles.alertBox}>
              <svg className={styles.alertIcon} width="20" height="20" viewBox="0 0 20 20" fill="currentColor">
                <path fillRule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zM8.28 7.22a.75.75 0 00-1.06 1.06L8.94 10l-1.72 1.72a.75.75 0 101.06 1.06L10 11.06l1.72 1.72a.75.75 0 101.06-1.06L11.06 10l1.72-1.72a.75.75 0 00-1.06-1.06L10 8.94 8.28 7.22z" clipRule="evenodd" />
              </svg>
              <span className={styles.alertMessage}>{errorMessage}</span>
            </div>
          )}

          {/* Reconnect Info */}
          {reconnectAttempts > 0 && (
            <div className={styles.reconnectInfo}>
              Reconnection attempts: {reconnectAttempts}/{maxReconnectAttempts}
            </div>
          )}

          {/* Retry Button */}
          {connectionStatus === 'error' && reconnectAttempts >= maxReconnectAttempts && (
            <button
              className={styles.retryButton}
              onClick={() => {
                setReconnectAttempts(0);
                connectWebSocket();
              }}
            >
              <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor">
                <path fillRule="evenodd" d="M8 3a5 5 0 104.546 2.914.5.5 0 01.908-.418A6 6 0 118 2v1z" clipRule="evenodd" />
                <path d="M8 4.466V.534a.25.25 0 01.41-.192l2.36 1.966c.12.1.12.284 0 .384L8.41 4.658A.25.25 0 018 4.466z" />
              </svg>
              Try Again
            </button>
          )}

          {/* Symbol Cards */}
          {symbolToTimeSeries.size > 0 ? (
            <div className={styles.symbolGrid}>
              {Array.from(symbolToTimeSeries.entries()).map(([symbol, timeSeries]) => {
                const symbolPositions = positions && Array.isArray(positions)
                  ? positions.filter(p => p.symbol === parseInt(symbol))
                  : [];

                return (
                  <SymbolCard
                    key={symbol}
                    symbol={symbol}
                    timeSeries={timeSeries}
                    symbolPositions={symbolPositions}
                  />
                );
              })}
            </div>
          ) : (
            connectionStatus === 'connected' && (
              <div className={styles.loadingState}>
                <div className={styles.loadingSpinner}></div>
                <div className={styles.loadingText}>Waiting for market data...</div>
              </div>
            )
          )}
        </div>
      </main>
    </div>
  );
}
