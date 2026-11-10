// GPT-like composer: one clean row until the text needs more — either an
// explicit newline (Shift+Enter) or wrapping at the current width. When it
// grows, the buttons drop below (.multiline); when it fits in one line again,
// it shrinks back to the single row automatically.
//
// Anti-oscillation + no hidden text: the grow/shrink DECISION is always made
// in SINGLE-row geometry — the exact width a locked 44px row renders at. If
// the text fits one line there, rendering it in that same width provably
// shows every character (nothing clipped or tucked above the box). Anything
// wider goes to multi mode, where the HEIGHT is measured separately in the
// rendered (wider) multi geometry, so the box always matches its own lines.
// The mode is a pure function of the text — never of which mode we're
// currently in — so typing across the wrap boundary flips at most once per
// direction and can never alternate single→multi→single on keystrokes.
function autoresize() {
  const composerRow = promptEl.closest('.row');
  if (!composerRow) return;

  // Decide at SINGLE-row width: strip .multiline (no paint happens between
  // these statements, so nothing flickers), zero the height — and min-height,
  // because the stylesheet's 44px min would otherwise clamp it — so
  // scrollHeight reports exact content.
  composerRow.classList.remove('multiline');
  promptEl.style.minHeight = '0px';
  promptEl.style.height = '0px';
  const singleLineH = parseFloat(getComputedStyle(promptEl).lineHeight) || 44;
  // Above 1.5x one line's height the content provably spans >= 2 lines
  // (a single line never measures more than ~1 line-height, even with rounding).
  const fitsOneLine = !promptEl.value.includes('\n')
    && promptEl.scrollHeight <= singleLineH * 1.5;

  if (fitsOneLine) {
    // Single-line mode: locked clean row, never grows.
    promptEl.style.minHeight = '';
    promptEl.style.height = '44px';
    promptEl.style.overflowY = 'hidden';
    return;
  }

  // Multi mode: re-measure content height at the rendered (wider) width so
  // the box is exactly as tall as its own lines — never shorter.
  composerRow.classList.add('multiline');
  promptEl.style.minHeight = '0px';
  promptEl.style.height = '0px';
  const lineHeight = parseFloat(getComputedStyle(promptEl).lineHeight) || 20;
  const maxRows = 5;
  // Snap to whole lines (round, not ceil: sub-pixel overshoot must not add a
  // phantom blank line).
  const lines = Math.max(1, Math.min(maxRows, Math.round(promptEl.scrollHeight / lineHeight)));
  promptEl.style.height = `${lines * lineHeight}px`;
  promptEl.style.overflowY = promptEl.scrollHeight > lineHeight * maxRows ? 'auto' : 'hidden';
}


// Events
toggleDrawerBtn.addEventListener('click', () => {
  drawer.classList.toggle('open');
  applyResponsiveShell();
});
toggleThinkingDrawerBtn.addEventListener('click', () => {
  if (thinkingDrawer.classList.contains('open')) closeThinkingDrawer();
  else openThinkingDrawer(activeThinkingState);
});
for (const tab of [generatedOutputTab, qwenLiveTab, runInfoTab]) {
  tab?.addEventListener('click', () => {
    rightPanelView = tab.dataset.runView === 'info' ? 'info' : tab.dataset.runView === 'qwen' ? 'qwen' : 'output';
    // A tab click should never synchronously rebuild a large transcript.
    scheduleRightDrawerRender();
  });
}
closeThinkingDrawerBtn?.addEventListener('click', event => {
  event.preventDefault();
  event.stopPropagation();
  closeThinkingDrawer();
});
newChatBtn.addEventListener('click', newChat);
window.addEventListener('resize', () => applyResponsiveShell({ resized: true }));
document.addEventListener('click', event => {
  if (ctxMenu.style.display === 'block' && !ctxMenu.contains(event.target)) ctxMenu.style.display = 'none';
  const clickedChatControl = event.target.closest('#toggleDrawer');
  const clickedActivityControl = event.target.closest('#toggleThinkingDrawer')
    || event.target.closest('.thinking-wrap')
    || event.target.closest('.activity-launch');
  // Qwen Live is a persistent monitor: keep the last generation readable
  // while idle, including when clicking or editing the main conversation.
  // The explicit close button and the panel toggle still close it.
  if (thinkingDrawer.classList.contains('open')
      && rightPanelView !== 'qwen'
      && !thinkingDrawer.contains(event.target)
      && !clickedActivityControl
      && !clickedChatControl) {
    closeThinkingDrawer();
  }
});
ctxMenu.addEventListener('click', () => {
  const id = Number(ctxMenu.dataset.id || 0);
  if (id) deleteChat(id);
  ctxMenu.style.display = 'none';
});

