'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const js = path.resolve(__dirname, '../js');
const api = fs.readFileSync(path.join(js, 'api.js'), 'utf8');
const main = fs.readFileSync(path.join(js, 'main.js'), 'utf8');
const section = (start, end) => api.slice(api.indexOf(start), api.indexOf(end));
const runtime = 'http://localhost:14321';
const response = (status, body) => ({ok: status >= 200 && status < 300, status, json: async () => body});

function harness() {
  const statuses = [];
  const errors = [];
  const app = vm.createContext({
    AbortController, AbortSignal, DOMException,
    setTimeout: callback => queueMicrotask(callback), clearTimeout: () => {},
    setStatus: text => statuses.push(text), showError: text => errors.push(text),
    engcoderBase: () => runtime, $: () => null,
    updateAssistantMessage: () => {}, setBusy: () => {}, maybeScroll: () => {},
    saveAll: () => {}, saveChats: () => {}, scheduleChatSave: () => {},
    appendActivity: () => {}, cleanAssistantContent: text => text,
    conciseRuntimeError: text => text, isCandidateReply: () => false,
    controller: null, activeEngCoderTaskId: null, activeEngCoderSessionId: null,
    stopRequested: false,
  });
  vm.runInContext([
    section('function terminalFromEvent(', 'async function submitEngCoderTask('),
    section('async function pollEngCoderTask(', 'function finalizeAssistantState('),
    section('async function resumeSavedEngCoderTask(', '// Host-wide Qwen output observer'),
    'this.actions = { sendMessage, stopStreaming, resumeSavedEngCoderTask, pollEngCoderTask, resolveTaskApproval };'
  ].join('\n'), app);
  return {app, statuses, errors, ...app.actions};
}

async function testStopDuringSubmission(terminalKind) {
  const {app, statuses, errors, sendMessage, stopStreaming} = harness();
  const chat = {name: 'Test', messages: []};
  const requests = [];
  const savedTasks = [];
  const assistantState = {content: '', modelRoles: {}, taskStartedAtMs: Date.now()};
  let acceptSubmission;
  let settlePoll;
  let loseConnection;
  Object.assign(app, {
    currentChat: () => chat, editingMessageIndex: -1, pendingAttachments: [],
    promptEl: {value: 'Hello'}, renderPendingAttachments: () => {}, autoresize: () => {},
    renderActiveChat: () => {}, makeRuntimeSessionId: () => 'session',
    attachmentTaskText: text => text, freshExecutionStats: () => ({}),
    addTextMessage: () => ({closest: () => ({__assistantState: assistantState})}),
    ensureExecutionStats: state => state,
    assistantMessageSnapshot: state => ({role: 'assistant', ...state}),
    saveChats: () => savedTasks.push(chat.messages.at(-1)?.runtimeTaskId),
    finalizeAssistantState: (state, timing, reason) => { state.completionReason = reason; },
    handleAssistantVoiceReply: () => {},
    submitEngCoderTask: () => new Promise(resolve => { acceptSubmission = resolve; }),
    pollEngCoderTask: () => new Promise((resolve, reject) => {
      settlePoll = resolve;
      loseConnection = reject;
    }),
    fetch: async (url, options) => {
      requests.push([url, options.method]);
      return response(terminalKind === 'completed' ? 404 : 202);
    },
  });

  const running = sendMessage();
  const signal = app.controller.signal;
  await stopStreaming();
  assert.equal(statuses.at(-1), 'Stopping');
  assert.equal(signal.aborted, false, 'Stop must wait for the task id');
  assert.equal(requests.length, 0);

  acceptSubmission({task_id: 't', session_id: 'session'});
  await new Promise(resolve => setImmediate(resolve));
  assert.ok(savedTasks.includes('t'), 'Persist the task id before relying on polling');
  assert.deepEqual(requests, [[runtime + '/api/tasks/t/cancel', 'POST']]);
  assert.equal(signal.aborted, false, 'Stop must keep the terminal event poll alive');
  await stopStreaming();
  assert.equal(requests.length, 1, 'Repeated Stop must not send a duplicate request');
  if (terminalKind === 'disconnected') loseConnection(new Error('offline'));
  else settlePoll({kind: terminalKind, result: 'A complete reply'});
  await running;
  if (terminalKind === 'disconnected') {
    assert.equal(statuses.at(-1), 'Disconnected');
    assert.match(errors.at(-1), /Refresh to reconnect/);
    assert.equal(chat.messages.at(-1).runtimeTaskId, 't');
    assert.equal(chat.messages.at(-1).completionReason, undefined);
  } else {
    assert.equal(errors.at(-1), '');
    assert.equal(statuses.at(-1), terminalKind === 'completed' ? 'Ready' : 'Stopped');
  }
}

