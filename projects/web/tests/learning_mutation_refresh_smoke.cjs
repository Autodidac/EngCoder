'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const state = fs.readFileSync(path.resolve(__dirname, '../js/state.js'), 'utf8');
const render = fs.readFileSync(path.resolve(__dirname, '../js/render.js'), 'utf8');
let confirmed = true;
let postFailure = false;
let refreshFailure = true;
let posts = 0;
let saves = 0;
const errors = [];
const message = {role: 'assistant', sourcePrompt: 'Hi.', content: 'Hello.',
  runtimeTaskId: 'task', runtimeSessionId: 'session', verifiedForLearning: true};
const persisted = {...message};
const context = vm.createContext({
  window: {confirm: () => confirmed},
  postEngCoder: async () => {
    posts++;
    if (postFailure) throw new Error('Mutation failed');
    return {removed: true, banned: true};
  },
  refreshLearningStatus: async () => {
    if (refreshFailure) throw new Error('Refresh unavailable');
  },
  currentChat: () => ({messages: [persisted]}), cleanAssistantContent: value => value,
  showError: error => errors.push(error), saveAll: () => saves++,
  setVerifyButtonState: () => {}, engcoderBase: () => 'http://localhost',
  fetch: async () => ({ok: true, json: async () => ({items: [{prompt: 'Hi.', response: 'Hello.', content_hash: 'hash'}]})}),
});
vm.runInContext(state.slice(state.indexOf('async function refreshLearningAfterMutation('),
  state.indexOf('function renderLearningStatus(')), context);
vm.runInContext(render.slice(render.indexOf('async function removeAssistantLearning('),
  render.indexOf('function renderAssistantBody(')), context);
(async () => {
  const button = {};
  for (const ban of [false, true]) {
    message.verifiedForLearning = persisted.verifiedForLearning = true;
    await context.removeAssistantLearning(message, button, [button], ban, () => {});
    assert.equal(message.verifiedForLearning, false, 'Successful mutation survives refresh failure');
    assert.equal(persisted.verifiedForLearning, false);
    assert.equal(button.disabled, false);
    assert.match(errors.at(-1), /change saved.*could not refresh/);
  }
  assert.equal(saves, 2);
  const result = await context.unbanLearningItem({id: 'ban'});
  assert.equal(result.removed, true);
  assert.match(result.refresh_warning, /Refresh unavailable/);

  postFailure = true;
  message.verifiedForLearning = true;
  await context.removeAssistantLearning(message, button, [button], true, () => {});
  assert.equal(message.verifiedForLearning, true, 'Actual mutation failure preserves saved state');
  assert.match(errors.at(-1), /Mutation failed/);
  postFailure = false;
  confirmed = false;
  const beforeCancel = posts;
  await context.removeAssistantLearning(message, button, [button], false, () => {});
  assert.equal(message.verifiedForLearning, true);
  assert.equal(posts, beforeCancel);

  confirmed = true;
  refreshFailure = false;
  const clean = await context.removeLearningEpisode({content_hash: 'hash'});
  assert.equal(clean.removed, true);
  assert.equal(clean.refresh_warning, undefined);
  console.log('PASS: acknowledged learning mutations survive failed refresh; errors and cancellation remain distinct');
})().catch(error => { console.error(error); process.exitCode = 1; });