async function runSettingsAction(label, action) {
  try {
    showError('');
    setStatus(label, 'busy');
    const result = await action();
    setStatus('Ready', 'idle');
    return result;
  } catch (error) {
    setStatus('Error', 'error');
    showError(String(error?.message || error));
    return null;
  }
}

$('probe')?.addEventListener('click', () => runSettingsAction('Refreshing', () => probeModels(false)));
$('remoteServing')?.addEventListener('change', event => runSettingsAction(
  event.target.checked ? 'Enabling remote access' : 'Disabling remote access',
  () => setRemoteServing(event.target.checked)
));
$('applyProviderToken')?.addEventListener('click', () => runSettingsAction('Updating provider', () => updateProviderToken($('providerToken')?.value || '')));
$('clearProviderToken')?.addEventListener('click', () => runSettingsAction('Clearing provider token', () => updateProviderToken('')));
$('resetModelRoles')?.addEventListener('click', resetAutomaticModelRoles);
$('refreshResidency')?.addEventListener('click', () => runSettingsAction('Checking GPU', () => refreshResidency(false)));
$('loadCoreModels')?.addEventListener('click', () => runSettingsAction('Loading core', async () => {
  const result = await postEngCoder('/api/engcoder/residency/activate', { model: 'engcoder://architect' });
  renderResidency(result);
  return result;
}));
$('releaseAddons')?.addEventListener('click', () => runSettingsAction('Releasing addons', async () => {
  const result = await postEngCoder('/api/engcoder/residency/release-addons');
  renderResidency(result);
  return result;
}));
$('refreshTraining')?.addEventListener('click', () => runSettingsAction('Refreshing training', refreshTrainingStatus));
$('refreshLearning')?.addEventListener('click', () => runSettingsAction('Refreshing memory', refreshLearningStatus));
$('initializeModel')?.addEventListener('click', () => runSettingsAction('Creating weights', async () => {
  const result = await postEngCoder('/api/engcoder/training/initialize', {
    profile: $('trainingProfile')?.value || 'desktop-16gb',
    force: false
  });
  await probeModels(false);
  await refreshTrainingStatus();
  return result;
}));
$('startTraining')?.addEventListener('click', () => runSettingsAction('Starting training', async () => {
  const result = await postEngCoder('/api/engcoder/training/start', {
    scope: $('trainingScope')?.value || 'keymaker-foundation',
    max_steps: Math.max(1, Number($('trainingSteps')?.value || 500)),
    checkpoint_steps: Math.max(1, Number($('checkpointSteps')?.value || 50)),
    require_cuda: true
  });
  renderTrainingStatus(result);
  return result;
}));
$('pauseTraining')?.addEventListener('click', () => runSettingsAction('Pausing training', async () => {
  const result = await postEngCoder('/api/engcoder/training/pause', { paused: true });
  renderTrainingStatus(result);
  return result;
}));
$('resumeTraining')?.addEventListener('click', () => runSettingsAction('Resuming training', async () => {
  const result = await postEngCoder('/api/engcoder/training/pause', { paused: false });
  renderTrainingStatus(result);
  return result;
}));
$('cancelTraining')?.addEventListener('click', () => runSettingsAction('Stopping training', async () => {
  const result = await postEngCoder('/api/engcoder/training/cancel');
  renderTrainingStatus(result);
  return result;
}));
$('applySettings')?.addEventListener('click', () => {
  saveSettings();
  if ($('model')) $('model').value = $('foundationModel')?.value || 'auto';
  if (settingsDialog.open) settingsDialog.close();
  else settingsDialog.classList.remove('open');
});
$('clear').addEventListener('click', () => {
  const chat = currentChat();
  if (!chat) return;
  if (controller && chat.runtimeSessionId === activeEngCoderSessionId) {
    showError('Stop the active run before clearing this chat.');
    return;
  }
  const runtimeSessionId = chat.runtimeSessionId || '';
  chat.messages = [];
  chat.runtimeSessionId = '';
  if (runtimeSessionId) void deleteRuntimeSession(runtimeSessionId);
  renderActiveChat();
  refreshLastUserFromChat();
  saveAll();
  setStatus('Cleared', 'idle');
});

