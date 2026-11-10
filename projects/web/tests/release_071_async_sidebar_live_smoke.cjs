'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const render = fs.readFileSync(path.join(root, 'js/render.js'), 'utf8');
const api = fs.readFileSync(path.join(root, 'js/api.js'), 'utf8');
const ui = fs.readFileSync(path.join(root, 'js/ui.js'), 'utf8');
const css = fs.readFileSync(path.join(root, 'css/engcoder.css'), 'utf8');
const frames = [];
let rendered = 0;
let layout = 0;
let shell = 0;
const classes = new Set();
const drawer = {classList: {
  add(x) { classes.add(x); },
  remove(x) { classes.delete(x); },
  contains(x) { return classes.has(x); }
}};
const ctx = vm.createContext({
  thinkingDrawer: drawer,
  activeThinkingState: null,
  rightPanelView: 'output',
  requestAnimationFrame(cb) { frames.push(cb); return frames.length; },
  syncThinkingDrawer() { rendered++; },
  applyResponsiveShell() { layout++; },
  syncShellLayout() { shell++; }
});
const opening = render.slice(render.indexOf('// Opening the inspector is UI-only:'), render.indexOf('function createMessageShell('));
assert.ok(opening.startsWith('// Opening the inspector is UI-only:'), 'missing async drawer implementation');
vm.runInContext(opening + '\nthis.open=openThinkingDrawer; this.schedule=scheduleRightDrawerRender', ctx);
// A pending native model-load promise must not hold the inspector open callback.
const unresolvedLoad = new Promise(() => {});
void unresolvedLoad;
ctx.open(null);
assert.ok(classes.has('open'), 'CSS drawer must open immediately, even with no chat model selected');
assert.equal(ctx.rightPanelView, 'qwen', 'open the live output view while no task is selected');
assert.equal(rendered, 0, 'heavy output rendering must not block the click');
assert.equal(layout, 0, 'geometry measurements must not block the click');
assert.equal(shell, 1, 'only cheap class/aria sync runs immediately');
assert.equal(frames.length, 1, 'render scheduled after paint');
ctx.schedule(); ctx.schedule();
assert.equal(frames.length, 1, 'multiple fragments must coalesce into one paint');
frames.shift()();
assert.equal(rendered, 1); assert.equal(layout, 1);
classes.delete('open');
ctx.schedule(); frames.shift()();
assert.equal(rendered, 1, 'late render must not reopen a closed drawer');
assert.match(ui, /scheduleRightDrawerRender\(\)/, 'switching tabs must not block the click');
const labOpener = ui.slice(ui.indexOf('function openLab()'), ui.indexOf("$('labBtn')"));
assert.match(labOpener, /labDialog\.show\(\)/, 'Lab must be non-modal so the inspector can open during loading');
assert.doesNotMatch(labOpener, /labDialog\.showModal\(/);
assert.match(css, /body\.activity-open \.thinking-drawer \{ z-index: 26;/);
assert.match(api, /scheduleRightDrawerRender\(\{ layout: true \}\)/, 'external model streams must open asynchronously');
assert.match(api, /let renderPending = false;/);
assert.match(api, /processedEvents % 128 === 0/);
assert.match(api, /await new Promise\(resolve => setTimeout\(resolve, 0\)\)/);
assert.match(api, /if \(renderPending\) updateAssistantMessage\(assistantBody, assistantState\)/);
assert.match(api, /if \(progressPending\) persistProgress\(\)/);
assert.match(api, /if \(terminal\) \{[\s\S]*?updateAssistantMessage\(assistantBody, assistantState\);[\s\S]*?persistProgress\(\);[\s\S]*?return terminal/);
console.log('PASS: sidebar opens without model-load wait; rendering deferred/coalesced; text batched without losing terminal persistence');