async function testRetryStop() {
  const {app, statuses, stopStreaming} = harness();
  app.controller = new AbortController();
  app.activeEngCoderTaskId = 't';
  app.fetch = async () => response(503);
  await stopStreaming();
  assert.match(statuses.at(-1), /Stop not confirmed · HTTP 503 · Try again/);
  assert.equal(app.controller.signal.aborted, false);
  app.fetch = async () => response(202);
  await stopStreaming();
  assert.equal(statuses.at(-1), 'Stopping');
}

async function testResumeApproval() {
  const {app, statuses, resumeSavedEngCoderTask, resolveTaskApproval} = harness();
  const last = {role: 'assistant', runtimeTaskId: 't', runtimeSessionId: 'original',
    approvalRequest: {pending: true, taskId: 't', callId: 'patch'}};
  const chat = {runtimeSessionId: 'different', messages: [last]};
  const state = {};
  Object.assign(app, {
    currentChat: () => chat,
    chatEl: {querySelectorAll: () => [{closest: () => ({__assistantState: state})}]},
    assistantMessageSnapshot: value => ({...value}),
    finalizeAssistantState: () => {},
    pollEngCoderTask: async submission => {
      assert.equal(submission.session_id, 'original');
      assert.equal(state.approvalRequest.pending, false);
      assert.equal(app.activeEngCoderSessionId, 'original');
      return {kind: 'cancelled'};
    },
  });
  await resumeSavedEngCoderTask();
  assert.equal(statuses.at(-1), 'Stopped');

  let reply;
  app.fetch = () => new Promise(resolve => { reply = resolve; });
  const submitting = resolveTaskApproval(state, true, {});
  state.approvalRequest = {taskId: 't', callId: 'new'};
  reply(response(202, {ok: true}));
  await submitting;
  assert.equal(statuses.at(-1), 'Stopped', 'A stale HTTP acknowledgment must not change the current status');
  assert.equal(state.approvalRequest.callId, 'new');
  state.approvalRequest.submitted = true;
  app.fetch = () => assert.fail('A submitted approval should not POST again');
  await resolveTaskApproval(state, true, {});
}

async function testInvalidEvents() {
  const {app, pollEngCoderTask} = harness();
  app.controller = new AbortController();
  let polls = 0;
  app.fetch = async () => { polls += 1; return response(200, {events: {}}); };
  await assert.rejects(pollEngCoderTask({task_id: 't', session_id: 's'}, {}, {}, {}), /invalid event response/);
  assert.equal(polls, 40, 'Malformed success responses must exhaust the reconnect budget');
}

(async () => {
  assert.ok(main.indexOf('void resumeSavedEngCoderTask()') > main.indexOf("setStatus(currentChat()"));
  await testStopDuringSubmission('cancelled');
  await testStopDuringSubmission('completed');
  await testStopDuringSubmission('disconnected');
  await testRetryStop();
  await testResumeApproval();
  await testInvalidEvents();
  console.log('PASS: Stop races, approval refresh and stale acknowledgments, and malformed task event responses');
})().catch(error => { console.error(error); process.exitCode = 1; });
