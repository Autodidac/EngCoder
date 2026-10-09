'use strict';
// Native model routing and GPU/MTP behavior must still be tested on Windows.
// This guards the Web wiring without simulating model inference.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const read = rel => fs.readFileSync(path.join(root, rel), 'utf8');
const html = read('index.html');
const state = read('js/state.js');
const api = read('js/api.js');
const config = read('js/config.js');
const ui = read('js/ui.js');
const nativeHttp = fs.readFileSync(path.resolve(root, '../native/src/server/http_server.cpp'), 'utf8');
const nativeAgent = fs.readFileSync(path.resolve(root, '../native/src/agent/agent_runtime.cpp'), 'utf8');
for (const id of ['reviewModel', 'reviewMode', 'thinkingMode']) {
  assert.match(html, new RegExp(`id=["']${id}["']`));
  assert.match(config, new RegExp(`${id}:`));
  assert.match(state, new RegExp(`STORAGE_KEYS\\.${id}`));
  assert.match(ui, new RegExp(`["']${id}["']`));
}
for (const key of ['review_model', 'review_mode', 'thinking_mode']) {
  assert.match(api, new RegExp(`${key}:`));
  assert.match(nativeHttp, new RegExp(`"${key}"`));
}
assert.match(state, /!item\.internal && !item\.native/);
assert.match(nativeAgent, /review\.requested/);
assert.match(nativeAgent, /review\.completed/);
assert.match(nativeAgent, /review\.failed/);
assert.match(nativeAgent, /controller\.escalated/);
assert.match(api, /model\.text_delta/);
assert.match(config, /APP_VERSION = '\d+\.\d+\.\d+'/);
console.log('PASS: reviewer/analysis selectors, persistence, payload, native routing and generated-only stream');
