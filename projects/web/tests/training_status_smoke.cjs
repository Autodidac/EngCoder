'use strict';
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');

class Element {
  constructor() { this.children = []; this.style = {}; this.textContent = ''; }
  set innerHTML(value) { assert.equal(value, ''); this.children = []; this.textContent = ''; }
  appendChild(child) { this.children.push(child); }
}
const host = new Element();
const controls = Object.fromEntries(['initializeModel', 'startTraining', 'pauseTraining',
  'resumeTraining', 'cancelTraining', 'trainingProfile', 'trainingScope', 'trainingSteps',
  'checkpointSteps', 'refreshTraining'].map(id => [id, new Element()]));
const context = vm.createContext({
  trainingState: null,
  renderTrainingRelationship() {},
  $: id => id === 'trainingStatus' ? host : controls[id],
  document: { createElement: () => new Element() },
});
const source = fs.readFileSync(path.resolve(__dirname, '../js/state.js'), 'utf8');
const start = source.indexOf('function renderTrainingStatus(payload)');
const end = source.indexOf('async function refreshTrainingStatus()', start);
assert.ok(start >= 0 && end > start);
vm.runInContext(source.slice(start, end), context);
const render = payload => {
  context.renderTrainingStatus(payload);
  return host.children.map(child => child.textContent).join('\n');
};
const stale = { phase: 'training', step: 90, maximum_steps: 100, percent: 90,
  training_data_scope: 'cpp23-core', message: 'Old job is still training' };

let text = render({ state: 'failed', running: false, error: '<failed worker>', progress: stale });
assert.match(text, /^failed/);
assert.match(text, /<failed worker>/, 'errors remain literal text');
assert.doesNotMatch(text, /Old job|90|cpp23/);
assert.equal(host.children.length, 2, 'a failed job must not retain a progress bar');

text = render({ state: 'rejected', running: false, status: 'rejected', executed_steps: 1,
  planned_maximum_steps: 1, training_data_scope: 'keymaker-foundation', progress: stale });
assert.match(text, /^rejected · keymaker-foundation · step 1 \/ 1/);
assert.match(text, /Active weights were not replaced/);
assert.doesNotMatch(text, /Old job|90|cpp23/);

text = render({ state: 'completed', status: 'promoted', running: false, progress: stale });
assert.equal(text, 'promoted');
text = render({ state: 'cancelled', running: false, progress: stale });
assert.equal(text, 'cancelled');

for (const state of ['queued', 'paused', 'paused-for-inference', 'idle']) {
  text = render({ state, running: state !== 'idle', progress: stale });
  assert.equal(text, state);
  assert.equal(host.children.length, 1);
}

text = render({ state: 'running', running: true, max_steps: 10,
  progress: { phase: 'complete', percent: 100, message: 'Previous model promoted' } });
assert.equal(text, 'running · step 0 / 10');
assert.equal(host.children.length, 1);

text = render({ state: 'running', running: true, steps: 99,
  progress: { phase: 'training', step: 0, maximum_steps: 10, percent: 0,
    training_data_scope: 'keymaker-foundation', message: 'Training current candidate' } });
assert.match(text, /^training · keymaker-foundation · step 0 \/ 10/);
assert.match(text, /Training current candidate/);
assert.equal(host.children[1].children[0].style.width, '0%');

text = render({ enabled: false, state: 'idle', running: false, progress: stale });
assert.equal(text, 'disabled\nTraining is disabled for this runtime.');
for (const [id, control] of Object.entries(controls)) {
  assert.equal(control.disabled, id !== 'refreshTraining', `${id}: disabled runtime`);
}
render({ enabled: true, state: 'idle', running: false });
assert.equal(controls.initializeModel.disabled, false);
assert.equal(controls.startTraining.disabled, false);
assert.equal(controls.pauseTraining.disabled, true);
assert.equal(controls.resumeTraining.disabled, true);
assert.equal(controls.cancelTraining.disabled, true);
render({ enabled: true, state: 'running', running: true });
assert.equal(controls.initializeModel.disabled, true);
assert.equal(controls.startTraining.disabled, true);
assert.equal(controls.pauseTraining.disabled, false);
assert.equal(controls.resumeTraining.disabled, true);
assert.equal(controls.cancelTraining.disabled, false);
assert.equal(controls.trainingProfile.disabled, true);
render({ enabled: true, state: 'paused', running: true });
assert.equal(controls.pauseTraining.disabled, true);
assert.equal(controls.resumeTraining.disabled, false);
assert.equal(controls.cancelTraining.disabled, false);
render({ enabled: true, state: 'paused-for-inference', running: true });
assert.equal(controls.pauseTraining.disabled, true);
assert.equal(controls.resumeTraining.disabled, true, 'manual resume must not override inference priority');
assert.equal(controls.cancelTraining.disabled, false);
assert.equal(controls.refreshTraining.disabled, false);
render(null);
assert.equal(controls.startTraining.disabled, true);
assert.equal(controls.refreshTraining.disabled, false);
console.log('PASS: native lifecycle wins over stale progress; disabled/running/paused controls, terminal outcomes, and live zero-step progress stay accurate');
