'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const api = fs.readFileSync(path.join(root, 'js/api.js'), 'utf8');
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
const render = fs.readFileSync(path.join(root, 'js/render.js'), 'utf8');
const server = fs.readFileSync(path.resolve(root, '../native/src/server/http_server.cpp'), 'utf8');
const multiplex = fs.readFileSync(path.resolve(root, '../native/src/model/multiplex_provider.cpp'), 'utf8');
assert.match(html, /id="qwenLiveTab"[^>]*data-run-view="qwen"/);
assert.match(server, /http\.Get\("\/api\/engcoder\/qwen-output"/);
assert.match(server, /model_\.qwen_output_after\(after\)/);
assert.match(server, /remote_api_token\(\)/); // pre-routing bearer authentication
assert.match(multiplex, /return complete_stream\(request, \{\}\)/); // nonstreaming Qwen visible
assert.match(multiplex, /qwen_output_\.append\(trace_id, resolved_model, "reasoning"/);
assert.match(multiplex, /qwen_output_\.append\(trace_id, resolved_model, "arguments"/);
assert.match(render, /function syncQwenLiveOutput\(\)/);
const start = api.indexOf('function applyQwenLiveEvents(');
const end = api.indexOf('\nasync function pollQwenLiveOutput()', start);
assert.ok(start > 0 && end > start);
const ctx = vm.createContext({RAW_OUTPUT_LIMIT: 96 * 1024});
vm.runInContext(api.slice(start, end) + '\nthis.apply=applyQwenLiveEvents;', ctx);
let state = {rawOutput:'',requestId:'',live:false, model:''};
const begin = [{request_id:'mcp-1',model:'qwenflash://next',kind:'start'},
  {request_id:'mcp-1',kind:'reasoning',delta:'thinking '},
  {request_id:'mcp-1',kind:'text',delta:'answer '},
  {request_id:'mcp-1',kind:'arguments',delta:'{"file":"main.cpp"}'}];
assert.equal(ctx.apply(state, begin), true);
assert.equal(state.rawOutput, 'thinking answer {"file":"main.cpp"}');
assert.equal(state.live, true);
assert.equal(ctx.apply(state,[{request_id:'other',kind:'text',delta:'secret from unrelated call'},
 {request_id:'mcp-1',kind:'finish'}]), false);
assert.equal(state.rawOutput, 'thinking answer {"file":"main.cpp"}');
assert.equal(state.live, false);
ctx.apply(state,[{request_id:'mcp-2',model:'qwenflash://next',kind:'start'},
 {request_id:'mcp-2',kind:'text',delta:'fresh'}]);
assert.equal(state.rawOutput,'fresh');
assert.equal(state.requestId,'mcp-2');
console.log('PASS: MCP/OpenAI Qwen feed, clean output, request isolation, bounded source, nonstreaming instrumentation');
