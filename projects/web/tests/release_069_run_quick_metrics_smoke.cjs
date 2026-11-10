'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const read = filename => fs.readFileSync(path.join(root, filename), 'utf8');
const render = read('js/render.js');
const api = read('js/api.js');
const state = read('js/state.js');
const runStart = render.indexOf('function assignedKeyMakerModel(');
const runEnd = render.indexOf('function syncThinkingDrawer()');
assert.ok(runStart >= 0 && runEnd > runStart, 'Run renderer exists');
const runCode = render.slice(runStart, runEnd);
const summaryPos = runCode.indexOf("runInfoRow('Total time'");
assert.ok(summaryPos > runCode.indexOf("runInfoRow('State'"), 'time follows State');
assert.ok(runCode.indexOf("runInfoRow('Generation speed (est.)'") > summaryPos, 'TPS follows time');
assert.ok(runCode.indexOf("sectionHtml('Timing'") > runCode.indexOf("sectionHtml('Run'"), 'Timing section retained');
assert.match(api, /assistantState\.firstOutputAtMs = timing\.firstContentAt/);
assert.match(api, /startedAt: Number\(last\.taskStartedAtMs\)/);
assert.match(api, /firstContentAt: Number\(last\.firstOutputAtMs\)/);
assert.match(api, /taskStartedAtMs: Number\(state\.taskStartedAtMs\)/);
assert.match(api, /firstOutputAtMs: Number\(state\.firstOutputAtMs\)/);
assert.match(render, /taskStartedAtMs: Number\(options\.taskStartedAtMs\)/);
assert.match(render, /taskStartedAtMs: msg\.taskStartedAtMs \|\| 0/);
assert.match(render, /firstOutputAtMs: msg\.firstOutputAtMs \|\| 0/);
assert.match(state, /base\.taskStartedAtMs = Number\(msg\.taskStartedAtMs\)/);
assert.match(state, /base\.firstOutputAtMs = Number\(msg\.firstOutputAtMs\)/);

const clock = 1800000000000;
const drawer = {
  innerHTML: '',
  querySelector: () => null
};
const context = vm.createContext({
  Date: class extends Date { static now() { return clock; } },
  thinkingDrawerBody: drawer,
  thinkingStateEl: {textContent: ''},
  activeThinkingState: null,
  esc: value => String(value).replaceAll('&','&amp;').replaceAll('<','&lt;'),
  cleanAssistantContent: text => text,
  cleanThinkingContent: text => text,
  formatDurationMs: ms => `${ms}ms`,
  estimateTokenCount: text => Math.round(text.length / 4),
  estimateTps: (text, duration) => duration > 0 ? (Math.round(text.length/4)/(duration/1000)).toFixed(1) : 'N/A',
  displayCompletionReason: () => 'Completed',
  formatTimestampFull: () => '',
  runtimeArchitecture: {foundation:{model:'qwenflash://main'}},
  activeThinkingStateFallback: null
});
vm.runInContext(runCode + '\nthis.draw=syncRunInfo;',context);
function draw(s) { context.activeThinkingState = s; context.draw(); return drawer.innerHTML; }
const running = {
  thinkingLive: true, taskStartedAtMs: clock-11000, firstOutputAtMs: clock-5000,
  content:'A'.repeat(200), executionStats:{architect:{},keymaker:{},tools:{},addons:{}},
  completionDurationMs:0
};
const liveHtml = draw(running);
assert.match(liveHtml, /Total time<\/div><div>11000ms/);
assert.match(liveHtml, /Generation speed \(est\.\)<\/div><div>~10\.0 tok\/s/);
assert.match(liveHtml, /activity-section-header">Timing/);
assert.match(liveHtml, /Output TPS ~/);
assert.match(draw({...running,content:'',firstOutputAtMs:0}), /Generation speed \(est\.\)<\/div><div>N\/A/);
const finished = {...running, thinkingLive:false, completionDurationMs:17500, genDurationMs:10000};
assert.match(draw(finished), /Total time<\/div><div>17500ms/);
assert.match(draw(finished), /Generation speed \(est\.\)<\/div><div>~5\.0 tok\/s/);
console.log('Run quick-reference and persisted-clock regression: PASS');