$('copyAll').addEventListener('click', () => {
  const chat = currentChat();
  if (!chat) return;
  const all = chat.messages.map(msg => {
    if (Array.isArray(msg.attachments) && msg.attachments.length) return `${msg.role.toUpperCase()} [attachments]: ${msg.content || ''}`;
    return `${msg.role.toUpperCase()}: ${msg.content || ''}`;
  }).join('\n\n');
  navigator.clipboard.writeText(all);
});

sendBtn.addEventListener('click', () => {
  const action = sendBtn.dataset.action || 'send';
  if (action === 'stop') {
    if (controller) stopStreaming();
    else if (recognitionMode !== 'idle') stopSpeechInput();
    else toggleTalkMode(false);
    return;
  }
  if (action === 'talk') {
    toggleTalkMode(true);
    return;
  }
  sendMessage({ regenerate: false });
});
stopBtn.addEventListener('click', stopStreaming);
retryLastBtn?.addEventListener('click', () => {
  if (controller) return;
  refreshLastUserFromChat();
  if (!lastUser) return;
  showError('');
  sendMessage({ regenerate: true });
});
micBtn?.addEventListener('click', toggleDictation);

promptEl.addEventListener('input', () => {
  autoresize();
  updatePrimaryAction();
});
promptEl.addEventListener('keydown', event => {
  if (event.key === 'Enter' && !event.shiftKey) {
    event.preventDefault();
    sendMessage({ regenerate: false });
  } else if (event.key === 'ArrowUp' && !promptEl.value) {
    event.preventDefault();
    promptEl.value = lastUser || '';
    autoresize();
    updatePrimaryAction();
  }
});


const TEXT_ATTACHMENT_EXTENSIONS = new Set([
  'txt','md','markdown','json','jsonl','csv','log','xml','html','htm','js','mjs','cjs','ts','tsx','jsx','css',
  'cpp','cc','cxx','c','h','hh','hpp','hxx','ixx','cppm','py','ps1','bat','cmd','sh','cmake','toml','yaml','yml',
  'ini','cfg','conf','sql','rs','go','java','cs','glsl','hlsl','vert','frag','comp','wgsl'
]);
const MAX_ATTACHMENT_COUNT = 96;
const MAX_ATTACHMENT_FILE_BYTES = 512 * 1024;
const MAX_ATTACHMENT_TOTAL_BYTES = 1536 * 1024;

function attachmentDisplayName(file) {
  return String(file?.webkitRelativePath || file?.name || 'attachment').replaceAll('\\', '/');
}

function attachmentFileExtension(file) {
  const name = String(file?.name || '').toLowerCase();
  const dot = name.lastIndexOf('.');
  return dot >= 0 ? name.slice(dot + 1) : '';
}

function isTextAttachmentFile(file) {
  const type = String(file?.type || '').toLowerCase();
  if (type.startsWith('text/')) return true;
  if (['application/json', 'application/xml', 'application/javascript', 'application/x-javascript'].includes(type)) return true;
  return TEXT_ATTACHMENT_EXTENSIONS.has(attachmentFileExtension(file));
}

function pendingAttachmentBytes() {
  return pendingAttachments.reduce((sum, attachment) => sum + (Number(attachment?.bytes) || 0), 0);
}

function updateQuickAttachmentCount() {
  if (!quickAttachmentCount) return;
  const count = pendingAttachments.length;
  quickAttachmentCount.hidden = count === 0;
  quickAttachmentCount.textContent = count ? `${count} attachment${count === 1 ? '' : 's'}` : '';
}

function renderPendingAttachments() {
  updateQuickAttachmentCount();
  if (!pendingAttachments.length) {
    imgPreview.innerHTML = '';
    imgPreview.hidden = true;
    updatePrimaryAction();
    return;
  }
  imgPreview.hidden = false;
  imgPreview.innerHTML = '';
  pendingAttachments.forEach((att, index) => {
    const chip = document.createElement('div');
    chip.className = 'file-chip';
    const label = document.createElement('span');
    label.textContent = att.name || 'Text file';
    label.title = att.name || '';
    const remove = document.createElement('button');
    remove.type = 'button';
    remove.setAttribute('aria-label', `Remove ${att.name || 'attachment'}`);
    remove.textContent = '×';
    remove.addEventListener('click', () => {
      pendingAttachments.splice(index, 1);
      renderPendingAttachments();
    });
    chip.append(label, remove);
    imgPreview.appendChild(chip);
  });
  updatePrimaryAction();
}

