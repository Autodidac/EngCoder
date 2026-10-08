'use strict';
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
class Node {
  constructor() {
    this.children = [];
    this.textContent = '';
    this.value = '';
    this.open = false;
    this.classList = { add() {}, remove() {} };
    this.listeners = new Map();
  }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = [...children]; }
  addEventListener(type, handler) { this.listeners.set(type, handler); }
  trigger(type) { const handler = this.listeners.get(type); assert.ok(handler, `missing ${type}`); handler(); }
  showModal() { this.open = true; }
  close() { this.open = false; }
  focus() { this.focused = true; }
}
const els = Object.fromEntries(['quickMissions', 'missionsDialog', 'closeMissions', 'missionSearch',
  'missionPriority', 'missionList', 'missionCount', 'missionCaption'].map(id => [id, new Node()]));
els.missionPriority.value = 'all';
const ctx = vm.createContext({document: {
  getElementById: id => els[id],
  createElement: () => new Node()
}});
for (const script of ['mission_data.js', 'missions.js']) {
  vm.runInContext(fs.readFileSync(path.join(root, 'js', script), 'utf8'), ctx);
}
const allCount = Number(els.missionCount.textContent);
assert.ok(allCount > 15, 'older missions should be carried forward');
assert.equal(els.missionList.children.length, allCount);
els.quickMissions.trigger('click');
assert.ok(els.missionsDialog.open);
assert.ok(els.missionSearch.focused);
els.missionPriority.value = 'P0';
els.missionPriority.trigger('change');
assert.ok(els.missionList.children.length > 2);
assert.ok(els.missionList.children.length < allCount);
els.missionSearch.value = 'words-no-mission-matches';
els.missionSearch.trigger('input');
assert.equal(els.missionList.children.length, 1);
assert.equal(els.missionList.children[0].textContent, 'No matching missions.');
els.closeMissions.trigger('click');
assert.equal(els.missionsDialog.open, false);
console.log(`PASS: ${allCount} open missions, main-footer button, modal lifecycle, priority/search filters, safe text rendering`);
