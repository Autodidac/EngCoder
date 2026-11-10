'use strict';
// Regression: a Web page can reference a script that exists on disk but is
// missing from the native HTTP server's explicit /ui/js/* route allowlist.
// That silently disabled math rendering in the 0.6.9 release.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '../../..');
const webRoot = path.join(root, 'projects/web');
const html = fs.readFileSync(path.join(webRoot, 'index.html'), 'utf8');
const nativeServer = fs.readFileSync(path.join(root, 'projects/native/src/server/http_server.cpp'), 'utf8');
const scriptTags = [...html.matchAll(/<script\b[^>]*\bsrc\s*=\s*["'](js\/[^"']+)["'][^>]*>/gi)]
  .map(match => match[1]);
assert.ok(scriptTags.length >= 8, 'should inspect all startup JavaScript assets');
const routeList = nativeServer.match(/for\s*\(const\s+auto\*\s+script\s*:\s*\{([^}]+)\}\)/);
assert.ok(routeList, 'native HTTP server must have an explicit static-script route list');
const routes = new Set([...routeList[1].matchAll(/"([^"\\]+\.js)"/g)].map(match => match[1]));
for (const name of scriptTags) {
  assert.ok(fs.existsSync(path.join(webRoot, name)), `HTML references nonexistent script: ${name}`);
  assert.ok(routes.has(path.basename(name)), `HTTP 404: ${name} is not registered by the native server`);
}
assert.ok(routes.has('math.js'), 'math renderer must be served over HTTP');
assert.ok(html.indexOf('src="js/math.js"') < html.indexOf('src="js/render.js"'), 'math renderer must load before chat renderer');
assert.match(nativeServer, /response\.set_header\("Cache-Control",\s*"no-store"\)/,
  'release changes must not leave a cached old UI asset behind');
console.log(`PASS: ${scriptTags.length} Web scripts exist and all have native HTTP routes (including math.js); caching disabled`);