async function fileToAttachment(file) {
  if (!isTextAttachmentFile(file)) {
    if (String(file?.type || '').startsWith('image/')) {
      throw new Error('Image/vision transport is not enabled in the full EngCoder agent route yet.');
    }
    throw new Error(`Unsupported attachment type: ${attachmentDisplayName(file)}`);
  }
  if (file.size > MAX_ATTACHMENT_FILE_BYTES) {
    throw new Error(`${attachmentDisplayName(file)} is larger than the 512 KiB per-file context limit.`);
  }
  const text = await file.text();
  return {
    kind: 'text',
    name: attachmentDisplayName(file),
    mime: file.type || 'text/plain',
    text,
    bytes: file.size
  };
}

async function addAttachmentFiles(fileList, { folder = false } = {}) {
  const files = Array.from(fileList || []);
  if (!files.length) return;

  let added = 0;
  let skipped = 0;
  const problems = [];
  let totalBytes = pendingAttachmentBytes();

  for (const file of files) {
    if (pendingAttachments.length >= MAX_ATTACHMENT_COUNT) {
      problems.push(`Attachment limit reached (${MAX_ATTACHMENT_COUNT}).`);
      break;
    }
    if (folder && !isTextAttachmentFile(file)) {
      skipped += 1;
      continue;
    }
    if (totalBytes + file.size > MAX_ATTACHMENT_TOTAL_BYTES) {
      problems.push('Attachment context limit reached (1.5 MiB total).');
      break;
    }
    try {
      const attachment = await fileToAttachment(file);
      const duplicate = pendingAttachments.some(existing =>
        existing.kind === attachment.kind && existing.name === attachment.name && Number(existing.bytes) === Number(attachment.bytes));
      if (duplicate) {
        skipped += 1;
        continue;
      }
      pendingAttachments.push(attachment);
      totalBytes += attachment.bytes || 0;
      added += 1;
    } catch (error) {
      problems.push(String(error?.message || error));
    }
  }

  renderPendingAttachments();
  if (added) {
    setStatus(`Added ${added} context file${added === 1 ? '' : 's'}`, 'idle');
    showError('');
  }
  if (skipped && folder && !problems.length) {
    setStatus(`Added ${added} source file${added === 1 ? '' : 's'} · skipped ${skipped} unsupported`, 'idle');
  }
  if (problems.length) showError(problems.slice(0, 3).join('\n'));
}

function closeAttachMenu() {
  if (!attachMenu) return;
  attachMenu.hidden = true;
  attachBtn?.setAttribute('aria-expanded', 'false');
}

function toggleAttachMenu(force) {
  if (!attachMenu || attachBtn?.disabled) return;
  const open = typeof force === 'boolean' ? force : attachMenu.hidden;
  attachMenu.hidden = !open;
  attachBtn?.setAttribute('aria-expanded', open ? 'true' : 'false');
  if (open) closeQuickModeMenu();
}

function closeQuickModeMenu() {
  if (!quickModeMenu) return;
  quickModeMenu.hidden = true;
  quickModeBtn?.setAttribute('aria-expanded', 'false');
}

function toggleQuickModeMenu(force) {
  if (!quickModeMenu) return;
  const open = typeof force === 'boolean' ? force : quickModeMenu.hidden;
  quickModeMenu.hidden = !open;
  quickModeBtn?.setAttribute('aria-expanded', open ? 'true' : 'false');
  if (open) closeAttachMenu();
}

function currentQuickMode() {
  if ($('enableAddons')?.checked === false) return 'foundation';
  const forced = String($('forcedAddon')?.value || '').trim();
  if (['cpp', 'planner', 'docs', 'tools'].includes(forced)) return forced;
  return 'auto';
}

function quickModeLabel(mode) {
  return ({ auto: 'Auto', foundation: 'Foundation', cpp: 'C++23', planner: 'Planning', docs: 'Docs', tools: 'Tools' })[mode] || 'Auto';
}

