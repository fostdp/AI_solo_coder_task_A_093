const http = require('http');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const PORT = parseInt(process.env.PORT || '3000', 10);
const HOST = process.env.HOST || '0.0.0.0';
const BACKEND_URL = process.env.BACKEND_URL || 'http://127.0.0.1:8080';
const ROOT = __dirname;

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'application/javascript; charset=utf-8',
  '.css':  'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png':  'image/png',
  '.jpg':  'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif':  'image/gif',
  '.svg':  'image/svg+xml',
  '.ico':  'image/x-icon',
  '.woff': 'font/woff',
  '.woff2':'font/woff2',
  '.ttf':  'font/ttf',
  '.map':  'application/json',
  '.txt':  'text/plain; charset=utf-8'
};

const CACHE_BUST_EXT = new Set(['.css', '.js', '.woff', '.woff2', '.ttf', '.png', '.jpg', '.jpeg', '.gif', '.svg']);

function getMime(filePath) {
  return MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream';
}

function proxyRequest(req, res, targetUrl) {
  const url = new URL(req.url, targetUrl);
  const options = {
    hostname: url.hostname,
    port: url.port,
    path: url.pathname + url.search,
    method: req.method,
    headers: { ...req.headers, host: url.host }
  };

  const proxyReq = http.request(options, (proxyRes) => {
    res.writeHead(proxyRes.statusCode, proxyRes.headers);
    proxyRes.pipe(res);
  });

  proxyReq.on('error', (err) => {
    res.writeHead(502, { 'Content-Type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ error: 'Bad Gateway', message: err.message }));
  });

  req.pipe(proxyReq);
}

const server = http.createServer((req, res) => {
  if (req.url.startsWith('/api/') || req.url === '/metrics') {
    return proxyRequest(req, res, BACKEND_URL);
  }

  let reqPath = decodeURIComponent(req.url.split('?')[0]);
  if (reqPath === '/') reqPath = '/index.html';

  const filePath = path.normalize(path.join(ROOT, reqPath));
  if (!filePath.startsWith(ROOT)) {
    res.writeHead(403, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Forbidden');
    return;
  }

  fs.stat(filePath, (err, stats) => {
    if (err || !stats.isFile()) {
      res.writeHead(404, { 'Content-Type': 'text/html; charset=utf-8' });
      fs.createReadStream(path.join(ROOT, 'index.html')).pipe(res);
      return;
    }

    const ext = path.extname(filePath).toLowerCase();
    const contentType = getMime(filePath);

    const headers = {
      'Content-Type': contentType,
      'Cache-Control': CACHE_BUST_EXT.has(ext)
        ? 'public, max-age=2592000, immutable'
        : 'no-cache'
    };

    const acceptEncoding = req.headers['accept-encoding'] || '';
    let stream = fs.createReadStream(filePath);

    if (acceptEncoding.includes('br') && ext !== '.br' && stats.size > 1024 &&
        (ext.startsWith('.htm') || ext === '.css' || ext === '.js' || ext === '.json' || ext === '.svg')) {
      try {
        headers['Content-Encoding'] = 'br';
        stream = stream.pipe(zlib.createBrotliCompress({
          params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 6 }
        }));
      } catch (e) { /* fallback no compress */ }
    } else if (acceptEncoding.includes('gzip') && ext !== '.gz' && stats.size > 1024 &&
               (ext.startsWith('.htm') || ext === '.css' || ext === '.js' || ext === '.json' || ext === '.svg' || ext === '.txt')) {
      headers['Content-Encoding'] = 'gzip';
      stream = stream.pipe(zlib.createGzip({ level: 6 }));
    }

    res.writeHead(200, headers);
    stream.pipe(res);
  });
});

server.listen(PORT, HOST, () => {
  console.log(`[frontend] 静态服务器已启动: http://${HOST}:${PORT}`);
  console.log(`[frontend] 根目录: ${ROOT}`);
  console.log(`[frontend] API 代理至: ${BACKEND_URL}`);
  console.log(`[frontend] Gzip/Brotli 压缩已启用`);
});
