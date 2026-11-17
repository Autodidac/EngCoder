'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const js = path.resolve(__dirname, '../js');
const api = fs.readFileSync(path.join(js, 'api.js'), 'utf8');
const render = fs.readFileSync(path.join(js, 'render.js'), 'utf8');
const section = (source, start, end) => source.slice(source.indexOf(start), source.indexOf(end));
const cases = [
  {candidateIteration: true},
  {activityEvents: [{type: 'iteration.started'}]}
];
const event = (type, sequence, data) => ({type, sequence, data, task_id: 't'});
const batches = [
  [event('tool.approval_required', 1, {id: 'edit', name: 'workspace.patch'})],
  [event('task.awaiting_approval', 2, {call_id: 'approval_one', tool_call_id: 'edit', tool: 'workspace.patch', arguments: {path: 'test.cpp'}})],
  [event('tool.approved', 3, {id: 'edit', approval_id: 'approval_old'})],
  [event('tool.approved', 4, {id: 'edit', approval_id: 'approval_one'})],
  [event('task.cancelled', 5, {})]
];
const messages = [{role: 'user', content: 'Repair a candidate'}, {role: 'assistant'}, {role: 'user', content: 'Keep this message'}];
const errors = [];
class Element {
  constructor() { this.childNodes = []; this.dataset = {}; }
  appendChild(child) { this.insertBefore(child, null); }
  append(...children) { children.forEach(child => this.appendChild(child)); }
  insertBefore(child, next) {
    child.parent = this;
    this.childNodes.splice(next ? this.childNodes.indexOf(next) : this.childNodes.length, 0, child);
  }
  remove() { this.parent.childNodes.splice(this.parent.childNodes.indexOf(this), 1); }
  querySelector() { return this.childNodes.find(child => child.className === 'approval-card') || null; }
  setAttribute() {}
  addEventListener() {}
}
const context = vm.createContext({
  controller: new AbortController(), currentChat: () => ({messages}), editingMessageIndex: -1,
  promptEl: {value: ''}, pendingAttachments: [], $: () => null, showError: message => errors.push(message),
  window: {confirm: () => assert.fail('Candidate retry must not offer to discard chat history')},
  engcoderBase: () => 'http://localhost:14321',
  fetch: async () => ({ok: true, json: async () => ({events: batches.shift()})}),
  eventActivityLine: () => '', recordExecutionEvent: () => {}, captureRawOutput: () => {}, appendActivity: () => {},
  setStatus: () => {}, updateAssistantMessage: () => {}, setTimeout, clearTimeout, DOMException,
  formatCompletionTime: () => '', formatTimestampFull: () => '', ensureExecutionStats: () => ({}), estimateTokenCount: () => 0,
  document: {createElement: () => new Element()}, cleanAssistantContent: value => value,
});
vm.runInContext([
  section(render, 'function isCandidateReply(', 'function canSaveAssistantForLearning('),
  section(api, 'function terminalFromEvent(', 'async function resolveTaskApproval('),
  section(api, 'async function pollEngCoderTask(', 'function assistantMessageSnapshot('),
  section(api, 'async function sendMessage(', 'async function stopStreaming('),
  section(render, 'function renderAssistantBody(', 'function addTextMessage('),
  'this.test = { sendMessage, pollEngCoderTask, finalizeAssistantState, renderAssistantBody };'
].join('\n'), context);

(async () => {
  const state = {};
  const approvals = [];
  const timing = {startedAt: Date.now()};
  const result = await context.test.pollEngCoderTask({task_id: 't', session_id: 's'}, {}, state, timing,
    () => approvals.push(state.approvalRequest?.callId || null));
  assert.equal(result.kind, 'cancelled');
  assert.equal(approvals[0], null, 'policy event alone must not make the card actionable');
  assert.equal(approvals[1], 'approval_one');
  assert.equal(approvals[2], 'approval_one', 'an old decision cannot clear the current approval card');
  assert.equal(approvals[3], null, 'the matching host approval ID clears the card');
  context.test.finalizeAssistantState(state, timing, 'engcoder-cancelled', 'fixture');
  assert.equal(state.approvalRequest, null, 'terminal runs must clear the approval card');

  const container = new Element();
  const message = {approvalRequest: {callId: 'one', tool: 'workspace.patch'}};
  const renderApproval = () => context.test.renderAssistantBody(container, message);
  renderApproval();
  const card = container.querySelector();
  assert.ok(card);
  renderApproval();
  assert.equal(container.querySelector(), card, 'an activity refresh must keep the approval controls mounted');
  message.approvalRequest = {callId: 'two', tool: 'workspace.patch'};
  renderApproval();
  assert.notEqual(container.querySelector(), card, 'a new approval needs its own controls');
  message.approvalRequest = null;
  renderApproval();
  assert.equal(container.querySelector(), null);

  context.controller = null;
  for (const provenance of cases) {
    messages[1] = {role: 'assistant', ...provenance};
    await context.test.sendMessage({regenerate: true, targetIndex: 1});
    assert.match(errors.at(-1), /Start a fresh candidate run/);
    assert.equal(messages.length, 3, 'blocked retry must leave the transcript untouched');
  }
  console.log('PASS: candidate retry remains opt-in; approval requires a registered call and clears on completion');
})().catch(error => {console.error(error); process.exitCode = 1;});