function syncComposerQuickBar() {
  const mode = currentQuickMode();
  if (quickModeValue) quickModeValue.textContent = quickModeLabel(mode);
  if (quickModeBtn) quickModeBtn.dataset.mode = mode;
  quickModeMenu?.querySelectorAll('[data-mode]').forEach(button => {
    const selected = button.dataset.mode === mode;
    button.classList.toggle('selected', selected);
    button.setAttribute('aria-checked', selected ? 'true' : 'false');
  });

  const foundation = $('foundationModel');
  if (quickModelValue) {
    const selectedOption = foundation?.selectedOptions?.[0];
    quickModelValue.textContent = String(selectedOption?.textContent || foundation?.value || 'Auto').trim() || 'Auto';
    quickModelValue.title = quickModelValue.textContent;
  }

  const historyOn = $('includeHistory')?.checked !== false;
  if (quickHistoryValue) quickHistoryValue.textContent = historyOn ? 'History on' : 'History off';
  if (quickHistoryBtn) {
    quickHistoryBtn.classList.toggle('active', historyOn);
    quickHistoryBtn.setAttribute('aria-pressed', historyOn ? 'true' : 'false');
  }

  if (quickControlsBtn) quickControlsBtn.classList.toggle('active', dialogVisible(settingsDialog));
  updateQuickAttachmentCount();
}

function applyQuickMode(mode) {
  const forced = $('forcedAddon');
  const autoRouting = $('autoRouting');
  const enableAddons = $('enableAddons');
  if (!forced || !autoRouting || !enableAddons) return;

  if (mode === 'foundation') {
    forced.value = '';
    enableAddons.checked = false;
    autoRouting.checked = false;
  } else if (mode === 'auto') {
    forced.value = '';
    enableAddons.checked = true;
    autoRouting.checked = true;
  } else {
    forced.value = mode;
    enableAddons.checked = true;
    autoRouting.checked = false;
  }

  for (const element of [forced, autoRouting, enableAddons]) {
    element.dispatchEvent(new Event('change', { bubbles: true }));
  }
  syncComposerQuickBar();
  setStatus(`Mode: ${quickModeLabel(mode)}`, 'idle');
}

async function addClipboardText() {
  closeAttachMenu();
  if (!navigator.clipboard?.readText) {
    showError('Clipboard access is unavailable here. Paste directly into the prompt with Ctrl+V.');
    promptEl.focus();
    return;
  }
  try {
    const text = await navigator.clipboard.readText();
    if (!text.trim()) {
      showError('Clipboard does not contain text.');
      return;
    }
    const bytes = new TextEncoder().encode(text).byteLength;
    if (bytes > MAX_ATTACHMENT_FILE_BYTES || pendingAttachmentBytes() + bytes > MAX_ATTACHMENT_TOTAL_BYTES) {
      showError('Clipboard text is too large for the attachment context limit. Paste a smaller selection.');
      return;
    }
    pendingAttachments.push({ kind: 'text', name: 'Clipboard text', mime: 'text/plain', text, bytes });
    renderPendingAttachments();
    showError('');
    setStatus('Clipboard text added', 'idle');
  } catch (error) {
    showError(`Clipboard access failed: ${String(error?.message || error)}`);
    promptEl.focus();
  }
}

if (folderInput) {
  folderInput.setAttribute('webkitdirectory', '');
  folderInput.setAttribute('directory', '');
}

attachBtn?.addEventListener('click', event => {
  event.stopPropagation();
  toggleAttachMenu();
});

attachMenu?.addEventListener('click', event => {
  const button = event.target.closest('[data-attach-action]');
  if (!button) return;
  const action = button.dataset.attachAction;
  closeAttachMenu();
  if (action === 'files') imgInput?.click();
  else if (action === 'folder') folderInput?.click();
  else if (action === 'clipboard') void addClipboardText();
  else if (action === 'vision') showError('Image/vision input is intentionally disabled until the full /api/tasks vision transport is wired. EngCoder will not pretend an image reached the model.');
  else if (action === 'controls') {
    openDialog(settingsDialog);
    void probeModels(false);
  } else if (action === 'runtime') {
    openLab();
    document.querySelector('.lab-tab[data-lab-tab="models"]')?.click();
  } else if (action === 'settings') {
    openDialog(settingsDialog);
    void probeModels(false);
  }
});

imgInput?.addEventListener('change', async () => {
  await addAttachmentFiles(imgInput.files);
  imgInput.value = '';
});

folderInput?.addEventListener('change', async () => {
  await addAttachmentFiles(folderInput.files, { folder: true });
  folderInput.value = '';
});

