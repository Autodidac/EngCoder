'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const api = fs.readFileSync(path.join(root, 'js/api.js'), 'utf8');
const stateSrc = fs.readFileSync(path.join(root, 'js/state.js'), 'utf8');
const render = fs.readFileSync(path.join(root, 'js/render.js'), 'utf8');
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');

const moduleSrc = api.slice(0, api.indexOf('function formatCompletionTime'));
const ctx = vm.createContext({});
vm.runInContext(moduleSrc + '\nthis.capture = captureRawOutput', ctx);
const push = (s, type, delta, sequence) => ctx.capture(s, {type, data: {delta}, sequence});
const s = { rawOutput: '', content: '' };
push(s, 'task.created', undefined, 1);
push(s, 'controller.decision', undefined, 2);
push(s, 'residency.prepared', undefined, 3);
assert.equal(s.rawOutput, '');
assert.equal(s.lastEventSequence, 3);
push(s, 'model.text_delta', 'Hello ', 4);
push(s, 'model.text_delta', 'world', 5);
push(s, 'model.reasoning_delta', '\nConsider the task.', 6);
push(s, 'model.tool_argument_delta', '\n{"path":', 7);
push(s, 'model.tool_argument_delta', '"src"}', 8);
assert.equal(s.rawOutput, 'Hello world\nConsider the task.\n{"path":"src"}');
assert.equal(s.rawOutputFormat, 'generated-v1');
push(s, 'controller.decision', undefined, 9);
assert.equal(s.lastEventSequence, 9);
assert.equal(s.rawOutput.includes('controller.decision'), false);

const legacy = {rawOutput: '[residency.prepared] {"profile":{}}', content: 'Existing reply', lastEventSequence: 17};
push(legacy, 'task.updated', undefined, 18);
assert.equal(legacy.rawOutput, 'Existing reply');
push(legacy, 'model.text_delta', ' new', 19);
assert.equal(legacy.rawOutput, 'Existing reply new');
assert.equal(legacy.lastEventSequence, 19);

// Old saved runs must not restore their previous JSON into the new stream.
assert.match(stateSrc, /rawOutputFormat === 'generated-v1'/);
assert.match(stateSrc, /base\.rawOutput = base\.rawOutputFormat/);
assert.match(api, /rawOutputFormat: GENERATED_OUTPUT_FORMAT/);
assert.match(render, /rawOutputFormat: msg\.rawOutputFormat/);
assert.match(html, /<h3>Generated output<\/h3>/);
assert.doesNotMatch(render.slice(render.indexOf('function syncThinkingDrawer()'), render.indexOf('function openThinkingDrawer(')), /Runtime timeline|Models & routing|sectionHtml|runtimeEvents|JSON\.stringify/);

// Rendering must update the same <pre>, not replace the user's scroll position
// every time another fragment arrives.
const node = {textContent: '', scrollHeight: 80, scrollTop: 0, clientHeight: 80, dataset: {}};
const panel = {child: null,
  querySelector() { return this.child; },
  replaceChildren() { this.child = null; },
  appendChild(child) { this.child = child; },
  set textContent(v) { this.child = null; this.text = v; },
};
const view = vm.createContext({
  activeThinkingState: {rawOutputFormat: 'generated-v1', rawOutput: 'First', thinkingLive: true},
  thinkingDrawerBody: panel, thinkingStateEl: { textContent: '' },
  document: {createElement() { return node; }}
});
const drawSrc = render.slice(render.indexOf('function syncGeneratedOutput()'), render.indexOf('function syncRunInfo()'));
vm.runInContext(drawSrc + '\nthis.draw = syncGeneratedOutput', view);
view.draw();
assert.equal(panel.child, node);
assert.equal(node.textContent, 'First');
view.activeThinkingState.rawOutput = 'First second';
view.draw();
assert.equal(panel.child, node);
assert.equal(node.textContent, 'First second');
node.scrollHeight = 500;
node.clientHeight = 100;
node.scrollTop = 30;
view.activeThinkingState.rawOutput = 'First second third';
view.draw();
assert.equal(node.scrollTop, 30, 'manual scrolling should be preserved');
assert.equal(node.textContent, 'First second third');
console.log('PASS: generated fragments only; no telemetry; legacy migration; event cursor; in-place scroll-safe panel');
