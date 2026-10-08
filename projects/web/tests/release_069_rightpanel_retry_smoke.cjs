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