const composerRow = document.querySelector('.composer .row');
composerRow?.addEventListener('dragover', event => {
  if (!event.dataTransfer?.types?.includes('Files')) return;
  event.preventDefault();
  composerRow.classList.add('drag-over');
});
composerRow?.addEventListener('dragleave', event => {
  if (!composerRow.contains(event.relatedTarget)) composerRow.classList.remove('drag-over');
});
composerRow?.addEventListener('drop', event => {
  if (!event.dataTransfer?.files?.length) return;
  event.preventDefault();
  composerRow.classList.remove('drag-over');
  void addAttachmentFiles(event.dataTransfer.files);
});

promptEl.addEventListener('paste', event => {
  // Clipboard formats can contain both text and stale image/file payloads.
  // For a text composer, text always wins. Only consume file payloads when
  // there is no usable text at all.
  const text = String(event.clipboardData?.getData('text/plain') || '');
  if (text.length) return;

  const files = Array.from(event.clipboardData?.files || []);
  if (!files.length) return;
  event.preventDefault();
  void addAttachmentFiles(files);
});

quickModeBtn?.addEventListener('click', event => {
  event.stopPropagation();
  toggleQuickModeMenu();
});
quickModeMenu?.addEventListener('click', event => {
  const button = event.target.closest('[data-mode]');
  if (!button) return;
  applyQuickMode(button.dataset.mode || 'auto');
  closeQuickModeMenu();
});
quickModelBtn?.addEventListener('click', () => {
  openLab();
  document.querySelector('.lab-tab[data-lab-tab="models"]')?.click();
  queueMicrotask(() => $('foundationModel')?.focus());
});
quickHistoryBtn?.addEventListener('click', () => {
  const history = $('includeHistory');
  if (!history) return;
  history.checked = !history.checked;
  history.dispatchEvent(new Event('change', { bubbles: true }));
  syncComposerQuickBar();
});
quickRuntimeBtn?.addEventListener('click', () => {
  openLab();
  document.querySelector('.lab-tab[data-lab-tab="models"]')?.click();
});
quickControlsBtn?.addEventListener('click', () => {
  openDialog(settingsDialog);
  void probeModels(false);
});

document.addEventListener('click', event => {
  if (!attachMenu?.hidden && event.target !== attachBtn && !attachMenu.contains(event.target)) closeAttachMenu();
  if (!quickModeMenu?.hidden && event.target !== quickModeBtn && !quickModeMenu.contains(event.target)) closeQuickModeMenu();
  for (const menu of document.querySelectorAll('.model-action-menu[open]')) {
    if (!menu.contains(event.target)) menu.open = false;
  }
});
document.addEventListener('keydown', event => {
  if (event.key !== 'Escape') return;
  closeAttachMenu();
  closeQuickModeMenu();
  for (const menu of document.querySelectorAll('.model-action-menu[open]')) menu.open = false;
});

function prepareDialog(dialog) {
  if (dialog && typeof dialog.showModal !== 'function') dialog.classList.add('fallback');
}

function openDialog(dialog) {
  if (!dialog) return;
  if (typeof dialog.showModal === 'function') {
    if (!dialog.open) dialog.showModal();
  } else {
    dialog.classList.add('open');
  }
}

function closeDialog(dialog) {
  if (!dialog) return;
  if (dialog.open) dialog.close();
  else dialog.classList.remove('open');
}

function dialogVisible(dialog) {
  return !!dialog && (dialog.open || dialog.classList.contains('open'));
}

prepareDialog(settingsDialog);
prepareDialog(labDialog);

function syncComposerControls() {
  syncComposerQuickBar();
}

$('settingsBtn')?.addEventListener('click', () => {
  openDialog(settingsDialog);
  void probeModels(false);
});
$('closeSettings')?.addEventListener('click', () => closeDialog(settingsDialog));
settingsDialog?.addEventListener('click', event => {
  if (event.target === settingsDialog) closeDialog(settingsDialog);
});

function openLab() {
  // EngCoder Lab owns long-running model loads. A modal dialog makes the
  // Qwen Live/Run info toolbar behind it unclickable until loading ends.
  // Keep the same Lab window, but use non-modal <dialog>.show() so the rest
  // of the app remains interactive while its fetch is pending.
  if (labDialog && typeof labDialog.show === 'function') {
    if (!labDialog.open) labDialog.show();
  } else {
    openDialog(labDialog);
  }
  void probeModels(false);
  void refreshServingStatus().catch(() => {});
  void refreshProviderStatus().catch(() => {});
  void refreshNativeModelLibrary().catch(() => {});
  void refreshLearningStatus().catch(() => {});
}

