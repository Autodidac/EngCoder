'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const api = fs.readFileSync(path.join(root,'js/api.js'),'utf8');
const ui = fs.readFileSync(path.join(root,'js/ui.js'),'utf8');
const render = fs.readFileSync(path.join(root,'js/render.js'),'utf8');
const server = fs.readFileSync(path.resolve(root,'../native/src/server/http_server.cpp'),'utf8');
const journal = fs.readFileSync(path.resolve(root,'../native/include/engcoder/model/qwen_output_journal.hpp'),'utf8');
assert.match(server, /"latest"[\s\S]*?snapshot\.latest\.output/);
assert.match(journal, /snapshot\.latest = latest_/);
assert.match(ui, /rightPanelView !== 'qwen'/);
assert.match(render, /Idle · last output retained/);
const start = api.indexOf('function applyQwenLiveEvents(');
const end = api.indexOf('\nasync function pollQwenLiveOutput()',start);
assert.ok(start !== -1 && end > start);
const ctx = vm.createContext({RAW_OUTPUT_LIMIT: 96 * 1024});
vm.runInContext(api.slice(start,end)+'\nthis.apply=applyQwenLiveEvents; this.reconcile=reconcileQwenLiveSnapshot;',ctx);
const state = {rawOutput:'', requestId:'', model:'', live:false};
assert.equal(ctx.apply(state,[{kind:'start',request_id:'a',model:'qwenflash://next'},
 {kind:'text',request_id:'a',delta:'Completed work'}, {kind:'finish',request_id:'a'}]),true);
assert.equal(state.live,false);
assert.equal(state.rawOutput,'Completed work');
assert.equal(ctx.apply(state,[]),false);
assert.equal(state.rawOutput,'Completed work');
assert.equal(ctx.reconcile(state,{request_id:'a',model:'qwenflash://next',output:'Completed work',generating:false}),false);
assert.equal(state.rawOutput,'Completed work');
// A refreshed/slow browser can read the last completed output even after
// all of that request's start/delta events have been pruned from the ring.
const restored = {rawOutput:'',requestId:'',model:'',live:false};
assert.equal(ctx.reconcile(restored,{request_id:'a',model:'qwenflash://next',output:'Completed work',generating:false}),true);
assert.equal(restored.rawOutput,'Completed work');
assert.equal(restored.live,false);
// A queued/empty next request must NOT erase the previous answer. Only an
// actual provider text/reasoning/argument fragment replaces it.
ctx.apply(state,[{kind:'start',request_id:'b',model:'qwenflash://next'}]);
assert.equal(state.rawOutput,'Completed work');
assert.equal(state.live,true);
assert.equal(state.awaitingNewOutput,true);
ctx.apply(state,[{kind:'finish',request_id:'b'}]);
assert.equal(state.rawOutput,'Completed work');
assert.equal(state.live,false);
ctx.apply(state,[{kind:'start',request_id:'c',model:'qwenflash://next'},
  {kind:'text',request_id:'c',delta:'New generation'},
  {kind:'finish',request_id:'c'}]);
assert.equal(state.rawOutput,'New generation');
assert.equal(state.live,false);
assert.equal(state.awaitingNewOutput,false);
// Refresh while a request is preparing: the server returns the retained text.
const preparing = {rawOutput:'',requestId:'',model:'',live:false};
ctx.reconcile(preparing,{request_id:'d',model:'qwenflash://next',output:'New generation',generating:true,has_current_output:false});
assert.equal(preparing.rawOutput,'New generation');
assert.equal(preparing.awaitingNewOutput,true);
console.log('PASS: Qwen idle output remains visible through empty starts, completion, refresh and real replacement');
