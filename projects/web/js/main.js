// Set the app version in the header
const appVersion = document.getElementById('appVersion');
if (appVersion) appVersion.textContent = `v${APP_VERSION}`;

loadAll();
void resumeSavedEngCoderTask();
syncComposerControls();
syncComposerQuickBar();
applyAppearance();
void syncSharedTheme();
window.setInterval(() => { void syncSharedTheme(); }, 8000);
renderModelRoles();
refreshLastUserFromChat();
autoresize();
syncThinkingDrawer();
// Poll the bounded native provider feed so even external API/MCP calls are visible.
void pollQwenLiveOutput();
window.setInterval(() => { void pollQwenLiveOutput(); }, 500);
applyResponsiveShell();
setBusy(false);
updateVoiceButtons();
showFileOriginMicNotice();
setStatus(currentChat()?.messages.length ? 'Ready' : 'Idle', 'idle');

// Populate the runtime-owned EngCoder architecture without blocking first paint.
probeModels(false);


// The Windows desktop shell is the same web client in Edge app mode. Register
// that page with the embedded runtime so closing the app window shuts down the
// runtime even when Edge keeps a background browser process alive. Reloads are
// tolerated by the native grace period: the new page immediately re-opens the
// same desktop session before shutdown is committed.
(function installDesktopLifecycle() {
  const params = new URLSearchParams(window.location.search);
  if (params.get('desktop') !== '1') return;

  const token = params.get('desktop_token') || '';
  if (!token) return;

  const endpoint = '/api/desktop/state';
  const payload = open => JSON.stringify({ token, open });

  void fetch(endpoint, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: payload(true),
    keepalive: true
  }).catch(() => {});

  const close = () => {
    const body = payload(false);
    try {
      if (navigator.sendBeacon) {
        navigator.sendBeacon(endpoint, new Blob([body], { type: 'application/json' }));
        return;
      }
    } catch {}
    try {
      void fetch(endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body,
        keepalive: true
      });
    } catch {}
  };

  window.addEventListener('pagehide', close, { once: true });
})();
