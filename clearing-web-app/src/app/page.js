'use client'

import { useEffect, useState, useRef } from "react";

// Don't call this function directly from render
// Instead, we'll use useEffect to call it after the component renders
function getOrCreateChart(timeSeries, symbolId) {
  // Create a unique chart ID for each symbol
  const chartId = `chart-${symbolId}`;
  const canvas = document.getElementById(chartId);

  if (!canvas) {
    console.error(`Canvas with id ${chartId} not found`);
    return null;
  }

  const ctx = canvas.getContext("2d");

  // Initialize chart instances object if it doesn't exist
  if (!window.chartInstances) {
    window.chartInstances = {};
  }

  const data = {
    datasets: [
      {
        label: "Best Bid Price",
        data: timeSeries.map(([timestamp, { best_bid }]) => ({
          x: new Date(timestamp),
          y: best_bid,
        })),
        borderColor: "#6495ED", // Cornflower blue - paler shade
        fill: false,
        pointRadius: 0, // Remove circles at data points
      },
      {
        label: "Best Ask Price",
        data: timeSeries.map(([timestamp, { best_ask }]) => ({
          x: new Date(timestamp),
          y: best_ask,
        })),
        borderColor: "#FF9999", // Light coral - paler shade
        fill: false,
        pointRadius: 0, // Remove circles at data points
      },
    ],
  };

  // Check if a chart already exists for this canvas
  if (window.chartInstances[chartId]) {
    // Update existing chart with new data
    const chart = window.chartInstances[chartId];
    chart.data.datasets[0].data = data.datasets[0].data;
    chart.data.datasets[1].data = data.datasets[1].data;
    chart.update('none'); // 'none' disables animations during update for better performance
    return chart;
  } else {
    // Create a new chart if one doesn't exist
    const myChart = new Chart(ctx, {
      type: "line",
      data: data,
      options: {
        elements: {
          point: {
            radius: 0, // Remove all points globally
            hoverRadius: 5, // Show points on hover for better interaction
          },
          line: {
            tension: 0.2, // Slightly smooth the line for better appearance
          }
        },
        scales: {
          x: {
            type: "time",
            time: {
              unit: "second",
            },
          },
          y: {
            // Remove beginAtZero to allow dynamic scaling
            // Add configuration for dynamic Y axis
            grace: '5%', // Add some padding above and below data points
            ticks: {
              precision: 0 // Use integer precision for price values
            },
            // Use suggested min/max for better auto-scaling
            suggestedMin: Math.min(...data.datasets[0].data.map(d => d.y),
                                    ...data.datasets[1].data.map(d => d.y)) - 1,
            suggestedMax: Math.max(...data.datasets[0].data.map(d => d.y),
                                    ...data.datasets[1].data.map(d => d.y)) + 1
          },
        },
        animation: {
          duration: 500 // Faster animations for better performance
        },
        plugins: {
          legend: {
            position: 'top',
          },
          tooltip: {
            mode: 'index',
            intersect: false,
          },
        },
      },
    });

    // Store the chart instance for future reference
    window.chartInstances[chartId] = myChart;
    return myChart;
  }
}

