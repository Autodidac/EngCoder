'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.resolve(__dirname, '../js/render.js'), 'utf8');

class Element {
  constructor() {
    this.childNodes = []; this.dataset = {}; this.events = {}; this.attributes = {};
    this.classList = { toggle() {}, add() {} };
  }
  append(...children) { children.forEach(child => this.appendChild(child)); }
  appendChild(child) { this.insertBefore(child, null); }
  insertBefore(child, next) {
    child.parentElement = this;
    this.childNodes.splice(next ? this.childNodes.indexOf(next) : this.childNodes.length, 0, child);
  }
  remove() { this.parentElement.childNodes.splice(this.parentElement.childNodes.indexOf(this), 1); }
  setAttribute(name, value) { this.attributes[name] = value; }
  addEventListener(name, callback) { this.events[name] = callback; }
  querySelector(selector) {
    const name = selector.replace(':scope > ', '').slice(1);
    for (const child of this.childNodes) {
      if ((child.className || '').split(' ').includes(name)) return child;
      const nested = child.querySelector(selector);
      if (nested) return nested;
    }
    return null;
  }
}
const errors = [];
let saves = 0;
let removeResult = {cancelled: true};
let banResult = {cancelled: true};
let fetchFailure = false;
const message = {role: 'assistant', content: 'Hello.', sourcePrompt: 'Hi.', runtimeTaskId: 't',
  runtimeSessionId: 's', completionReason: 'engcoder-task', showRegenerate: true};
const persisted = {...message};
const chat = {messages: [persisted]};
const context = vm.createContext({
  document: {createElement: () => new Element()}, $: () => null,
  cleanAssistantContent: value => value, currentChat: () => chat,
  showError: value => errors.push(value), saveAll: () => saves++,
  refreshLearningStatus: async () => {}, engcoderBase: () => 'http://localhost:14321',
  renderFormattedContent: () => {}, controller: null,
  removeLearningEpisode: async () => {
    if (removeResult instanceof Error) throw removeResult;
    return removeResult;
  },
  banLearningEpisode: async () => {
    if (banResult instanceof Error) throw banResult;
    return banResult;
  },
  fetch: async (_url, options) => {
    if (fetchFailure) return {ok: false, status: 503, json: async () => ({error: 'Offline'})};
    return options?.method === 'POST'
      ? {ok: true, status: 201, json: async () => ({stored: true})}
      : {ok: true, status: 200, json: async () => ({items: [{prompt: 'Hi.', response: 'Hello.', content_hash: 'h'}]})};
  }
});
vm.runInContext(source.slice(source.indexOf('function setVerifyButtonState('), source.indexOf('function addTextMessage(')) +
  '\nthis.test = {renderAssistantBody};', context);
const container = new Element();
const buttons = () => container.querySelector('.assistant-actions').childNodes;
const find = title => buttons().find(button => button.title === title);
const click = async button => {
  assert.ok(button);
  button.events.click();
  await new Promise(resolve => setImmediate(resolve));
};
(async () => {
  context.test.renderAssistantBody(container, message);
  assert.equal(find('Remove from learning'), undefined);
  await click(find('Mark this response verified for EngCoder learning'));
  assert.equal(message.verifiedForLearning, true);
  assert.equal(persisted.verifiedForLearning, true);
  assert.ok(find('Remove from learning'), 'Save immediately renders management controls');
  const afterSave = saves;

  await click(find('Remove from learning'));
  assert.equal(message.verifiedForLearning, true, 'Cancel must preserve saved status');
  assert.equal(saves, afterSave);
  assert.ok(find('Remove from learning'));
  assert.ok(buttons().every(button => !button.disabled));

  fetchFailure = true;
  await click(find('Remove from learning'));
  assert.match(errors.at(-1), /Offline/);
  assert.equal(message.verifiedForLearning, true);
  fetchFailure = false;
  banResult = new Error('Ban failed');
  await click(find('Remove and ban from all future learning'));
  assert.match(errors.at(-1), /Ban failed/);
  assert.equal(message.verifiedForLearning, true);
  assert.ok(buttons().every(button => !button.disabled));

  removeResult = {removed: true};
  await click(find('Remove from learning'));
  assert.equal(message.verifiedForLearning, false);
  assert.equal(persisted.verifiedForLearning, false);
  assert.equal(find('Remove from learning'), undefined);
  assert.ok(find('Mark this response verified for EngCoder learning'));
  console.log('PASS: save renders controls; cancelled/failed removal preserves evidence state; success updates persisted feedback');
})().catch(error => {console.error(error); process.exitCode = 1;});
