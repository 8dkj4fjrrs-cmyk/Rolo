#!/usr/bin/env node
// Checks a game exported for the web the way a player meets it: serves the
// folder, opens the page in a real (headless) browser, and looks at what
// happens. Used by CI (.github/workflows/ci.yml) and by hand:
//
//   node tools/web/check_web_game.js "exports/My Game Web" [--screenshot shot.png]
//                                   [--expect "text in the log"]... [--keys Escape,ArrowDown,Enter]
//
// It passes if the page loads the game and its first scene is shown, nothing
// crashed, the picture is not blank, the game still runs after the keys were
// pressed, and every --expect text appeared in the log by then. Needs Node and Playwright
// with Chromium (npm install playwright; npx playwright install chromium).

const http = require('http');
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
if (args.length === 0 || args[0].startsWith('--')) {
  console.error('Usage: check_web_game.js FOLDER [--screenshot FILE] [--expect TEXT]... [--keys K1,K2,...]');
  process.exit(2);
}
const folder = path.resolve(args[0]);
let screenshot = '';
let keys = [];
const started = ['Showing scene'];   // the log line that says the game is being drawn
const expected = [];
for (let i = 1; i < args.length; i++) {
  if (args[i] === '--screenshot') screenshot = args[++i];
  else if (args[i] === '--expect') expected.push(args[++i]);
  else if (args[i] === '--keys') keys = args[++i].split(',').filter(Boolean);
  else { console.error('Unknown option ' + args[i]); process.exit(2); }
}

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.txt': 'text/plain' };

// A plain web server for the folder: no special headers, like any web host.
const server = http.createServer((request, response) => {
  const name = decodeURIComponent(request.url.split('?')[0]);
  const file = path.join(folder, name === '/' ? 'index.html' : name);
  if (!file.startsWith(folder) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    response.writeHead(404);
    response.end('not found');
    return;
  }
  response.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
  fs.createReadStream(file).pipe(response);
});

function fail(message, log) {
  console.error('FAILED: ' + message);
  if (log) console.error(log.join('\n'));
  process.exit(1);
}

(async () => {
  const { chromium } = require('playwright');
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const url = 'http://127.0.0.1:' + server.address().port + '/';
  // SwiftShader: WebGL without a graphics card (CI machines have none).
  const browser = await chromium.launch({
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  const log = [];
  const errors = [];
  page.on('console', message => log.push('[' + message.type() + '] ' + message.text()));
  page.on('pageerror', error => errors.push(error.message));
  page.on('requestfailed', request => errors.push('could not load ' + request.url()));
  await page.goto(url);

  const seen = text => log.some(line => line.includes(text));
  const deadline = Date.now() + 60000;
  const pageFailed = () => page.locator('#failed').isVisible();
  while (!started.every(seen) && errors.length === 0 && !(await pageFailed()) && Date.now() < deadline) {
    await page.waitForTimeout(200);
  }
  if (await pageFailed()) fail('the page shows: ' + await page.locator('#reason').innerText(), log);
  if (errors.length > 0) fail('the page reported: ' + errors.join('; '), log);
  for (const text of started) {
    if (!seen(text)) fail('the log never said "' + text + '"', log);
  }
  if (await page.locator('#loading').isVisible()) fail('the loading panel is still shown', log);

  await page.waitForTimeout(1000);   // a second of play
  for (const key of keys) {
    await page.keyboard.press(key, { delay: 80 });
    await page.waitForTimeout(250);
  }
  await page.waitForTimeout(500);
  if (errors.length > 0) fail('the game crashed: ' + errors.join('; '), log);
  for (const text of expected) {
    if (!seen(text)) fail('the log never said "' + text + '"', log);
  }
  if (log.some(line => line.includes('[ERROR]'))) fail('the game logged an error', log);
  if (log.some(line => line.includes('Could not load the image'))) fail('an image could not be loaded', log);

  // The picture must not be one flat colour: something was drawn. (A WebGL
  // canvas cannot be read back between frames, so a screenshot of it is
  // taken, as a player sees it, and its colours counted.)
  const shot = await page.locator('#canvas').screenshot();
  const colours = await page.evaluate(async base64 => {
    const image = new Image();
    image.src = 'data:image/png;base64,' + base64;
    await image.decode();
    const copy = document.createElement('canvas');
    copy.width = 64;
    copy.height = 36;
    const context = copy.getContext('2d');
    context.drawImage(image, 0, 0, copy.width, copy.height);
    const data = context.getImageData(0, 0, copy.width, copy.height).data;
    const set = new Set();
    for (let i = 0; i < data.length; i += 4) set.add((data[i] << 16) | (data[i + 1] << 8) | data[i + 2]);
    return set.size;
  }, shot.toString('base64'));
  if (screenshot) await page.screenshot({ path: screenshot });
  if (colours < 4) fail('the game\'s picture is blank (' + colours + ' colours)', log);

  const frames = () => page.evaluate(() => new Promise(resolve => {
    let count = 0;
    const start = performance.now();
    function tick() { count++; if (performance.now() - start < 500) requestAnimationFrame(tick); else resolve(count); }
    requestAnimationFrame(tick);
  }));
  if (await frames() < 2) fail('the page stopped drawing frames', log);

  console.log('OK: ' + path.basename(folder) + ' runs in a browser (' + colours + ' colours in its picture, '
              + log.length + ' log lines).');
  await browser.close();
  server.close();
})().catch(error => fail(String(error && error.stack || error)));
