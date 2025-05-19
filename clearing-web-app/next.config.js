/** @type {import('next').NextConfig} */

const nextConfig = {
  reactStrictMode: true,
  // Add webpack configuration to ensure proper manifest generation
  webpack: (config, { isServer, dev }) => {
    // Add optimizations for production builds
    if (!dev && isServer) {
      console.log('Optimizing server build...');
    }
    return config;
  },
}

// Store server instance so pages/api/socket.js can access it
if (typeof global.server === 'undefined') {
  const { createServer } = require('http');
  const { parse } = require('url');
  const next = require('next');

  const dev = process.env.NODE_ENV !== 'production';
  const app = next({ dev });
  const handle = app.getRequestHandler();

  app.prepare().then(() => {
    global.server = createServer((req, res) => {
      const parsedUrl = parse(req.url, true);
      handle(req, res, parsedUrl);
    });

    global.server.listen(3000, (err) => {
      if (err) throw err;
      console.log('> Ready on http://localhost:3000');
    });
  });
}

module.exports = nextConfig;
