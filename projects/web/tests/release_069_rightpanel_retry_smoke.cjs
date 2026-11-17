'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const web = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(web,p), 'utf8');
const html = read('index.html');
const render = read('js/render.js');
const api = read('js/api.js');
const ui = read('js/ui.js');
const state = read('js/state.js');
const native = fs.readFileSync(path.resolve(web, '../native/src/agent/agent_runtime.cpp'),'utf8');
const http = fs.readFileSync(path.resolve(web, '../native/src/server/http_server.cpp'),'utf8');
assert.match(html, /id="generatedOutputTab"[^>]*data-run-view="output"/);
assert.match(html, /id="runInfoTab"[^>]*data-run-view="info"/);
assert.match(ui, /rightPanelView = tab\.dataset\.runView/);
for (const title of ['Run', 'Models & routing', 'Timing', 'Tools & context', 'Runtime timeline'])
  assert.ok(render.includes(title), `missing prior menu section ${title}`);
assert.match(render, /function syncGeneratedOutput\(\)/);
assert.match(render, /function syncRunInfo\(\)/);
assert.match(state, /base\.respondingModel = typeof msg\.respondingModel/);
assert.match(render, /respondingModel: options\.respondingModel \|\| ''/);
assert.match(render, /rawOutput: options\.rawOutput \|\| ''/);
assert.match(render, /retryTargetIndex: chat\.messages\.indexOf\(msg\)/);
assert.match(api, /retry_model: retryModel/);
assert.match(http, /retry_model must match the current task's foundation or small responder/);
assert.match(native, /request\.retry_model == request\.simple_model/);

function tab(view) {
  return {dataset: {runView: view}, selected: null, classList: {
    toggle(_class, chosen) {this.chosen = chosen;}
  }, setAttribute(name, value) {if (name==='aria-selected') this.selected=value;}, tabIndex: -1};
}
const t1=tab('output'), t2=tab('info');
const body = {setAttribute(name,value) {this.label=value;},
  textContent: '', innerHTML: '', details: null,
  querySelector(selector) {return selector==='[data-activity-thinking]' ? this.details : null;}}
const ctxt = vm.createContext({
  rightPanelView:'info', activeThinkingState: {
    content:'Hello', thinkingLive:false, completionAt:'12:34',
    respondingModel:'lmstudio/small-model', completionState:'Idle',
    executionStats:{architect:{source:'deterministic-fallback', confidence:0.85},
      keymaker:{requestedModel:'qwenflash://main',turns:1}, addons:{names:[]}, tools:{names:[]}},
    activityEvents:[{type:'controller.decision',text:'Controller chose safe route'}]
  },
  generatedOutputTab:t1, runInfoTab:t2, thinkingDrawerBody:body, thinkingStateEl:{textContent:''},
  runtimeArchitecture:{foundation:{model:'qwenflash://main'}},
  esc(x) {return String(x).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;');},
  cleanAssistantContent:x=>x, cleanThinkingContent:x=>x,
  formatTimestampFull:()=>'', displayCompletionReason:()=> 'Completed',
  formatDurationMs:()=> '0ms', estimateTokenCount:()=>5, estimateTps:()=>5,
  document:{createElement:()=>({})}
});
const start = render.indexOf('function assignedKeyMakerModel()');
assert.ok(start < 0, 'unexpected method signature');
const fnSrc = render.slice(render.indexOf('function assignedKeyMakerModel('),render.indexOf('function openThinkingDrawer('));
vm.runInContext(fnSrc + '\nthis.draw=syncThinkingDrawer;', ctxt);
ctxt.draw();
assert.match(body.innerHTML, /Models &amp; routing|Models & routing/);
assert.match(body.innerHTML, /lmstudio\/small-model/);
assert.match(body.innerHTML, /Runtime timeline/);
assert.equal(body.label, 'Run information');
assert.equal(t1.selected, 'false');assert.equal(t2.selected,'true');
assert.doesNotMatch(body.innerHTML, /Candidate repair evidence/);

const eventFormatting = api.slice(api.indexOf('function conciseRuntimeError('), api.indexOf('function terminalFromEvent('));
ctxt.$ = () => null;
vm.runInContext(eventFormatting + '\nthis.formatEvent = eventActivityLine;', ctxt);
const rejectedPatch = {type: 'tool.review_failed', data: {name: 'workspace.patch', error: 'expected block is ambiguous\nextra detail'}};
assert.equal(ctxt.formatEvent(rejectedPatch), 'Tool review failed · workspace.patch · expected block is ambiguous');
ctxt.activeThinkingState.activityEvents = [
  rejectedPatch,
  {type: 'iteration.check_finished', data: {stage: 'candidate', check: 'tests', attempt: 1, exit_code: 8}},
  {type: 'iteration.check_finished', data: {stage: 'candidate', check: 'build', attempt: 2, exit_code: 0}},
  {type: 'iteration.check_finished', data: {stage: 'candidate', check: 'tests', attempt: 2, exit_code: 0}},
].map(event => ({ type: event.type, text: ctxt.formatEvent(event) }));
ctxt.draw();
const evidence = body.innerHTML.split('Runtime timeline')[0];
assert.match(evidence, /Candidate repair evidence/);
assert.match(evidence, /Candidate tests · attempt 1 · failed \(exit 8\)/);
assert.match(evidence, /Candidate build · attempt 2 · passed/);
assert.match(evidence, /Candidate tests · attempt 2 · passed/);
assert.doesNotMatch(body.innerHTML, /data-activity-thinking open/);
assert.match(body.innerHTML, /Tool review failed · workspace\.patch · expected block is ambiguous/);

const recordSource = api.slice(api.indexOf('function freshExecutionStats('), api.indexOf('async function deleteRuntimeSession('));
vm.runInContext(recordSource + '\nthis.record = recordExecutionEvent;', ctxt);
const checkpoint = {type: 'tool.completed', data: {name: 'workspace.patch', candidate_path: 'src/fixture.cpp',
  result: {ok: true, checkpoint_id: 'cp_test_01'}}};
assert.equal(ctxt.formatEvent(checkpoint), 'Checkpoint saved · cp_test_01');
assert.equal(ctxt.formatEvent({type: 'tool.completed', data: {name: 'workspace.read', result: checkpoint.data.result}}),
  'Completed tool · workspace.read');
assert.equal(ctxt.formatEvent({type: 'tool.completed', data: {name: 'workspace.patch', result: {ok: false, checkpoint_id: 'cp_false'}}}),
  'Completed tool · workspace.patch');
assert.equal(ctxt.formatEvent({type: 'tool.completed', data: {name: 'workspace.patch'}}), 'Completed tool · workspace.patch');
assert.equal(ctxt.formatEvent({type: 'verification.completed', data: {status: 'model_finished'}}), 'Model finished');
const candidateState = {candidateIteration: true, content: 'Draft', completionState: 'Stopped'};
const record = (target, event) => ctxt.record(target, event, ctxt.formatEvent(event));
const checkEvent = (type, check, attempt, detail = {}) => ({type, data: {stage: 'candidate', check, attempt, ...detail}});
record(candidateState, {type: 'task.awaiting_approval', data: {tool: 'workspace.patch',
  arguments: {path: 'src/<draft>.cpp', expected: 'original source', replacement: 'new source'}}});
record(candidateState, {type: 'tool.approved', data: {name: 'workspace.patch', source: 'user',
  candidate_path: 'src/fixture.cpp'}});
assert.doesNotMatch(JSON.stringify(candidateState.executionStats.candidateEvidence), /Checkpoint saved/);
record(candidateState, checkpoint);
record(candidateState, checkEvent('iteration.check_started', 'build', 1));
record(candidateState, checkEvent('iteration.check_finished', 'build', 1, {exit_code: 0}));
record(candidateState, checkEvent('iteration.check_started', 'build', 1));
record(candidateState, checkEvent('iteration.check_started', 'tests', 1));
record(candidateState, {type: 'task.cancelled', sequence: 12});
record(candidateState, {type: 'task.cancelled', sequence: 12});
for (let index = 0; index < 305; index++) record(candidateState, {type: 'model.request', data: {model: 'fixture'}});
ctxt.activeThinkingState = JSON.parse(JSON.stringify(candidateState));
ctxt.draw();
const cancelledEvidence = body.innerHTML.split('Runtime timeline')[0];
assert.match(cancelledEvidence, /Waiting for approval · workspace\.patch · src\/&lt;draft&gt;\.cpp/);
assert.match(cancelledEvidence, /Approved tool · workspace\.patch · src\/fixture\.cpp/);
assert.match(cancelledEvidence, /Checkpoint saved · cp_test_01 · src\/fixture\.cpp/);
assert.doesNotMatch(cancelledEvidence, /original source|new source/);
assert.match(cancelledEvidence, /Candidate build · attempt 1 · passed/);
assert.doesNotMatch(cancelledEvidence, /Candidate build · attempt 1 · started/);
assert.match(cancelledEvidence, /Candidate tests · attempt 1 · started/);
assert.match(cancelledEvidence, />Stopped</);
assert.equal((cancelledEvidence.match(/class="activity-line">Stopped</g) || []).length, 1);
assert.doesNotMatch(cancelledEvidence, /tests · attempt 1 · (running|passed)/);
const ordinaryState = {content: 'Ordinary conversation'};
record(ordinaryState, checkpoint);
record(ordinaryState, {type: 'verification.completed', data: {status: 'model_finished'}});
ctxt.activeThinkingState = ordinaryState;
ctxt.draw();
assert.doesNotMatch(body.innerHTML, /Candidate repair evidence/);

// Output mode must not expose run metadata or JSON. It only streams generated text.
ctxt.rightPanelView='output';
const pre={textContent:'',scrollHeight:100,scrollTop:0,clientHeight:100,dataset:{}};
body.querySelector=selector => selector==='[data-generated-output]' ? pre : null;
ctxt.activeThinkingState.rawOutputFormat='generated-v1';
ctxt.activeThinkingState.rawOutput='Hello';
ctxt.draw();
assert.equal(pre.textContent,'Hello');
assert.equal(t1.selected,'true');assert.equal(t2.selected,'false');
assert.equal(body.label,'Generated output');
// Verify a restored assistant carries both retry identity and raw output.
assert.match(render, /retryTargetIndex: Number\.isInteger\(options\.retryTargetIndex\)/);
console.log('PASS: original right-side menu restored as separate Run info tab; clean output; preserved retry IDs');
