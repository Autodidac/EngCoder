'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const read = name => fs.readFileSync(path.join(root, name), 'utf8');
const state = read('js/state.js');
const ui = read('js/ui.js');
const html = read('index.html');
const config = read('js/config.js');
const native = fs.readFileSync(path.resolve(root, '../native/src/server/http_server.cpp'), 'utf8');

assert.match(native, /#if defined\(_WIN32\)[\s\S]*?_dupenv_s\(&raw, &length, "ENGCODER_REMOTE_API_TOKEN"\)/);
assert.match(native, /std::unique_ptr<char, decltype\(&std::free\)> value/);
assert.match(html, /id="toggleDrawer"[^>]+aria-controls="drawer"[^>]+aria-expanded="false"/);
assert.doesNotMatch(config, /LOCK_CHAT_DRAWER_WIDTH/);

function classes() {
  const set = new Set();
  return {
    contains: name => set.has(name),
    add: name => set.add(name),
    remove: name => set.delete(name),
    toggle: (name, force) => {
      const next = force === undefined ? !set.has(name) : Boolean(force);
      if (next) set.add(name); else set.delete(name);
      return next;
    }
  };
}
let viewport = 1100;
let clicks = {};
let layouts = 0;
const drawer = {classList: classes(), getBoundingClientRect: () => ({width: Math.min(360, viewport - 28)})};
const thinkingDrawer = {classList: classes(), getBoundingClientRect: () => ({width: 400})};
const toggleDrawerBtn = {
  classList: classes(),
  attrs: {},
  setAttribute(k, v) { this.attrs[k] = v; },
  addEventListener(name, listener) { clicks[name] = listener; }
};
const toggleThinkingDrawerBtn = {classList: classes()};
const values = new Map();
const windowObj = { get innerWidth() {return viewport;} };
const ctx = vm.createContext({
  window: windowObj, drawer, thinkingDrawer, toggleDrawerBtn, toggleThinkingDrawerBtn,
  document: {
    body: {classList: classes()},
    documentElement: {style: {setProperty(k, v) { values.set(k, v); }}}
  },
  AUTO_CLOSE_CHAT_WIDTH: 1220, MIN_APP_WIDTH: 560,
  autoresize() {layouts++;}
});
const stateBlock = state.slice(state.indexOf('let previousShellWidth'), state.indexOf("chatEl.addEventListener('scroll'"));
const uiBlock = ui.slice(ui.indexOf("toggleDrawerBtn.addEventListener('click'"), ui.indexOf("toggleThinkingDrawerBtn.addEventListener('click'"));
vm.runInContext(stateBlock + '\n' + uiBlock + '\nthis.layout = applyResponsiveShell;', ctx);
assert.equal(typeof clicks.click, 'function');
const open = () => drawer.classList.contains('open');
const press = () => clicks.click();
const resize = width => {viewport = width; ctx.layout({ resized: true });};

ctx.layout();
press();
assert.ok(open(), 'Manually open at 1100px (below old close threshold)');
assert.equal(toggleDrawerBtn.attrs['aria-expanded'], 'true');
assert.equal(values.get('--app-width'), '694px', 'side-by-side when width permits');
ctx.layout();
assert.ok(open(), 'Unrelated render/layout must not dismiss chats');
thinkingDrawer.classList.add('open');
ctx.layout();
assert.ok(open(), 'Opening generated-output panel should not close Chats');
assert.equal(values.get('--app-width'), '654px', 'compact drawer overlays if both panels cannot fit');
thinkingDrawer.classList.remove('open');
ctx.layout();
resize(1099);
assert.ok(!open(), 'Narrowing a compact window collapses Chats');
assert.equal(toggleDrawerBtn.attrs['aria-expanded'], 'false');
press();
assert.ok(open(), 'Manually re-open immediately after auto-close');
resize(1150);
assert.ok(open(), 'Widening should not close Chats');
resize(1140);
assert.ok(!open(), 'Narrowing again closes Chats');
resize(620);
press();
assert.ok(open(), 'Narrow windows must still permit overlay opening');
assert.equal(values.get('--app-width'), '588px', 'do not shrink main chat under minimum for sidebar');
ctx.layout();
assert.ok(open(), 'Overlay remains open through ordinary UI refreshes');
press();
assert.ok(!open(), 'User can explicitly close overlay');
resize(1350);
press();
assert.ok(open(), 'Wide windows can open drawer');
resize(1250);
assert.ok(open(), 'Wide resize keeps drawer open until compact threshold');
resize(1210);
assert.ok(!open(), 'Crossing compact threshold on shrink closes drawer');
assert.ok(layouts >= 10);
assert.doesNotMatch(ui, /drawer\.classList\.remove\('open'\)/, 'outside clicks must not auto-dismiss Chats');
assert.match(ui, /applyResponsiveShell\(\{ resized: true \}\)/);
console.log('PASS: responsive Chats manual-open, resize-close, overlay, model-panel coexistence, accessibility, remote token source');
