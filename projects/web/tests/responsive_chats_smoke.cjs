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
assert.match(html, /id="activityResize"[^>]+role="separator"[^>]+aria-orientation="vertical"/);
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
const values = new Map();
const thinkingDrawer = {classList: classes(), getBoundingClientRect: () => ({width: Math.min(parseInt(values.get('--activity-w')) || 400, viewport - 28)})};
const activityEvents = {};
const activityResize = {
  attrs: {}, captured: null,
  setAttribute(key, value) { this.attrs[key] = value; },
  addEventListener(name, listener) { activityEvents[name] = listener; },
  setPointerCapture(id) { this.captured = id; },
  hasPointerCapture(id) { return this.captured === id; },
  releasePointerCapture() { this.captured = null; }
};
const toggleDrawerBtn = {
  classList: classes(),
  attrs: {},
  setAttribute(k, v) { this.attrs[k] = v; },
  addEventListener(name, listener) { clicks[name] = listener; }
};
const toggleThinkingDrawerBtn = {classList: classes(), setAttribute() {}};
const storage = new Map();
const frames = new Map();
let nextFrame = 0;
const windowObj = { get innerWidth() {return viewport;} };
const ctx = vm.createContext({
  window: windowObj, drawer, thinkingDrawer, activityResize, toggleDrawerBtn, toggleThinkingDrawerBtn,
  requestAnimationFrame(fn) { frames.set(++nextFrame, fn); return nextFrame; },
  cancelAnimationFrame(id) { frames.delete(id); },
  localStorage: {setItem(key, value) { storage.set(key, value); }, getItem: key => storage.get(key)},
  STORAGE_KEYS: {activityPanelWidth: 'activityWidth'},
  document: {
    body: {classList: classes()},
    documentElement: {style: {setProperty(k, v) { values.set(k, v); }, removeProperty(k) { values.delete(k); }}}
  },
  AUTO_CLOSE_CHAT_WIDTH: 1220, MIN_APP_WIDTH: 560,
  autoresize() {layouts++;}
});
const stateBlock = state.slice(state.indexOf('let previousShellWidth'), state.indexOf("chatEl.addEventListener('scroll'"));
const uiBlock = ui.slice(ui.indexOf("toggleDrawerBtn.addEventListener('click'"), ui.indexOf("toggleThinkingDrawerBtn.addEventListener('click'"));
const splitter = ui.slice(ui.indexOf('let activityDrag ='), ui.indexOf("document.addEventListener('click'"));
vm.runInContext(stateBlock + '\n' + uiBlock + '\n' + splitter + '\nthis.layout = applyResponsiveShell;', ctx);
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
assert.equal(values.get('--app-offset'), '-207px', 'main chat is centered in the space outside the dock');
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

resize(1400);
thinkingDrawer.classList.add('open');
ctx.layout();
const move = (x, buttons = 1) => activityEvents.pointermove({pointerId: 1, clientX: x, buttons});
const down = () => activityEvents.pointerdown({pointerId: 1, button: 0, clientX: 1000});
const up = () => activityEvents.pointerup({pointerId: 1});
const paint = () => { for (const [id, callback] of frames) { frames.delete(id); callback(); } };
assert.equal(activityResize.hidden, false);
down();
move(880);
move(850);
assert.equal(frames.size, 1, 'pointer updates share one frame');
paint();
assert.equal(values.get('--activity-w'), '550px');
assert.equal(activityResize.attrs['aria-valuenow'], '550');
assert.equal(storage.has('activityWidth'), false);
move(850, 0);
up();
assert.equal(values.has('--activity-w'), false, 'lost button ownership rolls back the pending width');
assert.equal(storage.has('activityWidth'), false, 'stale release must not commit');
down();
move(800);
up();
assert.equal(storage.get('activityWidth'), '600');
down();
move(500);
activityResize.captured = null;
activityEvents.lostpointercapture({pointerId: 1});
paint();
assert.equal(values.get('--activity-w'), '600px', 'lost capture cancels queued paint');
activityEvents.keydown({key: 'Home', preventDefault() {}});
assert.equal(storage.get('activityWidth'), '320', 'the separator is keyboard adjustable');
resize(620);
assert.equal(activityResize.hidden, true);
assert.equal(values.get('--app-width'), '588px');
assert.equal(values.get('--app-offset'), '0px', 'narrow windows use an overlay');
resize(1400);
assert.equal(values.get('--activity-w'), '320px', 'compact layout preserves the preferred width');
console.log('PASS: responsive Chats and output docking, pointer cancellation, keyboard resizing, and remote token source');