$('labBtn')?.addEventListener('click', openLab);
$('openLabFromSettings')?.addEventListener('click', () => {
  closeDialog(settingsDialog);
  openLab();
});
$('closeLab')?.addEventListener('click', () => closeDialog(labDialog));
labDialog?.addEventListener('click', event => {
  if (event.target === labDialog) closeDialog(labDialog);
});
// Outside clicks still dismiss Lab, but are no longer swallowed by a modal
// backdrop. In particular, clicking the right-hand inspector opens it now.
document.addEventListener('click', event => {
  if (!dialogVisible(labDialog) || labDialog.contains(event.target)) return;
  if (event.target.closest('#labBtn, #openLabFromSettings')) return;
  closeDialog(labDialog);
});
$('labRefreshModels')?.addEventListener('click', () => runSettingsAction('Refreshing models', () => probeModels(false)));
$('refreshModelLibrary')?.addEventListener('click', () => runSettingsAction('Refreshing library', refreshNativeModelLibrary));
$('installNativeRuntime')?.addEventListener('click', () => runSettingsAction('Updating Qwen runtime backend', installNativeRuntime));
$('registerNativeModel')?.addEventListener('click', () => runSettingsAction('Adding model', registerNativeModel));
$('unloadNativeModel')?.addEventListener('click', () => runSettingsAction('Unloading native model', unloadNativeModel));
$('resetNativeProfileDefaults')?.addEventListener('click', resetNativeProfileDefaults);
$('nativeModelLibrary')?.addEventListener('click', event => {
  const button = event.target.closest('button[data-model-action]');
  if (!button) return;
  const action = button.dataset.modelAction || '';
  const uri = button.dataset.modelUri || '';
  if (action === 'configure') {
    const model = (nativeModelLibrary?.models || []).find(item => (item.uri || `qwenflash://${item.id}`) === uri);
    if (model) { populateNativeModelForm(model); $('registerNativeModel')?.scrollIntoView({ behavior: 'smooth', block: 'center' }); }
    return;
  }
  if (action === 'use') {
    void runSettingsAction('Selecting KeyMaker', async () => {
      await persistFoundationSelection(uri);
      await probeModels(false);
      setStatus('Ready', 'idle');
    });
    return;
  }
  const actions = {
    install: () => installNativeModel(uri),
    load: () => loadNativeModel(uri),
    unload: () => unloadNativeModel(),
    reload: () => reloadNativeModel(uri),
    'clear-cache': () => clearNativeModelCache(uri),
    'delete-weights': () => deleteNativeModelWeights(uri)
  };
  const labels = { install: 'Starting download', load: 'Loading model', unload: 'Unloading model', reload: 'Reloading model with saved settings', 'clear-cache': 'Clearing temporary cache', 'delete-weights': 'Deleting downloaded model weights' };
  if (actions[action]) void runSettingsAction(labels[action] || 'Updating model', actions[action]);
});

['nativeContextTokens', 'nativeParallelSlots'].forEach(id => {
  $(id)?.addEventListener('change', updateNativeEffectiveContext);
  $(id)?.addEventListener('input', updateNativeEffectiveContext);
});
$('nativeRuntimePolicy')?.addEventListener('change', () => {
  updateNativePolicyControls();
  saveSettings();
  scheduleNativeProfileAutosave();
});
for (const id of NATIVE_TUNING_CONTROL_IDS) {
  const element = $(id);
  if (!element) continue;
  const promote = () => {
    promoteNativeTuningToManual();
    // A literal exact-layer value overrides the percentage. Moving the
    // percentage means the percentage is now authoritative.
    if (id === 'nativeGpuOffloadPercent' && $('nativeGpuLayers')) {
      $('nativeGpuLayers').value = 'auto';
    }
  };
  element.addEventListener('input', promote);
  element.addEventListener('change', () => {
    promote();
    scheduleNativeProfileAutosave();
  });
}

