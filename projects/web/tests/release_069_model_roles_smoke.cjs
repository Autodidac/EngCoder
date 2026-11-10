'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const web = path.resolve(__dirname, '..');
const api = fs.readFileSync(path.join(web, 'js/api.js'), 'utf8');
const state = fs.readFileSync(path.join(web, 'js/state.js'), 'utf8');
const render = fs.readFileSync(path.join(web, 'js/render.js'), 'utf8');
const native = fs.readFileSync(path.join(web, '../native/src/agent/agent_runtime.cpp'), 'utf8');
const http = fs.readFileSync(path.join(web, '../native/src/server/http_server.cpp'), 'utf8');

assert.match(native, /native Qwen addon cannot replace or evict KeyMaker/);
assert.doesNotMatch(native, /active_model = selected_addon_model;/);
assert.match(http, /Middle reviewer must be a different model than the small responder/);
assert.match(http, /Middle reviewer must be independent from the KeyMaker foundation/);
assert.match(state, /item\.id !== small/);
assert.match(render, /respondingRole: msg\.respondingRole/);
assert.match(api, /retryRole = prior\.respondingRole === 'small-response'/);

const start = api.indexOf('function freshExecutionStats()');
const end = api.indexOf('async function deleteRuntimeSession(');
assert.ok(start > 0 && end > start);
const context = vm.createContext({});
vm.runInContext(api.slice(start, end) + '\nthis.record=recordExecutionEvent;this.stats=freshExecutionStats;', context);
const run = context.stats();
const response = {executionStats:run, respondingRole:'', respondingModel:'', activityEvents:[]};
const event = (type, data) => ({type,data,timestamp_ms:1000,sequence:1});
context.record(response, event('controller.decision',{model:'lmstudio://middle',model_attempted:true,model_used:false,source:'deterministic-fallback',model_fallback_reason:'controller returned no JSON decision'}));
assert.equal(response.executionStats.architect.modelFallbackReason, 'controller returned no JSON decision');
assert.match(render, /Architect model fallback/);
context.record(response, event('handoff.draft_completed',{model:'lmstudio://tiny'}));
context.record(response, event('review.completed',{model:'lmstudio://middle'}));
context.record(response, event('controller.model_selected',{model:'qwenflash://large',role:'keymaker-foundation'}));
context.record(response, event('model.request',{model:'qwenflash://large',role:'keymaker-foundation',turn:0}));
context.record(response, event('model.response',{model:'qwenflash://large',duration_ms:600}));
assert.equal(response.executionStats.modelRoles.draftUsed, 'lmstudio://tiny');
assert.equal(response.executionStats.modelRoles.middleUsed, 'lmstudio://middle');
assert.equal(response.respondingRole, 'keymaker-foundation');
assert.equal(response.respondingModel, 'qwenflash://large');
const small = {executionStats:context.stats(), activityEvents:[]};
context.record(small, event('controller.model_selected',{model:'lmstudio://tiny',role:'small-response'}));
context.record(small, event('residency.prepared',{model:'lmstudio://tiny'}));
context.record(small, event('model.request',{model:'lmstudio://tiny',role:'small-response',turn:0}));
context.record(small, event('model.response',{model:'lmstudio://tiny',duration_ms:20}));
assert.equal(small.respondingRole, 'small-response');
assert.equal(small.respondingModel, 'lmstudio://tiny');
assert.equal(small.executionStats.keymaker.turns, 0);
assert.equal(small.executionStats.keymaker.model, '');

const fnStart = api.indexOf('async function submitEngCoderTask(');
const fnEnd = api.indexOf('async function pollEngCoderTask(', fnStart);
assert.ok(fnStart > 0 && fnEnd > fnStart);
let sent;
const selects = {
  foundationModel:{value:'lmstudio://large'}, simpleResponseModel:{value:'lmstudio://tiny'},
  reviewModel:{value:'lmstudio://middle'}, reviewMode:{value:'auto'},
  thinkingMode:{value:'auto'}, controllerModel:{value:''}, forcedAddon:{value:''},
  autoRouting:{checked:true}, enableAddons:{checked:false}, system:{value:''},
  temperature:{value:0.15}, max_tokens:{value:-1}, includeHistory:{checked:true},
  maxHistoryTurns:{value:0}
};
const routing = vm.createContext({
  $:id=>selects[id] ?? null, controller:null, engcoderBase:()=> 'http://localhost:14321',
  fetch:async (_url,opts)=>{sent=JSON.parse(opts.body);return {ok:true,json:async()=>({task_id:'test',session_id:'s'})};}
});
vm.runInContext(api.slice(fnStart, fnEnd) + '\nthis.submit=submitEngCoderTask;', routing);
(async()=>{
  await routing.submit('s','retry large', 'lmstudio://large','keymaker-foundation');
  assert.equal(sent.retry_model,'lmstudio://large');
  assert.equal(sent.foundation_model,'lmstudio://large');
  assert.equal(sent.simple_model,'lmstudio://tiny');
  selects.simpleResponseModel.value='lmstudio://another';
  await routing.submit('s','retry small','lmstudio://tiny','small-response');
  assert.equal(sent.retry_model,'lmstudio://tiny');
  assert.equal(sent.simple_model,'lmstudio://tiny');
  assert.equal(sent.foundation_model,'lmstudio://large');
  console.log('PASS: correct actual model roles, independent 3-model assignments, restored role-aware retry and foundation protection');
})().catch(error=>{console.error(error);process.exitCode=1;});
