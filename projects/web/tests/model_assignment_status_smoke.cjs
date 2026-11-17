'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.resolve(__dirname, '../js/state.js'), 'utf8');
class Element {
  constructor() { this.children = []; this.textContent = ''; }
  set innerHTML(value) { this.children = []; }
  append(...items) { this.children.push(...items); }
  appendChild(item) { this.children.push(item); }
}
const list = new Element();
const status = new Element();
const context = vm.createContext({
  $: id => id === 'modelRoleList' ? list : id === 'modelRoleStatus' ? status : null,
  document: {createElement: () => new Element()},
  runtimeArchitecture: {
    controller: {model: 'small', enabled: false},
    foundation: {model: 'Qwen3.5-9B'},
    addons: [{id: 'cpp', model: 'coder', enabled: true}],
    internal_host: {available: false},
    residency: {physical: {loaded: false}}
  }
});
vm.runInContext(source.slice(source.indexOf('function renderModelRoles()'), source.indexOf('function syncReviewModelChoices()')) +
  '\nthis.render = renderModelRoles;', context);
context.render();
assert.equal(list.children[0].children[1].textContent, 'Disabled');
assert.equal(list.children[1].children[1].textContent, 'Configured');
assert.match(status.textContent, /Assignments do not indicate loaded models/);
assert.doesNotMatch(status.textContent, /external models remain available/);
assert.ok(list.children.every(row => row.children[1].textContent !== 'Resident'));
context.runtimeArchitecture.foundation = {};
context.runtimeArchitecture.controller = {};
context.runtimeArchitecture.internal_host.available = true;
context.render();
assert.equal(list.children[0].children[1].textContent, 'Fallback');
assert.equal(list.children[1].children[1].textContent, 'Unassigned');
assert.match(status.textContent, /Internal EngCoder generation available/);
assert.match(status.textContent, /live status/);
console.log('PASS: model assignments never imply physical residency or external provider availability');