for (const id of NATIVE_RUNTIME_AUTOSAVE_IDS) {
  if (id === 'nativeRuntimePolicy' || NATIVE_TUNING_CONTROL_IDS.includes(id)) continue;
  const element = $(id);
  if (!element) continue;
  element.addEventListener('change', () => scheduleNativeProfileAutosave());
}
const NATIVE_PROFILE_AUTOSAVE_IDS = [
  'nativeContextTokens', 'nativeParallelSlots', 'nativeContextReserve', 'nativeAutoCompact',
  'nativeCompactTrigger', 'nativeCompactTarget', 'nativeSpeculative', 'nativeSpecType',
  'nativeSpecRepo', 'nativeSpecModel', 'nativeSpecTokens'
];
for (const id of NATIVE_PROFILE_AUTOSAVE_IDS) {
  $(id)?.addEventListener('change', scheduleNativeProfileAutosave);
}
$('nativeSpecModel')?.addEventListener('input', updateMtpAvailability);
$('nativeRuntimeType')?.addEventListener('change', updateMtpAvailability);
$('autofill')?.addEventListener('click', () => runSettingsAction('Selecting model', async () => useFirstExternalModel()));
$('foundationModel')?.addEventListener('change', event => {
  const uri = String(event.target?.value || '').trim();
  if ($('simpleResponseModel')?.value === uri) {
    $('simpleResponseModel').value = '';
    localStorage.setItem(STORAGE_KEYS.simpleResponseModel, '');
    if ($('simpleRoutingStatus')) $('simpleRoutingStatus').textContent = 'Small responder cleared: KeyMaker cannot also be the small model.';
  }
  syncReviewModelChoices();
  renderTrainingRelationship();
  if (!uri) return;
  void runSettingsAction('Selecting KeyMaker', async () => {
    await persistFoundationSelection(uri);
    await probeModels(false);
    renderTrainingRelationship();
    setStatus('Ready', 'idle');
  });
});

document.querySelectorAll('.lab-tab').forEach(button => {
  button.addEventListener('click', () => {
    const tab = button.dataset.labTab;
    document.querySelectorAll('.lab-tab').forEach(item => item.classList.toggle('active', item === button));
    document.querySelectorAll('.lab-panel').forEach(panel => panel.classList.toggle('active', panel.dataset.labPanel === tab));
  });
});

['model', 'system', 'temperature', 'max_tokens', 'stream', 'voiceLang', 'speakReplies',
 'includeHistory', 'maxHistoryTurns', 'autoRelisten', 'silenceFinishMs', 'voiceRate', 'voicePitch',
 'thinkingExpandDefault', 'thinkingShowEmpty', 'theme', 'messageStyle', 'fontSize',
 'foundationModel', 'controllerModel', 'simpleResponseModel', 'reviewModel', 'reviewMode', 'thinkingMode', 'addonCppModel', 'addonDocsModel', 'addonPlannerModel',
 'addonToolsModel', 'addonRerankerModel', 'forcedAddon', 'autoRouting', 'enableAddons',
 'showActivityDetails', 'trainingProfile', 'trainingScope', 'trainingSteps', 'checkpointSteps'].forEach(id => {
  const el = $(id);
  if (!el) return;
  el.addEventListener('change', () => { saveSettings(); applyAppearance(); });
  el.addEventListener('input', () => { saveSettings(); applyAppearance(); });
});

$('theme')?.addEventListener('change', () => { void persistSharedTheme(); });
$('theme')?.addEventListener('input', () => { void persistSharedTheme(); });

// Keep the small-responder status accurate immediately after the selection changes.
$('simpleResponseModel')?.addEventListener('change', () => {
  syncReviewModelChoices();
  const select = $('simpleResponseModel');
  const status = $('simpleRoutingStatus');
  if (!status) return;
  status.textContent = select?.value
    ? `Simple requests can use ${select.selectedOptions[0]?.textContent || select.value} when automatic routing is enabled.`
    : 'No small response model selected. KeyMaker will answer even simple greetings.';
});
$('reviewModel')?.addEventListener('change', () => {
  const status = $('reviewRoutingStatus');
  if (status) status.textContent = $('reviewModel')?.value
    ? 'Review is advisory; KeyMaker remains the final authority.'
    : 'Middle review disabled; KeyMaker verifies directly.';
});

setInterval(() => {
  if (document.hidden || !dialogVisible(labDialog)) return;

  const activeTab = document.querySelector('.lab-tab.active')?.dataset.labTab;
  if (activeTab === 'training') {
    void refreshTrainingStatus().catch(() => {});
  } else if (activeTab === 'gpu') {
    void refreshResidency(false).catch(() => {});
  } else if (activeTab === 'learning') {
    void refreshLearningStatus().catch(() => {});
  }
}, 2500);

window.addEventListener('beforeunload', () => {
  try { stopSpeechInput(); } catch {}
});


installNativeDefaultButtons();