export default function Home() {
  // use a state variable to trigger a re-render when the time series is updated
  const [symbolToTimeSeries, setSymbolToTimeSeries] = useState(new Map());
  const [positions, setPositions] = useState(new Map());

  const [connectionStatus, setConnectionStatus] = useState('disconnected');
  const [errorMessage, setErrorMessage] = useState('');
  const socketRef = useRef(null);
  const reconnectTimeoutRef = useRef(null);
  const maxReconnectAttempts = 1;
  const [reconnectAttempts, setReconnectAttempts] = useState(0);

  const connectWebSocket = () => {
    // Clear any existing socket
    if (socketRef.current && socketRef.current.readyState !== WebSocket.CLOSED) {
      socketRef.current.close();
    }

    try {
      console.log(`Attempting to connect to WebSocket (Attempt ${reconnectAttempts + 1}/${maxReconnectAttempts})`);
      setConnectionStatus('connecting');

      // Connect to our proxy instead of directly to the WebSocket server
      // This avoids CORS issues
      socketRef.current = new WebSocket('ws://129.74.160.245:9002');

      // Connection opened
      socketRef.current.addEventListener("open", event => {
        console.log("Connection established");
        setConnectionStatus('connected');
        setErrorMessage('');
        setReconnectAttempts(0); // Reset attempts on successful connection
      });

      // Connection error
      socketRef.current.addEventListener("error", event => {
        console.error("WebSocket error:", event);
        setConnectionStatus('error');
        setErrorMessage(`Connection failed. Check if server is running at ws://129.74.160.245:9002`);

        // Try to reconnect if we haven't exceeded max attempts
        if (reconnectAttempts < maxReconnectAttempts) {
          clearTimeout(reconnectTimeoutRef.current);
          reconnectTimeoutRef.current = setTimeout(() => {
            setReconnectAttempts(prev => prev + 1);
            connectWebSocket();
          }, 2000); // Wait 2 seconds before trying again
        }
      });

      // Listen for messages
      socketRef.current.addEventListener("message", event => {
        const stots = symbolToTimeSeries;

        // parse json message from server
        const message = JSON.parse(event.data)

        // format is { timestamp: 1699999999, snapshot: { ... }, positions: [...] }
        const { timestamp, snapshot, positions } = message

        // Update positions if available
        if (positions) {
          setPositions(positions);
        }

        // snapshot is a list of {"symbol": {}, "best_bid": {}, "best_ask": {}}}
        // for each symbol in the snapshot, update the time series
        for (const sym in snapshot) {
          const { symbol, best_bid, best_ask } = snapshot[sym]

          // if the symbol is not in the map, add it
          if (!stots.has(symbol)) {
            stots.set(symbol, [])
          }

          // add the timestamp and snapshot to the time series
          stots.get(symbol).push([timestamp/ 1000000, { best_bid, best_ask }])
        }

        // if the time series is too long, remove the oldest entry
        // Use proper Map iteration with for...of
        for (const symbol of stots.keys()) {
          const timeSeries = stots.get(symbol)
          if (timeSeries.length > 1000) {
            // delete the oldest entry
            timeSeries.shift()
          }
        }

        // Set state with the NEW Map to trigger re-render
        setSymbolToTimeSeries(new Map(stots));
      });

      // Connection closed
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

  // Use a ref to track if Chart.js has been loaded
  const chartJsLoadedRef = useRef(false);

  // This effect will run after the component has mounted
  useEffect(() => {
    // Check if Chart.js is already available
    if (typeof Chart !== 'undefined' && chartJsLoadedRef.current) {
      return;
    }

    // Load Chart.js and the required date adapter
    const loadChartJs = async () => {
      try {
        // Create and load Chart.js script
        const chartScript = document.createElement('script');
        chartScript.src = 'https://cdn.jsdelivr.net/npm/chart.js';
        chartScript.async = true;

        // Create and load the date adapter script
        const adapterScript = document.createElement('script');
        adapterScript.src = 'https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns';
        adapterScript.async = true;

        // Wait for Chart.js to load first
        await new Promise((resolve) => {
          chartScript.onload = resolve;
          document.body.appendChild(chartScript);
        });

        // Then load the adapter
        await new Promise((resolve) => {
          adapterScript.onload = resolve;
          document.body.appendChild(adapterScript);
        });

        console.log('Chart.js and date adapter loaded');
        chartJsLoadedRef.current = true;

        // Force a re-render to create charts now that Chart.js is loaded
        setSymbolToTimeSeries(symbolToTimeSeries);
      } catch (error) {
        console.error('Failed to load Chart.js or adapter:', error);
      }
    };

    loadChartJs();

    return () => {
      // Clean up if component unmounts
      if (window.chartInstances) {
        Object.values(window.chartInstances).forEach(chart => chart.destroy());
        window.chartInstances = {};
      }
    };
  }, []);

  // This effect will update charts whenever symbolToTimeSeries changes
  useEffect(() => {
    // Only try to create charts if Chart.js is loaded
    if (!chartJsLoadedRef.current || typeof Chart === 'undefined') {
      return;
    }

    // Add a small delay to ensure DOM is ready
    const timer = setTimeout(() => {
      symbolToTimeSeries.forEach((timeSeries, symbol) => {
        if (timeSeries.length > 0) {
          getOrCreateChart(timeSeries, symbol);
        }
      });
    }, 20);

    return () => clearTimeout(timer);
  }, [symbolToTimeSeries]); // This dependency array is correct

  useEffect(() => {
    // Initial connection attempt
    connectWebSocket();

    // Cleanup function
    return () => {
      if (socketRef.current) {
        socketRef.current.close();
        socketRef.current = null;
      }
      clearTimeout(reconnectTimeoutRef.current);
    };
  }, []); // Empty dependency array means this runs once on mount

  return (
    <div>
      <h1>NDFEX Scoreboard</h1>
      <div>Connection Status: {connectionStatus}</div>
      {errorMessage && <div style={{ color: 'red' }}>{errorMessage}</div>}
      {reconnectAttempts > 0 &&
        <div>Reconnection attempts: {reconnectAttempts}/{maxReconnectAttempts}</div>
      }
      {connectionStatus === 'error' && reconnectAttempts >= maxReconnectAttempts &&
        <button onClick={() => {
          setReconnectAttempts(0);
          connectWebSocket();
        }}>Try again</button>
      }
      {Array.from(symbolToTimeSeries.entries()).map(([symbol, timeSeries]) => {
        // Find all position data for this symbol across all client IDs
        const symbolPositions = positions && Array.isArray(positions)
          ? positions.filter(p => p.symbol === parseInt(symbol))
          : [];

        return (
          <div key={symbol} className="chart-container" style={{ margin: '20px 0' }}>
            <h2>Symbol: {symbol == 1 ? "GOLD" : "BLUE"}</h2>
            <div style={{ display: 'flex', gap: '20px' }}>
              <div style={{ flex: '1' }}>
                <canvas id={`chart-${symbol}`} width="400" height="200"></canvas>
              </div>
              <div style={{ flex: '0 0 300px', border: '1px solid #ccc', borderRadius: '4px', padding: '10px' }}>
                <h3>Position Data</h3>
                {symbolPositions.length > 0 ? (
                  <table style={{ width: '100%', borderCollapse: 'collapse' }}>
                    <thead>
                      <tr>
                        <th style={{ padding: '4px 8px', textAlign: 'left', borderBottom: '1px solid #ddd' }}>Client ID</th>
                        <th style={{ padding: '4px 8px', textAlign: 'left', borderBottom: '1px solid #ddd' }}>Position</th>
                        <th style={{ padding: '4px 8px', textAlign: 'left', borderBottom: '1px solid #ddd' }}>PnL</th>
                        <th style={{ padding: '4px 8px', textAlign: 'left', borderBottom: '1px solid #ddd' }}>Volume</th>
                      </tr>
                    </thead>
                    <tbody>
                      {symbolPositions.map((posData, idx) => (
                        <tr key={idx}>
                          <td style={{ padding: '4px 8px' }}>{posData.client_id}</td>
                          <td style={{ padding: '4px 8px' }}>{posData.position}</td>
                          <td style={{
                            padding: '4px 8px',
                            color: posData.pnl >= 0 ? 'green' : 'red',
                            fontWeight: 'bold'
                          }}>
                            {posData.pnl.toFixed(2)}
                          </td>
                          <td style={{ padding: '4px 8px' }}>{posData.volume}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                ) : (
                  <p>No position data available</p>
                )}
              </div>
            </div>
          </div>
        );
      })}
    </div>
  );
}
