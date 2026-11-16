'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8');
const state = read('js/state.js');
const ui = read('js/ui.js');
const cpp = read('../native/src/model/native_model_manager.cpp');
const http = read('../native/src/server/http_server.cpp');

assert.match(state, /'Use existing GGUF…'/);
assert.match(state, /'Fix local GGUF path…'/);
assert.match(state, /linkInput\.placeholder = String\.raw/);
assert.match(state, /button\.dataset\.modelAction = action/);
assert.match(state, /'link-save'/);
assert.match(ui, /if \(action === 'link-save'\)/);
assert.match(state, /model && editingId === model\.id/, 'one-shot local override must not apply to another model');
assert.match(state, /openLinkPanels\.get\(linkPanel\.dataset\.linkModelUri\)/, 'refresh must restore the editor for the same model');
assert.match(http, /model-library\/link-local/);
assert.match(cpp, /existing->estimated_bytes - existing->estimated_bytes \/ 10U/);
assert.match(cpp, /linked\.source = ModelSourceKind::local_gguf/);
assert.match(cpp, /save_profile\(linked\)/);
assert.match(cpp, /normalize_local_gguf_path\(existing_file\)/);

const begin = state.indexOf('async function linkNativeModel(');
const end = state.indexOf('\nasync function installNativeModel', begin);
assert.ok(begin >= 0 && end > begin);
let call = null;
let form = 0;
let status = { textContent: '', dataset: {} };
let rejectLink = false;
const ctx = vm.createContext({
  postEngCoder: async (endpoint, body) => {
    call = { endpoint, body };
    if (rejectLink) throw new Error('invalid fixture path');
    return { id: 'qwen', runtime_reload_required: false, verified_bytes: 100 };
  },
  refreshNativeModelLibrary: async () => ({}),
  probeModels: async () => ({}),
  populateNativeModelForm: () => { form++; },
  $: id => id === 'registerNativeModel' ? { dataset: { editingId: 'qwen' } } :
     id === 'nativeModelInstallStatus' ? status : null,
});
vm.runInContext(state.slice(begin, end) + '\nthis.link = linkNativeModel;', ctx);
(async () => {
  const raw = String.raw`"C:\EpochEngine\models\Qwen-00002-of-00003.gguf"`;
  const panel = { hidden: false };
  await ctx.link('qwenflash://qwen', ` ${raw} `, panel);
  assert.equal(panel.hidden, true, 'a successful link closes the editor');
  assert.equal(call.endpoint, '/api/engcoder/model-library/link-local');
  assert.equal(call.body.model, 'qwenflash://qwen');
  assert.equal(call.body.path, raw);
  assert.equal(form, 1);
  assert.match(status.textContent, /No weights were copied or downloaded/);
  assert.equal(status.dataset.state, 'ready');
  panel.hidden = false;
  rejectLink = true;
  await assert.rejects(ctx.link('qwenflash://qwen', raw, panel), /invalid fixture path/);
  assert.equal(panel.hidden, false, 'a rejected link leaves the editor available for correction');
  console.log('PASS: existing GGUF link control, endpoint wiring, path handling, no cross-model override');
})().catch(e => { console.error(e); process.exitCode = 1; });
