// Keep the transcript pinned only while the user is at the bottom. Never
// animate each streamed update: CSS smooth-scrolling caused the scroll event
// to mistake a half-finished animation for an intentional user scroll.
let chatScrollFrame = 0;
let renderingChat = false;

function isNearBottom(el, threshold = 96) {
  if (!el) return true;
  return el.scrollHeight - el.scrollTop - el.clientHeight <= threshold;
}

function maybeScroll(_final = false, force = false) {
  if (!chatEl || renderingChat) return;
  if (force) shouldAutoScroll = true;
  if (!shouldAutoScroll) return;

  // Move synchronously before another scroll event can misclassify the view;
  // do one follow-up after layout (composer resizing, image load, etc.).
  chatEl.scrollTop = chatEl.scrollHeight;
  if (chatScrollFrame) return;
  chatScrollFrame = requestAnimationFrame(() => {
    chatScrollFrame = 0;
    if (shouldAutoScroll) chatEl.scrollTop = chatEl.scrollHeight;
  });
}


let previousShellWidth = window.innerWidth;

function syncShellLayout() {
  const chatsOpen = drawer.classList.contains('open');
  document.body.classList.toggle('chats-open', chatsOpen);
  document.body.classList.toggle('activity-open', thinkingDrawer.classList.contains('open'));
  toggleDrawerBtn.classList.toggle('active', chatsOpen);
  toggleDrawerBtn.setAttribute('aria-expanded', String(chatsOpen));
  toggleThinkingDrawerBtn.classList.toggle('active', thinkingDrawer.classList.contains('open'));
}

// Only an actual narrowing of the viewport may auto-close Chats. Calling this
// during a user click, transcript render, or another panel's toggle must never
// override the user's decision to keep Chats open.
function applyResponsiveShell({ resized = false } = {}) {
  const viewportWidth = window.innerWidth;
  if (resized && viewportWidth < previousShellWidth && viewportWidth <= AUTO_CLOSE_CHAT_WIDTH) {
    drawer.classList.remove('open');
  }
  previousShellWidth = viewportWidth;

  const edgeGap = 14;
  const shellPadding = 32;
  const rightOccupied = thinkingDrawer.classList.contains('open') ? thinkingDrawer.getBoundingClientRect().width + edgeGap : 0;
  const desiredLeftWidth = drawer.classList.contains('open') ? drawer.getBoundingClientRect().width + edgeGap : 0;
  // Compact viewports use the existing overlay drawer. Never crush the chat
  // below its minimum width merely to make room for the drawer beside it.
  const leftOccupied = viewportWidth - desiredLeftWidth - rightOccupied - shellPadding >= MIN_APP_WIDTH
    ? desiredLeftWidth : 0;
  const available = viewportWidth - leftOccupied - rightOccupied - shellPadding;
  const target = Math.min(1240, Math.max(Math.min(MIN_APP_WIDTH, viewportWidth - shellPadding), Math.floor(available)));
  document.documentElement.style.setProperty('--app-width', `${target}px`);

  syncShellLayout();
  // Shell width changed → re-evaluate composer wrap in the new geometry.
  autoresize();
}

chatEl.addEventListener('scroll', () => {
  if (!renderingChat) shouldAutoScroll = isNearBottom(chatEl);
}, { passive: true });


function setStatus(text, state = 'idle') {
  let label = text || '';
  if (state === 'idle') {
    if (!label || label === 'Idle' || label === 'Ready' || /^Done/i.test(label) || /^Ready\s*[\(·]/i.test(label)) label = 'Ready';
    else if (/^Model[:·]/i.test(label)) label = 'Model';
    else if (/^History cleared/i.test(label)) label = 'Cleared';
  }
  if (state === 'busy' && /^Connecting/i.test(label)) label = 'Connecting';
  if (state === 'busy' && /^Sending/i.test(label)) label = 'Sending';
  if (state === 'busy' && /Streaming|Live/i.test(label)) label = 'Streaming';
  if (state === 'error') label = 'Error';
  statusEl.textContent = label || 'Ready';
  statusDot.dataset.state = state;
}

function conciseError(message) {
  const full = String(message || '').replace(/\r\n?/g, '\n').trim();
  if (!full) return '';
  if (/\bHTTP 401\b|authentication/i.test(full)) {
    return 'LM Studio authentication is required. Open EngCoder Lab → Runtime and apply the model-server API token.';
  }
  const first = full.split('\n').map(line => line.trim()).find(Boolean) || full;
  return first.length > 360 ? `${first.slice(0, 357)}…` : first;
}

function showError(message, { retry = false } = {}) {
  const full = String(message || '').replace(/\r\n?/g, '\n').trim();
  const concise = conciseError(full);
  if (errorTextEl) errorTextEl.textContent = concise;
  else errEl.textContent = concise;
  errEl.title = full && full !== concise ? full : '';
  errEl.hidden = !full;
  if (retryLastBtn) retryLastBtn.hidden = !(full && retry);
}

function setBusy(isBusy) {
  attachBtn.disabled = isBusy;
  stopBtn.classList.add('hidden');
  updateVoiceButtons();
  updatePrimaryAction();
}

function esc(text) {
  return String(text ?? '').replace(/[&<>]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
}

function authHeaders() {
  const key = $('apiKey').value.trim();
  return key ? { Authorization: `Bearer ${key}` } : {};
}

function clamp(value, min, max) {
  const n = Number(value);
  if (!Number.isFinite(n)) return min;
  return Math.min(Math.max(n, min), max);
}


function normalizeBaseUrl(raw) {
  let value = (raw || '').trim();
  if (!value) value = window.location.origin;
  if (!/^https?:\/\//i.test(value)) value = `http://${value}`;
  value = value.replace(/\/+$/, '');
  value = value.replace(/\/(health|api\/model\/discovery|ui)$/i, '');
  return value;
}

function modelsUrl() {
  return `${normalizeBaseUrl($('endpoint').value)}/api/model/discovery`;
}

function chatUrl() {
  return `${normalizeBaseUrl($('endpoint').value)}/api/tasks`;
}


function currentChat() {
  return chats.find(c => c.id === currentChatId) || chats[0] || null;
}

function makeChatName(seed) {
  const words = String(seed || 'New chat').trim().split(/\s+/).filter(Boolean).slice(0, 7).join(' ');
  return words ? `Conversation: ${words}…` : 'Conversation';
}

function sanitizeMessage(msg) {
  if (!msg || typeof msg !== 'object') return null;

  let role = '';
  if (typeof msg.role === 'string') role = msg.role;
  else if (typeof msg.isUser === 'boolean') role = msg.isUser ? 'user' : 'assistant';
  else if (typeof msg.sender === 'string') role = msg.sender;

  if (!role) return null;
  if (role !== 'user' && role !== 'assistant' && role !== 'system') return null;

  if (Array.isArray(msg.attachments) && msg.attachments.length) {
    return {
      role: 'user',
      attachments: msg.attachments.map(att => ({
        kind: att && att.kind === 'image' ? 'image' : 'text',
        name: typeof att?.name === 'string' ? att.name : '',
        mime: typeof att?.mime === 'string' ? att.mime : '',
        data: typeof att?.data === 'string' ? att.data : '',
        text: typeof att?.text === 'string' ? att.text : '',
        preview: typeof att?.preview === 'string' ? att.preview : ''
      })),
      content: typeof msg.content === 'string' ? msg.content : (typeof msg.text === 'string' ? msg.text : '')
    };
  }

  if (msg.imageData) {
    return {
      role: 'user',
      attachments: [{
        kind: 'image',
        name: 'image',
        mime: 'image/*',
        data: typeof msg.imageData === 'string' ? msg.imageData : '',
        preview: typeof msg.imageData === 'string' ? msg.imageData : ''
      }],
      content: typeof msg.text === 'string' ? msg.text : (typeof msg.content === 'string' ? msg.content : "What's in this image?")
    };
  }

  let content = '';
  if (typeof msg.content === 'string') content = msg.content;
  else if (typeof msg.text === 'string') content = msg.text;
  else if (Array.isArray(msg.content)) {
    content = msg.content.map(part => {
      if (typeof part === 'string') return part;
      if (part && typeof part.text === 'string') return part.text;
      return '';
    }).filter(Boolean).join('\n');
  }

  const base = { role, content };
  if (role === 'assistant') {
    base.thinking = typeof msg.thinking === 'string' ? msg.thinking : '';
    base.thinkingOpen = msg.thinkingOpen === true;
    base.thinkingLabel = typeof msg.thinkingLabel === 'string' ? msg.thinkingLabel : '';
    base.hasThinking = msg.hasThinking === true || !!base.thinking;
    base.completionReason = typeof msg.completionReason === 'string' ? msg.completionReason : '';
    base.streamEndedBy = typeof msg.streamEndedBy === 'string' ? msg.streamEndedBy : '';
    base.finishReasonRaw = typeof msg.finishReasonRaw === 'string' ? msg.finishReasonRaw : '';
    base.completionAt = typeof msg.completionAt === 'string' ? msg.completionAt : '';
    base.completionTimestamp = Number(msg.completionTimestamp) || 0;
    base.completionModel = typeof msg.completionModel === 'string' ? msg.completionModel : '';
    base.respondingModel = typeof msg.respondingModel === 'string' ? msg.respondingModel : '';
    base.completionDurationMs = Number(msg.completionDurationMs) || 0;
    base.thinkDurationMs = Number(msg.thinkDurationMs) || 0;
    base.genDurationMs = Number(msg.genDurationMs) || 0;
    base.taskStartedAtMs = Number(msg.taskStartedAtMs) || 0;
    base.firstOutputAtMs = Number(msg.firstOutputAtMs) || 0;
    base.completionState = typeof msg.completionState === 'string' ? msg.completionState : 'Idle';
    base.timestampFull = typeof msg.timestampFull === 'string' ? msg.timestampFull : '';
    base.activityThinkingOpen = msg.activityThinkingOpen === true;
    // Legacy rawOutput included huge telemetry lines; never restore those to the output panel.
    base.rawOutputFormat = msg.rawOutputFormat === 'generated-v1' ? 'generated-v1' : '';
    base.rawOutput = base.rawOutputFormat && typeof msg.rawOutput === 'string'
      ? msg.rawOutput.slice(-96 * 1024) : '';
    base.reasoningOutput = typeof msg.reasoningOutput === 'string' ? msg.reasoningOutput.slice(-96 * 1024) : '';
    base.runtimeTaskId = typeof msg.runtimeTaskId === 'string' ? msg.runtimeTaskId : '';
    base.runtimeSessionId = typeof msg.runtimeSessionId === 'string' ? msg.runtimeSessionId : '';
    base.candidateIteration = msg.candidateIteration === true;
    base.sourcePrompt = typeof msg.sourcePrompt === 'string' ? msg.sourcePrompt : '';
    base.selectedAddon = typeof msg.selectedAddon === 'string' ? msg.selectedAddon : '';
    base.verifiedForLearning = msg.verifiedForLearning === true;
    base.approvalRequest = msg.approvalRequest && typeof msg.approvalRequest === 'object'
      ? { ...msg.approvalRequest, pending: false } : null;
    base.lastEventSequence = Number(msg.lastEventSequence) || 0;
    base.executionStats = msg.executionStats && typeof msg.executionStats === 'object' ? JSON.parse(JSON.stringify(msg.executionStats)) : null;
    base.activityEvents = Array.isArray(msg.activityEvents) ? msg.activityEvents.slice(-300).map(item => ({
      sequence: Number(item?.sequence) || 0,
      timestampMs: Number(item?.timestampMs) || 0,
      type: typeof item?.type === 'string' ? item.type : '',
      text: typeof item?.text === 'string' ? item.text : ''
    })).filter(item => item.text || item.type) : [];
  }
  return base;
}

function sanitizeChats(rawChats) {
  if (!Array.isArray(rawChats)) return [];
  const out = [];
  for (const chat of rawChats) {
    if (!chat || typeof chat !== 'object') continue;
    const id = Number(chat.id) || Date.now() + out.length;
    const messages = Array.isArray(chat.messages) ? chat.messages.map(sanitizeMessage).filter(Boolean) : [];
    const name = typeof chat.name === 'string' && chat.name.trim() ? chat.name : `Conversation ${out.length + 1}`;
    const runtimeSessionId = typeof chat.runtimeSessionId === 'string' ? chat.runtimeSessionId : '';
    out.push({ id, name, messages, runtimeSessionId });
  }
  return out;
}


function updateRuntimeEndpointFromMode() {
  const endpoint = $('endpoint');
  if (endpoint) endpoint.value = normalizeBaseUrl(window.location.origin);
}

function discoveredModelIds() {
  const values = new Set();
  for (const entry of modelDiscovery?.models || []) {
    const id = typeof entry === 'string' ? entry : entry?.id;
    if (id) values.add(String(id));
  }
  for (const component of modelRegistry?.components || []) {
    if (component?.provider_model && component?.active !== false && component?.enabled !== false) {
      values.add(String(component.provider_model));
    }
  }
  return [...values];
}

function managedModelId(value) {
  return /^(engcoder|threadspire|internal|qwenflash):\/\//.test(String(value || ''));
}

function roleModelCompatible(control, model) {
  if (control === 'foundationModel') return true;
  if (!managedModelId(model)) return true; // external provider models are eligible
  const mapping = {
    controllerModel: 'engcoder://architect',
    addonCppModel: 'engcoder://cpp', addonDocsModel: 'engcoder://docs',
    addonPlannerModel: 'engcoder://planning', addonToolsModel: 'engcoder://tools',
    addonRerankerModel: 'engcoder://reranker'
  };
  return mapping[control] === model;
}

function populateModelSelect(id, preferred = '', allowDisabled = false) {
  const select = $(id);
  if (!select) return;
  const prior = select.value;
  const options = discoveredModelIds().filter(value => roleModelCompatible(id, value));
  select.innerHTML = '';

  const automatic = document.createElement('option');
  automatic.value = '';
  automatic.textContent = allowDisabled ? 'Disabled / runtime default' : 'Runtime default';
  select.appendChild(automatic);

  for (const modelId of options) {
    const option = document.createElement('option');
    option.value = modelId;
    const discovered = (modelDiscovery?.models || []).find(item => (typeof item === 'string' ? item : item?.id) === modelId);
    const role = typeof discovered === 'object' && discovered ? discovered.role : '';
    option.textContent = `${modelId}${role ? ` · ${role}` : ''}`;
    select.appendChild(option);
  }

  const savedRaw = localStorage.getItem(STORAGE_KEYS[id]) || '';
  const saved = options.includes(savedRaw) ? savedRaw : '';
  const priorAvailable = options.includes(prior) ? prior : '';
  const preferredAvailable = options.includes(preferred) ? preferred : '';
  if (savedRaw && !saved) localStorage.removeItem(STORAGE_KEYS[id]);

  const desired = saved || priorAvailable || preferredAvailable || '';
  for (const candidate of [saved, preferredAvailable]) {
    if (!candidate || !options.includes(candidate) || [...select.options].some(option => option.value === candidate)) continue;
    const custom = document.createElement('option');
    custom.value = candidate;
    custom.textContent = `${candidate} · ${candidate === preferredAvailable ? 'configured' : 'saved'}`;
    select.appendChild(custom);
  }
  select.value = [...select.options].some(option => option.value === desired) ? desired : '';
}

function saveModelRoles() {
  // Model selection is session-local UI state; the native registry remains authoritative.
}

function loadModelRoles() {
  manualModelRoles = {};
}

function setRoleForModel() {}

async function resetAutomaticModelRoles() {
  await probeModels(true);
}

function applyAutomaticModelRoles() {}

function renderModelRoles() {
  const list = $('modelRoleList');
  const status = $('modelRoleStatus');
  if (!list || !status) return;
  list.innerHTML = '';

  const controller = runtimeArchitecture?.controller;
  const foundation = runtimeArchitecture?.foundation;
  const addons = runtimeArchitecture?.addons || [];
  const items = [
    controller ? { name: 'Architect', detail: controller.model || 'deterministic fallback', kind: 'controller', pinned: true } : null,
    foundation ? { name: 'KeyMaker Foundation', detail: foundation.model || 'unassigned', kind: 'foundation', pinned: true } : null,
    ...addons.map(addon => ({
      name: addon.display_name || addon.id,
      detail: addon.model || 'not assigned',
      kind: 'addon',
      pinned: false,
      enabled: addon.enabled !== false
    }))
  ].filter(Boolean);

  for (const item of items) {
    const row = document.createElement('div');
    row.className = 'model-role-row compact';
    const name = document.createElement('div');
    name.className = 'model-role-name';
    name.textContent = item.name;
    const meta = document.createElement('span');
    meta.className = 'model-role-meta';
    meta.textContent = `${item.kind}${item.pinned ? ' · pinned' : ' · on demand'} · ${item.detail}`;
    name.appendChild(meta);
    const badge = document.createElement('span');
    badge.className = 'model-role-select';
    badge.textContent = item.enabled === false ? 'Disabled' : (item.pinned ? 'Resident' : 'Addon');
    row.append(name, badge);
    list.appendChild(row);
  }

  const internalAvailable = runtimeArchitecture?.internal_host?.available === true;
  status.textContent = internalAvailable
    ? `Internal EngCoder generation available · ${addons.filter(a => a.enabled !== false).length} capability addons registered`
    : `No trained internal generation yet · external models remain available while you bootstrap EngCoder`;
}

function syncReviewModelChoices() {
  const reviewer = $('reviewModel');
  if (!reviewer) return;
  const small = $('simpleResponseModel')?.value || '';
  const foundation = $('foundationModel')?.value || '';
  const previous = reviewer.value || localStorage.getItem(STORAGE_KEYS.reviewModel) || '';
  const available = (modelDiscovery?.models || []).filter(item => item && typeof item === 'object' &&
    !item.internal && !item.native && typeof item.id === 'string' && item.id.trim() &&
    !managedModelId(item.id) && item.id !== small && item.id !== foundation);
  reviewer.replaceChildren(new Option('Disabled · direct to KeyMaker', ''));
  for (const entry of available) reviewer.add(new Option(entry.display_name || entry.id, entry.id));
  reviewer.value = available.some(entry => entry.id === previous) ? previous : '';
  const invalidSameModel = Boolean(previous && (previous === small || previous === foundation));
  if (invalidSameModel) localStorage.setItem(STORAGE_KEYS.reviewModel, '');
  const status = $('reviewRoutingStatus');
  if (status) status.textContent = invalidSameModel
    ? 'Middle reviewer cleared: it must differ from the small model and KeyMaker.'
    : reviewer.value ? 'Review is advisory; KeyMaker remains the final authority.'
    : 'Assign a different external model for middle-tier verification, or leave it off.';
}

function populateArchitectureSelectors() {
  const addonMap = Object.fromEntries((runtimeArchitecture?.addons || []).map(addon => [addon.id, addon.model || '']));
  populateModelSelect('foundationModel', runtimeArchitecture?.foundation?.model || '');
  populateModelSelect('controllerModel', runtimeArchitecture?.controller?.model || '', true);
  // Never offer an internal model or draft/model-management URI as a simple responder.
  const small = $('simpleResponseModel');
  if (small) {
    const prior = localStorage.getItem(STORAGE_KEYS.simpleResponseModel) || '';
    small.replaceChildren(new Option('Disabled · always use KeyMaker', ''));
    const external = (modelDiscovery?.models || []).filter(item =>
      item && typeof item === 'object' && !item.internal && !item.native &&
      typeof item.id === 'string' && item.id.trim() && !managedModelId(item.id) &&
      item.id !== ($('foundationModel')?.value || ''));
    for (const entry of external) small.add(new Option(entry.display_name || entry.id, entry.id));
    const assigned = external.find(entry => entry.id === prior);
    small.value = assigned ? prior : '';
    const status = $('simpleRoutingStatus');
    if (status) {
      status.textContent = assigned
        ? `Simple requests can use ${assigned.display_name || assigned.id} when automatic routing is enabled.`
        : prior
          ? 'Previously selected small model is unavailable; KeyMaker will respond until you select an available model.'
          : external.length
            ? 'Select an LM Studio model for greetings and simple responses. Without one, KeyMaker handles everything.'
            : 'No compatible external response models discovered. Connect LM Studio and refresh models.';
    }
  }
  // Reviewer must be external: native Qwen stays the final authority.
  syncReviewModelChoices();
  populateModelSelect('addonCppModel', addonMap.cpp || '', true);
  populateModelSelect('addonDocsModel', addonMap.docs || '', true);
  populateModelSelect('addonPlannerModel', addonMap.planner || '', true);
  populateModelSelect('addonToolsModel', addonMap.tools || '', true);
  populateModelSelect('addonRerankerModel', addonMap.reranker || '', true);
  if ($('model')) $('model').value = $('foundationModel')?.value || runtimeArchitecture?.foundation?.model || 'auto';
}

function formatBytes(value) {
  const bytes = Number(value) || 0;
  if (bytes <= 0) return '0 B';
  const units = ['B', 'KiB', 'MiB', 'GiB'];
  let n = bytes;
  let index = 0;
  while (n >= 1024 && index < units.length - 1) { n /= 1024; index += 1; }
  return `${n.toFixed(index >= 2 ? 2 : 0)} ${units[index]}`;
}

function nativeModelSourceLabel(model) {
  if (model?.source === 'local-gguf') return model?.local_path || 'Local GGUF';
  const repo = model?.repo || 'Hugging Face';
  return model?.quant ? `${repo}:${model.quant}` : repo;
}

function setNativeModelAsFoundation(uri) {
  if (!uri) return;
  const select = $('foundationModel');
  if (select && ![...select.options].some(option => option.value === uri)) {
    select.add(new Option(uri, uri));
  }
  if (select) select.value = uri;
  if ($('model')) $('model').value = uri;
  localStorage.setItem(STORAGE_KEYS.foundationModel, uri);
  localStorage.setItem(STORAGE_KEYS.model, uri);
  saveSettings();
}

async function persistFoundationSelection(uri) {
  if (!uri) return;
  const response = await fetch(`${engcoderBase()}/api/engcoder/foundation`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ model: uri })
  });
  const payload = await response.json().catch(() => null);
  if (!response.ok) throw new Error(payload?.error || `Foundation selection HTTP ${response.status}`);
  setNativeModelAsFoundation(payload?.foundation_model || uri);
  renderTrainingRelationship();
  return payload;
}

function renderNativeModelLibrary(payload) {
  nativeModelLibrary = payload || null;
  const host = $('nativeModelLibrary');
  const facts = $('nativeRuntimeFacts');
  const status = $('nativeModelInstallStatus');
  const progressHost = $('nativeInstallProgress');
  const progressPhase = $('nativeInstallProgressPhase');
  const progressPercent = $('nativeInstallProgressPercent');
  const progressBar = $('nativeInstallProgressBar');
  const progressDetail = $('nativeInstallProgressDetail');
  if (!host || !facts || !status) return;

  const renderInstallProgress = (visible, percent = 0, phase = '', detail = '', indeterminate = false) => {
    if (!progressHost || !progressPhase || !progressPercent || !progressBar || !progressDetail) return;
    progressHost.hidden = !visible;
    if (!visible) return;
    const bounded = Math.max(0, Math.min(100, Number(percent) || 0));
    progressPhase.textContent = phase || 'Working';
    progressPercent.textContent = indeterminate ? 'Working' : `${bounded.toFixed(bounded < 10 ? 1 : 0)}%`;
    progressBar.style.width = indeterminate ? '35%' : `${bounded}%`;
    progressBar.classList.toggle('indeterminate', indeterminate);
    progressDetail.textContent = detail || '';
  };

  host.innerHTML = '';
  facts.innerHTML = '';

  const runtime = payload?.qwen_flash || {};
  updateMtpAvailability();
  if ($('registerNativeModel') && !$('registerNativeModel').dataset.defaultsApplied) {
    const initialModel = (payload?.models || []).find(model => model.active === true) || (payload?.models || [])[0] || null;
    populateNativeModelForm(initialModel);
    $('registerNativeModel').dataset.defaultsApplied = '1';
  }
  const runtimeFact = document.createElement('div');
  const runtimeLabel = document.createElement('span');
  const runtimeValue = document.createElement('strong');
  runtimeLabel.textContent = 'Qwen Flash runtime';
  runtimeValue.textContent = runtime.startup_in_progress
    ? `Loading · ${runtime.startup_model || runtime.active_model || 'model'}`
    : runtime.running
      ? `Running · ${runtime.active_model || 'model loaded'}`
      : (runtime.server ? (runtime.last_error ? 'Available · last startup failed' : 'Available · stopped') : 'Backend not found');
  const installRuntimeButton = $('installNativeRuntime');
  const runtimeInstall = payload?.runtime_install || { state: 'idle' };
  if (installRuntimeButton) {
    const patchedRuntimeReady = runtime.runtime_verified === true;
    installRuntimeButton.disabled = patchedRuntimeReady || runtimeInstall.state === 'installing';
    installRuntimeButton.textContent = runtimeInstall.state === 'installing'
      ? 'Updating Qwen Runtime Backend…'
      : (patchedRuntimeReady ? 'Qwen Runtime Backend Current' : (runtime.server ? 'Update Qwen Runtime Backend' : 'Install Qwen Runtime Backend'));
  }
  runtimeFact.append(runtimeLabel, runtimeValue);

  const cacheFact = document.createElement('div');
  const cacheLabel = document.createElement('span');
  const cacheValue = document.createElement('strong');
  cacheLabel.textContent = 'Model cache';
  const cacheFree = payload?.cache?.available_bytes;
  cacheValue.textContent = `${runtime.cache_root || payload?.cache?.root || 'Not configured'}${cacheFree != null ? ` · ${formatBytes(cacheFree)} free` : ''}`;
  cacheFact.append(cacheLabel, cacheValue);
  facts.append(runtimeFact, cacheFact);

  if (runtime.server) {
    const qualificationFact = document.createElement('div');
    const label = document.createElement('span');
    const value = document.createElement('strong');
    label.textContent = 'Qwen acceleration';
    if (runtime.expert_cache_qualified === true) {
      value.textContent = 'MoE cache qualified for the active model/runtime pair';
    } else if (runtime.expert_cache_candidate === true) {
      value.textContent = 'Patched runtime · cache qualification pending; Auto will probe once';
    } else if (runtime.runtime_verified === true) {
      value.textContent = 'Current runtime · stable baseline; expert cache unavailable';
    } else {
      value.textContent = 'Compatibility baseline · expert cache disabled automatically · runtime backend update available';
    }
    qualificationFact.append(label, value);
    facts.appendChild(qualificationFact);
  }

  if (runtime.hardware && Object.keys(runtime.hardware).length) {
    const hardwareFact = document.createElement('div');
    const label = document.createElement('span');
    const value = document.createElement('strong');
    label.textContent = 'Auto-tuning hardware';
    const ram = Number(runtime.hardware.ram_gib || 0);
    const vram = Number(runtime.hardware.vram_mib || 0);
    value.textContent = `${runtime.hardware.gpu_name || 'GPU'}${vram ? ` · ${(vram / 1024).toFixed(1)} GiB VRAM` : ''}${ram ? ` · ${ram.toFixed(1)} GiB RAM` : ''} · ${Number(runtime.hardware.logical_threads || 0)} threads`;
    hardwareFact.append(label, value);
    facts.appendChild(hardwareFact);
  }
  if (runtime.startup_note) {
    const policyFact = document.createElement('div');
    const label = document.createElement('span');
    const value = document.createElement('strong');
    label.textContent = 'Runtime policy';
    value.textContent = runtime.startup_note;
    policyFact.append(label, value);
    facts.appendChild(policyFact);
  }
  if (runtime.effective_profile && Object.keys(runtime.effective_profile).length) {
    const profile = runtime.effective_profile;
    const effectiveFact = document.createElement('div');
    const label = document.createElement('span');
    const value = document.createElement('strong');
    label.textContent = 'Effective launch profile';
    const policy = String(profile.runtime_policy || 'recommended');
    const ngl = String(profile.gpu_layers ?? 'auto');
    const offloadPercent = Number(profile.gpu_offload_percent ?? 50);
    const expert = Number(profile.expert_cache_mib || 0);
    const pinned = Number(profile.pinned_host_mib || 0);
    const batch = Number(profile.batch_size || 0);
    const ubatch = Number(profile.ubatch_size || 0);
    const kv = policy === 'recommended'
      ? 'KV backend default + pinned host support'
      : `${profile.kv_offload ? 'KV GPU offload' : 'KV host'}${profile.kv_cpu_pinned ? ' pinned' : ''}`;
    const launchProfile = String(profile.launch_profile || 'full');
    value.textContent = `${policy} · ${launchProfile} · GPU layers ${offloadPercent}% / -ngl ${ngl} · experts ${expert.toLocaleString()} MiB · pinned ${pinned.toLocaleString()} MiB · ${kv} · batch ${batch}/${ubatch}`;
    effectiveFact.append(label, value);
    facts.appendChild(effectiveFact);
  }

  const install = payload?.install || { state: 'idle' };
  const installState = String(install.state || 'idle');
  const runtimeInstallState = String(runtimeInstall.state || 'idle');
  if (runtimeInstallState === 'installing') {
    const percent = Number(runtimeInstall.percent || 0);
    const phase = String(runtimeInstall.phase || 'runtime backend').replaceAll('-', ' ');
    status.textContent = runtimeInstall.message || 'Updating Qwen Runtime Backend…';
    status.dataset.state = 'busy';
    renderInstallProgress(true, percent, `Runtime backend · ${phase}`, runtimeInstall.detail || 'Model weights are not being changed.', false);
  } else if (runtimeInstallState === 'failed') {
    status.textContent = `Runtime backend update failed · ${runtimeInstall.error || 'Unknown error'}`;
    status.dataset.state = 'error';
    renderInstallProgress(false);
  } else if (runtimeInstallState === 'installed' && !runtime.server) {
    status.textContent = 'Runtime backend update completed; refresh to verify it.';
    status.dataset.state = 'ready';
    renderInstallProgress(true, 100, 'Runtime backend · complete', 'Backend files were staged successfully.', false);
  } else if (installState === 'installing') {
    const target = (payload?.models || []).find(model => model.uri === install.model);
    const cached = Number(target?.cache?.cached_bytes || 0);
    const expected = Number(target?.estimated_bytes || 0);
    const percent = expected > 0 ? Math.min(100, cached / expected * 100) : 0;
    const progress = expected > 0 ? ` · ${percent.toFixed(1)}%` : '';
    status.textContent = `Downloading model weights · ${install.display_name || install.model || 'native model'} · ${formatBytes(cached)} cached${progress}`;
    status.dataset.state = 'busy';
    renderInstallProgress(true, percent, 'Model weights · download', expected > 0 ? `${formatBytes(cached)} of approximately ${formatBytes(expected)}` : `${formatBytes(cached)} cached; total size is not known yet`, expected <= 0);
  } else if (installState === 'failed') {
    status.textContent = `Model download failed · ${install.error || 'Unknown error'}`;
    status.dataset.state = 'error';
    renderInstallProgress(false);
  } else if (installState === 'installed') {
    status.textContent = `Model weights ready · ${install.model || 'native model'}`;
    status.dataset.state = 'ready';
    renderInstallProgress(true, 100, 'Model weights · complete', 'The model can now be loaded into the Qwen Runtime Backend.', false);
  } else if (runtime.last_error) {
    status.textContent = 'Last Qwen runtime load failed · details are shown in the error banner; the next load will retain the exact command and exit tail.';
    status.dataset.state = 'error';
    renderInstallProgress(false);
  } else {
    status.textContent = 'No model download or runtime-backend update is active.';
    status.dataset.state = 'idle';
    renderInstallProgress(false);
  }

  for (const model of payload?.models || []) {
    const card = document.createElement('article');
    card.className = `model-library-card${model.active ? ' active' : ''}`;

    const head = document.createElement('div');
    head.className = 'model-library-card-head';
    const title = document.createElement('strong');
    title.textContent = model.display_name || model.id || 'Native model';
    const badge = document.createElement('span');
    badge.className = 'model-library-badge';
    const modelLoading = runtime.startup_in_progress === true && runtime.startup_model === model.id;
    badge.textContent = modelLoading ? 'Loading' : (model.active ? 'Loaded' : (model.cache?.installed ? 'Installed' : 'Available'));
    head.append(title, badge);

    const meta = document.createElement('div');
    meta.className = 'model-library-meta';
    const policy = model.runtime_policy || 'recommended';
    const gpuPercent = Number(model.gpu_offload_percent ?? 50);
    const gpuTarget = nativeGpuLayerTarget(model);
    const gpuPolicy = `${gpuPercent}% of safe Qwen layer budget · -ngl ${gpuTarget}${String(model.gpu_layers || 'auto') !== 'auto' ? ' capped override' : ''}`;
    const effective = model.active && runtime.effective_profile && runtime.active_model === model.id ? runtime.effective_profile : null;
    const effectiveGpu = effective ? `${Number(effective.gpu_offload_percent ?? gpuPercent)}% configured · -ngl ${String(effective.gpu_layers ?? gpuTarget)}` : null;
    const rows = [
      ['Runtime', model.runtime || 'qwen-flash'],
      ['Tuning', policy.charAt(0).toUpperCase() + policy.slice(1)],
      ['GPU offload', effectiveGpu ? `${gpuPolicy} · ${effectiveGpu}` : gpuPolicy],
      ['Source', nativeModelSourceLabel(model)],
      ['Cached', model.cache?.expected_bytes
        ? `${formatBytes(model.cache?.cached_bytes || 0)} / ~${formatBytes(model.cache.expected_bytes)}`
        : formatBytes(model.cache?.cached_bytes || 0)],
      ['Cache integrity', model.cache?.installed
        ? 'Complete'
        : (Number(model.cache?.matching_gguf_files || 0) > 0
          ? (model.cache?.split_complete && model.cache?.size_plausible === false ? 'Incomplete / truncated' : 'Incomplete')
          : 'Not downloaded')],
      ['Context', `${Number(model.context_tokens || 0).toLocaleString()} total`],
      ['Effective / slot', `${Number(model.effective_context_tokens || model.context_tokens || 0).toLocaleString()} tokens · ${Number(model.parallel_slots || 1)} slot${Number(model.parallel_slots || 1) === 1 ? '' : 's'}`],
      ['Compaction', model.auto_compact ? `${Math.round(Number(model.compact_trigger_ratio || .90) * 100)}% → ${Math.round(Number(model.compact_target_ratio || .70) * 100)}%` : 'Off'],
      ['Expert cache', `${model.expert_cache_mode || 'auto'} · ${Number(model.expert_cache_mib || 0).toLocaleString()} MiB VRAM`],
      ['Pinned host', `${Number(model.pinned_host_mib || 0).toLocaleString()} MiB`],
      ['KV cache', policy === 'recommended'
        ? `${model.kv_type_k || 'q4_0'}/${model.kv_type_v || 'q4_0'} · QFR runtime default · pinned-host capable`
        : (policy === 'automatic'
          ? `${model.kv_type_k || 'q4_0'}/${model.kv_type_v || 'q4_0'} · hardware-tuned at load`
          : `${model.kv_type_k || 'q4_0'}/${model.kv_type_v || 'q4_0'} · ${model.kv_offload ? 'GPU offload' : 'Pinned CPU'} · GPU layers ${Number(model.kv_gpu_layers || 0)}`)]
    ];
    for (const [key, value] of rows) {
      const row = document.createElement('div');
      const k = document.createElement('span');
      const v = document.createElement('span');
      k.textContent = key;
      v.textContent = String(value || '—');
      row.append(k, v);
      meta.appendChild(row);
    }

    const actions = document.createElement('div');
    actions.className = 'model-library-actions';
    const addAction = (label, action, disabled = false) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = label;
      button.dataset.modelAction = action;
      button.dataset.modelUri = model.uri || `qwenflash://${model.id}`;
      button.disabled = disabled;
      actions.appendChild(button);
    };
    const installLabel = model.source === 'local-gguf' ? (model.cache?.installed ? 'Local weights ready' : 'Verify local weights') : (model.cache?.installed ? 'Weights downloaded' : 'Download model weights');
    if (model.cache?.installed !== true) addAction(installLabel, 'install', installState === 'installing');
    // One primary lifecycle action, with destructive/secondary commands under
    // a compact submenu so the model card does not grow a wall of buttons.
    if (model.active) addAction('Reload / Apply settings', 'reload', modelLoading || installState === 'installing');
    else addAction(modelLoading ? 'Loading…' : 'Load', 'load', modelLoading || installState === 'installing');

    const more = document.createElement('details');
    more.className = 'model-action-menu';
    const summary = document.createElement('summary');
    summary.textContent = 'More ▾';
    more.appendChild(summary);
    const menu = document.createElement('div');
    menu.className = 'model-action-menu-items';
    const addMenuAction = (label, action, disabled = false) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = label;
      button.dataset.modelAction = action;
      button.dataset.modelUri = model.uri || `qwenflash://${model.id}`;
      button.disabled = disabled;
      menu.appendChild(button);
    };
    addMenuAction('Configure', 'configure');
    addMenuAction('Use as KeyMaker', 'use', model.cache?.installed !== true && model.active !== true);
    if (model.active) addMenuAction('Unload', 'unload');
    addMenuAction('Clear temp cache', 'clear-cache', installState === 'installing');
    if (model.source !== 'local-gguf' && model.cache?.installed === true) addMenuAction('Delete downloaded model…', 'delete-weights', model.active === true || installState === 'installing');
    more.appendChild(menu);
    actions.appendChild(more);

    card.append(head, meta, actions);
    host.appendChild(card);
  }

  if (!(payload?.models || []).length) {
    const empty = document.createElement('div');
    empty.className = 'model-library-install-status';
    empty.textContent = 'No native model profiles are registered.';
    host.appendChild(empty);
  }

  if (nativeModelLibraryPollTimer) {
    clearTimeout(nativeModelLibraryPollTimer);
    nativeModelLibraryPollTimer = null;
  }
  if (installState === 'installing' || runtimeInstallState === 'installing' || runtime.startup_in_progress === true) {
    nativeModelLibraryPollTimer = setTimeout(() => void refreshNativeModelLibrary().catch(() => {}), 2000);
  }
}

async function refreshNativeModelLibrary() {
  const response = await fetch(`${engcoderBase()}/api/engcoder/model-library`, { cache: 'no-store' });
  const payload = await response.json().catch(() => null);
  if (!response.ok) throw new Error(payload?.error || `Model library HTTP ${response.status}`);
  renderNativeModelLibrary(payload);
  return payload;
}

function nativeNumber(id, fallback) {
  const value = Number($(id)?.value);
  return Number.isFinite(value) ? value : fallback;
}

function updateNativeEffectiveContext() {
  const total = Math.max(4096, Math.trunc(nativeNumber('nativeContextTokens', 81920)));
  const slots = Math.max(1, Math.trunc(nativeNumber('nativeParallelSlots', 1)));
  const effective = Math.floor(total / slots);
  if ($('nativeEffectiveContext')) $('nativeEffectiveContext').value = `${effective.toLocaleString()}${effective < 75000 ? ' · below 75K target' : ' · recommended range'}`;
}

let nativeProfileAutosaveTimer = null;
let nativeProfileAutosaveBusy = false;

const NATIVE_TUNING_CONTROL_IDS = [
  'nativeThreads', 'nativeBatchThreads', 'nativeBatchSize', 'nativeUbatchSize',
  'nativeGpuOffloadPercent', 'nativeGpuLayerCountHint', 'nativeGpuLayers',
  'nativeExpertCacheMode', 'nativeExpertCache', 'nativePinnedHost', 'nativePromptCache', 'nativeKvK', 'nativeKvV',
  'nativeKvGpuLayers', 'nativeLoadMode', 'nativeLazyMode', 'nativeTensorOverride',
  'nativeFlashAttention', 'nativeKvOffload', 'nativeKvCpuPinned', 'nativePhaseWorkspace',
  'nativeEarlyRouter', 'nativeReasoningPreserve', 'nativeContextShift'
];

const NATIVE_RUNTIME_AUTOSAVE_IDS = [
  'nativeRuntimePolicy', 'nativeContextTokens', 'nativeParallelSlots', 'nativeContextReserve',
  'nativeAutoCompact', 'nativeCompactTrigger', 'nativeCompactTarget',
  ...NATIVE_TUNING_CONTROL_IDS,
  'nativeSpeculative', 'nativeSpecType', 'nativeSpecRepo', 'nativeSpecModel', 'nativeSpecTokens'
];

function updateNativePolicyControls() {
  const policy = $('nativeRuntimePolicy')?.value || 'recommended';
  const hint = $('nativeRuntimePolicyHint');
  if (hint) {
    hint.textContent = policy === 'manual'
      ? 'Manual tuning is active. These values will be saved with the model profile.'
      : policy === 'automatic'
        ? 'Automatic tuning is active. Editing any tuning field switches this profile to Manual.'
        : 'Recommended tuning is active. Editing any tuning field switches this profile to Manual.';
  }
  for (const id of NATIVE_TUNING_CONTROL_IDS) {
    const element = $(id);
    if (!element) continue;
    element.disabled = false;
    element.dataset.runtimePolicy = policy;
    element.title = policy === 'manual' ? '' : 'Editing this value switches Runtime tuning to Manual.';
  }
}

function promoteNativeTuningToManual() {
  const policy = $('nativeRuntimePolicy');
  if (!policy || policy.value === 'manual') return;
  policy.value = 'manual';
  updateNativePolicyControls();
  saveSettings();
  setStatus('Runtime tuning: Manual', 'idle');
}

function nativeGpuLayerTarget(model) {
  const rawHint = Number(model?.gpu_layer_count_hint ?? 48);
  const hint = Number.isFinite(rawHint) ? Math.max(1, Math.trunc(rawHint)) : 48;
  const ceiling = Math.round(hint * .6);
  const exact = String(model?.gpu_layers ?? 'auto').trim();
  if (exact && exact !== 'auto') {
    if (exact === 'all') return String(ceiling);
    if (/^\d+$/.test(exact)) return String(Math.min(Number(exact), ceiling));
    return exact;
  }
  const rawPercent = Number(model?.gpu_offload_percent ?? 50);
  const percent = Number.isFinite(rawPercent) ? Math.max(0, Math.min(100, Math.trunc(rawPercent))) : 50;
  if (percent <= 0) return '0';
  return String(Math.min(ceiling, Math.max(1, Math.round(hint * .6 * percent / 100))));
}

function updateMtpAvailability() {
  const enabled = $('nativeSpeculative');
  const draft = String($('nativeSpecModel')?.value || '').trim();
  const compatibleNative = ($('nativeRuntimeType')?.value || 'qwen-flash') === 'qwen-flash';
  const available = compatibleNative && nativeModelLibrary?.qwen_flash?.runtime_verified === true &&
    /\.gguf$/i.test(draft);
  if (enabled) {
    enabled.disabled = !available;
    if (!available) enabled.checked = false;
    enabled.title = available ? 'Only select a matching Qwen draft-MTP GGUF; incompatible metadata is rejected by the runtime.'
      : 'MTP requires a verified native backend and a dedicated local .gguf draft path.';
  }
  if ($('nativeSpecRepo')) $('nativeSpecRepo').value = '';
  if ($('nativeSpecType')) $('nativeSpecType').value = 'draft-mtp';
}

function populateNativeModelForm(model = null) {
  const defaults = nativeModelLibrary?.runtime_types?.find(type => type.id === 'qwen-flash')?.recommended || {};
  const source = model || defaults;
  const setValue = (id, key, fallback) => { if ($(id)) $(id).value = source[key] ?? fallback; };
  const setChecked = (id, key, fallback) => { if ($(id)) $(id).checked = source[key] ?? fallback; };
  setValue('nativeModelRepo', 'repo', 'unsloth/Qwen3.8-Flash-Next-GGUF');
  setValue('nativeModelQuant', 'quant', 'UD-Q2_K_XL');
  setValue('nativeModelDisplay', 'display_name', 'Qwen3.8 Flash Next · UD-Q2_K_XL');
  setValue('nativeModelLocalPath', 'local_path', '');
  setValue('nativeRuntimePolicy', 'runtime_policy', 'recommended');
  setValue('nativeGpuOffloadPercent', 'gpu_offload_percent', 50);
  setValue('nativeGpuLayerCountHint', 'gpu_layer_count_hint', 48);
  setValue('nativeContextTokens', 'context_tokens', 81920);
  setValue('nativeParallelSlots', 'parallel_slots', 1);
  setValue('nativeContextReserve', 'context_reserve_tokens', 4096);
  setChecked('nativeAutoCompact', 'auto_compact', true);
  if ($('nativeCompactTrigger')) $('nativeCompactTrigger').value = Math.round(Number(source.compact_trigger_ratio ?? .97) * 100);
  if ($('nativeCompactTarget')) $('nativeCompactTarget').value = Math.round(Number(source.compact_target_ratio ?? .72) * 100);
  setValue('nativeThreads', 'threads', 12);
  setValue('nativeBatchThreads', 'batch_threads', 12);
  setValue('nativeBatchSize', 'batch_size', 1024);
  setValue('nativeUbatchSize', 'ubatch_size', 256);
  setValue('nativeGpuLayers', 'gpu_layers', 'auto');
  setValue('nativeExpertCacheMode', 'expert_cache_mode', 'auto');
  setValue('nativeExpertCache', 'expert_cache_mib', 6144);
  setValue('nativePinnedHost', 'pinned_host_mib', 12288);
  setValue('nativePromptCache', 'prompt_cache_mib', 2048);
  setValue('nativeKvK', 'kv_type_k', 'q4_0');
  setValue('nativeKvV', 'kv_type_v', 'q4_0');
  setValue('nativeKvGpuLayers', 'kv_gpu_layers', 0);
  setValue('nativeLoadMode', 'load_mode', 'mmap');
  setValue('nativeLazyMode', 'lazy_mode', 'on');
  setValue('nativeTensorOverride', 'tensor_override', 'per_layer_token_embd.weight=CPU');
  setChecked('nativeFlashAttention', 'flash_attention', true);
  setChecked('nativeKvOffload', 'kv_offload', false);
  setChecked('nativeKvCpuPinned', 'kv_cpu_pinned', true);
  setChecked('nativePhaseWorkspace', 'phase_aware_workspace', true);
  setChecked('nativeEarlyRouter', 'early_router', false);
  setChecked('nativeReasoningPreserve', 'reasoning_preserve', false);
  setChecked('nativeContextShift', 'context_shift', false);
  setChecked('nativeSpeculative', 'speculative_enabled', false);
  setValue('nativeSpecType', 'speculative_type', 'draft-mtp');
  setValue('nativeSpecRepo', 'speculative_repo', '');
  setValue('nativeSpecModel', 'speculative_model', '');
  updateMtpAvailability();
  setValue('nativeSpecTokens', 'speculative_tokens', 2);
  if ($('registerNativeModel')) $('registerNativeModel').dataset.editingId = model?.id || '';
  updateNativeEffectiveContext();
  updateNativePolicyControls();
}


const NATIVE_PROFILE_DEFAULTS = {
  nativeModelRepo: 'unsloth/Qwen3.8-Flash-Next-GGUF', nativeModelQuant: 'UD-Q2_K_XL',
  nativeModelDisplay: 'Qwen3.8 Flash Next · UD-Q2_K_XL', nativeModelLocalPath: '',
  nativeRuntimePolicy: 'recommended', nativeGpuOffloadPercent: 95, nativeGpuLayerCountHint: 48,
  nativeContextTokens: 81920, nativeParallelSlots: 1, nativeContextReserve: 4096,
  nativeAutoCompact: true, nativeCompactTrigger: 97, nativeCompactTarget: 72,
  nativeThreads: 12, nativeBatchThreads: 12, nativeBatchSize: 1024, nativeUbatchSize: 256,
  nativeGpuLayers: 'auto', nativeExpertCacheMode: 'auto', nativeExpertCache: 6144,
  nativePinnedHost: 12288, nativePromptCache: 2048, nativeKvK: 'q4_0', nativeKvV: 'q4_0',
  nativeKvGpuLayers: 0, nativeLoadMode: 'mmap', nativeLazyMode: 'on',
  nativeTensorOverride: 'per_layer_token_embd.weight=CPU', nativeFlashAttention: true,
  nativeKvOffload: false, nativeKvCpuPinned: true, nativePhaseWorkspace: true,
  nativeEarlyRouter: false, nativeReasoningPreserve: false, nativeContextShift: false,
  nativeSpeculative: false, nativeSpecType: 'draft-mtp', nativeSpecRepo: '',
  nativeSpecModel: '', nativeSpecTokens: 2
};

function applyNativeFieldDefault(id, { autosave = true } = {}) {
  const el = $(id);
  if (!el || !(id in NATIVE_PROFILE_DEFAULTS)) return;
  const value = NATIVE_PROFILE_DEFAULTS[id];
  if (el.type === 'checkbox') el.checked = !!value;
  else el.value = String(value);
  if (id === 'nativeGpuOffloadPercent' && $('nativeGpuLayers')) $('nativeGpuLayers').value = 'auto';
  updateNativeEffectiveContext();
  updateNativePolicyControls();
  if (autosave) scheduleNativeProfileAutosave();
}

function resetNativeProfileDefaults() {
  if (!window.confirm('Reset every visible native-model setting to EngCoder defaults?\n\nThis changes the profile settings only. It does not delete model weights or caches.')) return false;
  for (const id of Object.keys(NATIVE_PROFILE_DEFAULTS)) applyNativeFieldDefault(id, { autosave: false });
  scheduleNativeProfileAutosave();
  const hint = $('nativeGpuApplyHint');
  if (hint) hint.textContent = 'Defaults restored. If this model is loaded, use Reload / Apply settings to activate restart-required values.';
  return true;
}

function installNativeDefaultButtons() {
  for (const id of Object.keys(NATIVE_PROFILE_DEFAULTS)) {
    const el = $(id);
    if (!el || el.dataset.defaultButtonInstalled) continue;
    el.dataset.defaultButtonInstalled = '1';
    const label = el.closest('label');
    if (!label) continue;
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'field-default-btn';
    btn.textContent = 'Default';
    btn.title = 'Reset only this setting to its default';
    btn.addEventListener('click', event => { event.preventDefault(); applyNativeFieldDefault(id); });
    label.appendChild(btn);
  }
}

async function registerNativeModel({ refresh = true, runtimeOnly = false } = {}) {
  const editingId = $('registerNativeModel')?.dataset.editingId || '';
  const existingModel = runtimeOnly
    ? (nativeModelLibrary?.models || []).find(model => model.id === editingId)
    : null;
  // Runtime autosave must not accidentally persist a one-shot Local GGUF
  // load override. Source identity changes only when the user explicitly
  // presses Add / save model profile.
  const repo = existingModel ? String(existingModel.repo || '') : ($('nativeModelRepo')?.value.trim() || '');
  const quant = existingModel ? String(existingModel.quant || '') : ($('nativeModelQuant')?.value.trim() || '');
  const localPath = existingModel
    ? (existingModel.source === 'local-gguf' ? String(existingModel.local_path || '') : '')
    : ($('nativeModelLocalPath')?.value.trim() || '');
  const displayName = existingModel
    ? String(existingModel.display_name || 'Qwen3.8 Flash Next · UD-Q2_K_XL')
    : ($('nativeModelDisplay')?.value.trim() || 'Qwen3.8 Flash Next · UD-Q2_K_XL');
  if (!repo && !localPath) throw new Error('Enter a Hugging Face repository or a local GGUF path.');
  const payload = await postEngCoder('/api/engcoder/model-library/register', {
    id: editingId || undefined,
    runtime: $('nativeRuntimeType')?.value || 'qwen-flash',
    source: localPath ? 'local-gguf' : 'hugging-face',
    repo,
    quant,
    local_path: localPath,
    display_name: displayName,
    runtime_policy: $('nativeRuntimePolicy')?.value || 'recommended',
    gpu_offload_percent: Math.max(0, Math.min(100, Math.trunc(nativeNumber('nativeGpuOffloadPercent', 50)))),
    gpu_layer_count_hint: Math.max(1, Math.trunc(nativeNumber('nativeGpuLayerCountHint', 48))),
    context_tokens: Math.max(4096, Math.trunc(nativeNumber('nativeContextTokens', 81920))),
    parallel_slots: Math.max(1, Math.trunc(nativeNumber('nativeParallelSlots', 1))),
    context_reserve_tokens: Math.max(2048, Math.trunc(nativeNumber('nativeContextReserve', 4096))),
    auto_compact: $('nativeAutoCompact')?.checked !== false,
    compact_trigger_ratio: Math.max(.50, Math.min(.98, nativeNumber('nativeCompactTrigger', 97) / 100)),
    compact_target_ratio: Math.max(.25, Math.min(.90, nativeNumber('nativeCompactTarget', 72) / 100)),
    threads: Math.max(1, Math.trunc(nativeNumber('nativeThreads', 12))),
    batch_threads: Math.max(1, Math.trunc(nativeNumber('nativeBatchThreads', 12))),
    batch_size: Math.max(64, Math.trunc(nativeNumber('nativeBatchSize', 1024))),
    ubatch_size: Math.max(32, Math.trunc(nativeNumber('nativeUbatchSize', 256))),
    gpu_layers: $('nativeGpuLayers')?.value.trim() || 'auto',
    flash_attention: $('nativeFlashAttention')?.checked !== false,
    kv_type_k: $('nativeKvK')?.value || 'q4_0',
    kv_type_v: $('nativeKvV')?.value || 'q4_0',
    kv_gpu_layers: Math.max(0, Math.trunc(nativeNumber('nativeKvGpuLayers', 0))),
    kv_offload: $('nativeKvOffload')?.checked === true,
    kv_cpu_pinned: $('nativeKvCpuPinned')?.checked !== false,
    phase_aware_workspace: $('nativePhaseWorkspace')?.checked !== false,
    load_mode: $('nativeLoadMode')?.value || 'mmap',
    lazy_mode: $('nativeLazyMode')?.value || 'on',
    tensor_override: $('nativeTensorOverride')?.value.trim() || 'per_layer_token_embd.weight=CPU',
    prompt_cache_mib: Math.max(0, Math.trunc(nativeNumber('nativePromptCache', 2048))),
    context_shift: $('nativeContextShift')?.checked === true,
    reasoning_preserve: $('nativeReasoningPreserve')?.checked === true,
    expert_cache_mode: $('nativeExpertCacheMode')?.value || 'auto',
    expert_cache_mib: Math.max(0, Math.trunc(nativeNumber('nativeExpertCache', 6144))),
    pinned_host_mib: Math.max(0, Math.trunc(nativeNumber('nativePinnedHost', 12288))),
    early_router: $('nativeEarlyRouter')?.checked === true,
    speculative_enabled: $('nativeSpeculative')?.checked === true,
    speculative_type: 'draft-mtp',
    speculative_repo: '',
    speculative_model: $('nativeSpecModel')?.value.trim() || '',
    speculative_tokens: Math.max(1, Math.trunc(nativeNumber('nativeSpecTokens', 2)))
  });
  if ($('registerNativeModel')) {
    $('registerNativeModel').dataset.editingId = String(payload?.id || editingId || '');
  }
  const applyHint = $('nativeGpuApplyHint');
  if (applyHint) {
    applyHint.textContent = payload?.runtime_reload_required
      ? 'Saved. The loaded model keeps its current GPU layout; this change applies on the next unload/load or restart.'
      : 'Saved. GPU/runtime layout changes apply when this model is next loaded.';
  }
  if (refresh) await Promise.all([refreshNativeModelLibrary(), probeModels(false)]);
  return payload;
}

function scheduleNativeProfileAutosave() {
  const button = $('registerNativeModel');
  const editingId = String(button?.dataset.editingId || '').trim();
  if (!editingId) return;

  if (nativeProfileAutosaveTimer) clearTimeout(nativeProfileAutosaveTimer);
  const hint = $('nativeGpuApplyHint');
  const active = (nativeModelLibrary?.models || []).some(model => model.id === editingId && model.active === true);
  if (hint) {
    hint.textContent = active
      ? 'Saving… the loaded model keeps its current GPU/runtime layout; changes apply on next load.'
      : 'Saving runtime profile… changes apply on the next model load.';
  }

  nativeProfileAutosaveTimer = setTimeout(async () => {
    if (nativeProfileAutosaveBusy) {
      scheduleNativeProfileAutosave();
      return;
    }
    nativeProfileAutosaveTimer = null;
    nativeProfileAutosaveBusy = true;
    try {
      const payload = await registerNativeModel({ refresh: false, runtimeOnly: true });
      if (hint) {
        hint.textContent = payload?.runtime_reload_required
          ? 'Saved. Current loaded model is unchanged; GPU/runtime changes apply on the next unload/load or restart.'
          : 'Saved. GPU/runtime changes apply when this model is next loaded.';
      }
      await refreshNativeModelLibrary();
    } catch (error) {
      if (hint) hint.textContent = `Save failed: ${String(error?.message || error)}`;
      showError(String(error?.message || error));
    } finally {
      nativeProfileAutosaveBusy = false;
    }
  }, 450);
}

async function installNativeRuntime() {
  const payload = await postEngCoder('/api/engcoder/model-library/runtime/install', {});
  await refreshNativeModelLibrary();
  return payload;
}

async function installNativeModel(uri) {
  const payload = await postEngCoder('/api/engcoder/model-library/install', { model: uri });
  await refreshNativeModelLibrary();
  return payload;
}

async function loadNativeModel(uri) {
  // "Local GGUF path" is a true load-time override, matching the original
  // QwenFlashRuntime --model behavior. Saving the profile is optional; a
  // non-empty field applies to the next Load immediately.
  const localPathOverride = $('nativeModelLocalPath')?.value.trim() || '';
  const model = (nativeModelLibrary?.models || []).find(item => (item.uri || `qwenflash://${item.id}`) === uri);
  if (!localPathOverride && model?.cache?.installed !== true) {
    throw new Error('Model weights are not complete. Finish the download/copy, or enter a Local GGUF path override and press Load.');
  }
  const payload = await postEngCoder('/api/engcoder/model-library/load', {
    model: uri,
    local_path_override: localPathOverride || undefined
  });
  renderNativeModelLibrary(payload?.library || await refreshNativeModelLibrary());
  // Loading a native model makes it the preferred candidate for Runtime Default
  // without pinning it as the persisted KeyMaker. Refresh both selectors and
  // architecture so the UI reflects the model EngCoder will actually use.
  await probeModels(false);
  return payload;
}

async function unloadNativeModel() {
  const payload = await postEngCoder('/api/engcoder/model-library/unload');
  renderNativeModelLibrary(payload?.library || await refreshNativeModelLibrary());
  return payload;
}

async function clearNativeModelCache(uri) {
  const payload = await postEngCoder('/api/engcoder/model-library/clear-cache', { model: uri });
  await refreshNativeModelLibrary();
  return payload;
}

async function deleteNativeModelWeights(uri) {
  const model = (nativeModelLibrary?.models || []).find(item => (item.uri || `qwenflash://${item.id}`) === uri);
  const name = model?.display_name || model?.id || 'this model';
  const bytes = Number(model?.cache?.cached_bytes || model?.estimated_bytes || 0);
  const size = bytes > 0 ? ` (${formatBytes(bytes)})` : '';
  if (!window.confirm(`Delete downloaded model weights for ${name}${size}?\n\nThis cannot be undone. The model profile/settings will be kept.`)) return { cancelled: true };
  const payload = await postEngCoder('/api/engcoder/model-library/delete-weights', { model: uri });
  await Promise.all([refreshNativeModelLibrary(), probeModels(false)]);
  return payload;
}

async function reloadNativeModel(uri) {
  const model = (nativeModelLibrary?.models || []).find(item => (item.uri || `qwenflash://${item.id}`) === uri);
  if (!model) throw new Error('Model is no longer in the library. Refresh and try again.');
  // Commit the visible runtime controls before restarting so Apply settings is
  // deterministic and the loaded stats match what the user just selected.
  if (($('registerNativeModel')?.dataset.editingId || '') === model.id) {
    await registerNativeModel({ refresh: false, runtimeOnly: true });
  }
  await unloadNativeModel();
  return loadNativeModel(uri);
}

function renderResidency(payload) {
  residencyState = payload || null;
  const summary = $('gpuSummary');
  const host = $('residencyComponents');
  if (!summary || !host) return;
  host.innerHTML = '';
  if (!payload) {
    summary.textContent = 'Residency information unavailable.';
    return;
  }
  const cuda = payload.cuda || {};
  if (cuda.available) {
    const used = Number(cuda.allocated_bytes || 0);
    const total = Number(cuda.total_bytes || 0);
    const budget = Number(payload.vram_budget_bytes || 0);
    summary.textContent = `${cuda.device || 'CUDA GPU'} · ${formatBytes(used)} allocated / ${formatBytes(total)} total${budget ? ` · ${formatBytes(budget)} EngCoder budget` : ''}`;
  } else {
    summary.textContent = payload.loaded ? `Model loaded on ${payload.device || 'CPU'}` : 'CUDA worker is not loaded yet.';
  }

  for (const component of payload.components || []) {
    const card = document.createElement('div');
    card.className = `residency-card state-${component.state || 'disk'}`;
    const title = document.createElement('strong');
    title.textContent = component.id === 'keymaker' ? 'KeyMaker Foundation' : component.id === 'architect' ? 'Architect' : component.id;
    const state = document.createElement('span');
    state.className = 'residency-state';
    state.textContent = `${component.state || 'disk'}${component.pinned ? ' · pinned' : ''}`;
    const meta = document.createElement('small');
    meta.textContent = `${Number(component.parameter_count || 0).toLocaleString()} params · ${formatBytes(component.resident_bytes || 0)}`;
    card.append(title, state, meta);
    host.appendChild(card);
  }
}

async function refreshResidency(load = false) {
  const response = await fetch(`${engcoderBase()}/api/engcoder/residency${load ? '?load=1' : ''}`, { cache: 'no-store' });
  const payload = await response.json().catch(() => null);
  if (!response.ok) throw new Error(payload?.error || `Residency HTTP ${response.status}`);
  renderResidency(payload);
  return payload;
}

function renderTrainingRelationship() {
  const host = $('trainingRelationship');
  if (!host) return;

  const selected = String(
    $('foundationModel')?.value || runtimeArchitecture?.foundation?.model || 'unassigned'
  ).trim() || 'unassigned';
  const internal = selected.startsWith('engcoder://');
  const qwenFlash = selected.startsWith('qwenflash://');

  host.innerHTML = '';

  const runtimeRow = document.createElement('div');
  runtimeRow.className = 'training-relationship-row';
  const runtimeLabel = document.createElement('strong');
  runtimeLabel.textContent = 'Runtime KeyMaker';
  const runtimeValue = document.createElement('span');
  runtimeValue.textContent = selected;
  runtimeRow.append(runtimeLabel, runtimeValue);

  const destinationRow = document.createElement('div');
  destinationRow.className = 'training-relationship-row';
  const destinationLabel = document.createElement('strong');
  destinationLabel.textContent = 'Trainer writes to';
  const destinationValue = document.createElement('span');
  destinationValue.textContent = internal
    ? 'the internal EngCoder generation currently assigned as KeyMaker'
    : 'EngCoder internal KeyMaker / Architect / addon weights';
  destinationRow.append(destinationLabel, destinationValue);

  const note = document.createElement('p');
  note.className = 'training-relationship-note';
  if (qwenFlash) {
    note.textContent = 'Qwen Flash is serving live inference. Its GGUF weights are not modified. Thumbs-up stores verified examples in EngCoder memory; eligible examples are consumed when you train the internal EngCoder model.';
  } else if (internal) {
    note.textContent = 'The live KeyMaker and the trainable internal generation are the same model family. Thumbs-up stores verified examples first; running training applies eligible examples to the selected internal component.';
  } else {
    note.textContent = 'The selected provider model serves live inference but is not rewritten by EngCoder. Thumbs-up stores verified examples; the trainer uses eligible examples to improve EngCoder\'s internal model generation.';
  }

  host.append(runtimeRow, destinationRow, note);
}

function renderTrainingStatus(payload) {
  trainingState = payload || null;
  renderTrainingRelationship();
  const host = $('trainingStatus');
  if (!host) return;
  if (!payload) { host.textContent = 'Training status unavailable.'; return; }
  const progress = payload.progress || {};
  const state = progress.phase || payload.state || (payload.running ? 'running' : 'idle');
  const step = Number(progress.step || payload.steps || 0);
  const max = Number(progress.maximum_steps || payload.max_steps || 0);
  const scope = progress.training_data_scope || progress.scope || payload.scope || '';
  const percent = Number(progress.percent);
  host.innerHTML = '';
  const line = document.createElement('div');
  line.className = 'training-status-line';
  line.textContent = `${state}${scope ? ` · ${scope}` : ''}${max ? ` · step ${step.toLocaleString()} / ${max.toLocaleString()}` : ''}`;
  host.appendChild(line);
  if (Number.isFinite(percent) && percent >= 0) {
    const track = document.createElement('div');
    track.className = 'training-progress-track';
    const bar = document.createElement('div');
    bar.className = 'training-progress-bar';
    bar.style.width = `${Math.max(0, Math.min(100, percent))}%`;
    track.appendChild(bar);
    host.appendChild(track);
  }
  if (progress.message) {
    const message = document.createElement('small');
    message.textContent = progress.message;
    host.appendChild(message);
  }
}

async function refreshTrainingStatus() {
  const response = await fetch(`${engcoderBase()}/api/engcoder/training/status`, { cache: 'no-store' });
  const payload = await response.json().catch(() => null);
  if (!response.ok) throw new Error(payload?.error || `Training HTTP ${response.status}`);
  renderTrainingStatus(payload);
  return payload;
}

async function removeLearningEpisode(episode) {
  const hash = String(episode?.content_hash || '');
  if (!hash) throw new Error('This learning item has no content hash. Refresh the learning list and try again.');
  if (!window.confirm('Remove this item from EngCoder learning memory?')) return { cancelled: true };
  const result = await postEngCoder('/api/engcoder/learning/remove', { content_hash: hash });
  await refreshLearningStatus();
  return result;
}

async function banLearningEpisode(episode) {
  const prompt = String(episode?.prompt || '');
  const response = String(episode?.response || '');
  if (!prompt && !response) throw new Error('This learning item has no content to ban.');
  if (!window.confirm('Remove and BAN this content from learning?\n\nEngCoder will refuse this prompt or response content from every future learning-ingestion path until you explicitly unban it.')) return { cancelled: true };
  const result = await postEngCoder('/api/engcoder/learning/ban', { prompt, response, reason: 'user-banned' });
  await refreshLearningStatus();
  return result;
}

async function unbanLearningItem(item) {
  const id = String(item?.id || '');
  if (!id) throw new Error('Ban id is missing.');
  if (!window.confirm('Unban this content and allow it to be learned again in the future?')) return { cancelled: true };
  const result = await postEngCoder('/api/engcoder/learning/unban', { ban_id: id });
  await refreshLearningStatus();
  return result;
}

function renderLearningStatus(status, recent, bans) {
  const summary = $('learningSummary');
  const host = $('recentMemory');
  const banHost = $('learningBans');
  if (summary) {
    const verified = Number(status?.verified_records || 0);
    const eligible = Number(status?.training_eligible_records || 0);
    const banned = Number(status?.banned_items || 0);
    summary.textContent = `${verified.toLocaleString()} verified memories · ${eligible.toLocaleString()} eligible for internal training · ${banned.toLocaleString()} banned from future learning`;
  }
  if (host) {
    host.innerHTML = '';
    for (const episode of recent?.items || []) {
      const row = document.createElement('div');
      row.className = 'memory-row';
      const title = document.createElement('strong');
      title.textContent = episode.source === 'compiler-and-tests:candidate-boundary'
        ? 'C++ boundary repair · compiler and tests · memory only'
        : `${episode.skill || 'keymaker-foundation'} · ${episode.source || 'verified'}`;
      const prompt = document.createElement('small');
      prompt.textContent = String(episode.prompt || '');
      const response = document.createElement('small');
      response.textContent = String(episode.response || '');
      const actions = document.createElement('div');
      actions.className = 'memory-row-actions';
      const remove = document.createElement('button');
      remove.type = 'button'; remove.textContent = 'Remove from learning';
      remove.addEventListener('click', () => void runSettingsAction('Removing learning item', () => removeLearningEpisode(episode)));
      const ban = document.createElement('button');
      ban.type = 'button'; ban.className = 'danger'; ban.textContent = 'Remove + Ban…';
      ban.addEventListener('click', () => void runSettingsAction('Banning learning content', () => banLearningEpisode(episode)));
      actions.append(remove, ban);
      row.append(title, prompt, response, actions);
      host.appendChild(row);
    }
    if (!(recent?.items || []).length) {
      const empty = document.createElement('small');
      empty.textContent = 'No verified memory has been recorded yet.';
      host.appendChild(empty);
    }
  }
  if (banHost) {
    banHost.innerHTML = '';
    for (const item of bans?.items || []) {
      const row = document.createElement('div');
      row.className = 'memory-row';
      const title = document.createElement('strong');
      title.textContent = 'Banned from all learning ingestion';
      const prompt = document.createElement('small');
      prompt.textContent = item.prompt_preview ? `Prompt: ${item.prompt_preview}` : 'Prompt fingerprint stored';
      const response = document.createElement('small');
      response.textContent = item.response_preview ? `Response: ${item.response_preview}` : 'Response fingerprint stored';
      const actions = document.createElement('div');
      actions.className = 'memory-row-actions';
      const unban = document.createElement('button');
      unban.type = 'button'; unban.textContent = 'Unban…';
      unban.addEventListener('click', () => void runSettingsAction('Unbanning learning content', () => unbanLearningItem(item)));
      actions.appendChild(unban);
      row.append(title, prompt, response, actions);
      banHost.appendChild(row);
    }
    if (!(bans?.items || []).length) {
      const empty = document.createElement('small');
      empty.textContent = 'Nothing is banned from learning.';
      banHost.appendChild(empty);
    }
  }
}

async function refreshLearningStatus() {
  const base = engcoderBase();
  const [statusResponse, recentResponse, bansResponse] = await Promise.all([
    fetch(`${base}/api/engcoder/learning`, { cache: 'no-store' }),
    fetch(`${base}/api/engcoder/memory/recent?limit=20`, { cache: 'no-store' }),
    fetch(`${base}/api/engcoder/learning/bans`, { cache: 'no-store' })
  ]);
  const status = await statusResponse.json().catch(() => null);
  const recent = await recentResponse.json().catch(() => null);
  const bans = await bansResponse.json().catch(() => null);
  if (!statusResponse.ok) throw new Error(status?.error || `Learning HTTP ${statusResponse.status}`);
  if (!recentResponse.ok) throw new Error(recent?.error || `Memory HTTP ${recentResponse.status}`);
  if (!bansResponse.ok) throw new Error(bans?.error || `Learning bans HTTP ${bansResponse.status}`);
  renderLearningStatus(status, recent, bans);
  return status;
}

async function postEngCoder(path, body = {}) {
  const response = await fetch(`${engcoderBase()}${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  const payload = await response.json().catch(() => null);
  if (!response.ok) throw new Error(payload?.error || `EngCoder HTTP ${response.status}`);
  return payload;
}

async function refreshServingStatus() {
  const response = await fetch(`${engcoderBase()}/api/engcoder/serving`, { cache: 'no-store' });
  const payload = await response.json().catch(() => null);
  if (!response.ok) throw new Error(payload?.error || `Serving HTTP ${response.status}`);
  if ($('remoteServing')) $('remoteServing').checked = payload?.remote_enabled === true;
  if ($('localUiUrl')) $('localUiUrl').textContent = payload?.local_url || `${engcoderBase()}/ui/`;
  if ($('remoteUiUrl')) $('remoteUiUrl').textContent = payload?.remote_enabled ? (payload?.remote_url || 'Enabled on LAN') : 'Disabled';
  if ($('openAiBaseUrl')) $('openAiBaseUrl').textContent = payload?.local_openai_base || `${engcoderBase()}/v1`;
  if ($('chatCompletionsUrl')) $('chatCompletionsUrl').textContent = payload?.local_chat_completions || `${engcoderBase()}/v1/chat/completions`;
  if ($('responsesUrl')) $('responsesUrl').textContent = payload?.local_responses || `${engcoderBase()}/v1/responses`;
  if ($('agentTasksUrl')) $('agentTasksUrl').textContent = payload?.local_agent_tasks || `${engcoderBase()}/api/tasks`;
  if ($('agentEventsUrl')) $('agentEventsUrl').textContent = payload?.local_agent_events || `${engcoderBase()}/api/sessions/{session_id}/events?after=0`;
  if ($('mcpEndpointUrl')) $('mcpEndpointUrl').textContent = payload?.mcp_endpoint || 'Not exposed — MCP is a tool protocol, not inference';
  if ($('servingStatus')) {
    $('servingStatus').textContent = payload?.remote_enabled ? 'LAN enabled' : 'Local only';
    $('servingStatus').dataset.state = 'ready';
  }
  return payload;
}

async function setRemoteServing(enabled) {
  const payload = await postEngCoder('/api/engcoder/serving', { remote_enabled: !!enabled });
  if ($('remoteServing')) $('remoteServing').checked = payload?.remote_enabled === true;
  if ($('localUiUrl')) $('localUiUrl').textContent = payload?.local_url || `${engcoderBase()}/ui/`;
  if ($('remoteUiUrl')) $('remoteUiUrl').textContent = payload?.remote_enabled ? (payload?.remote_url || 'Enabled on LAN') : 'Disabled';
  if ($('openAiBaseUrl')) $('openAiBaseUrl').textContent = payload?.local_openai_base || `${engcoderBase()}/v1`;
  if ($('chatCompletionsUrl')) $('chatCompletionsUrl').textContent = payload?.local_chat_completions || `${engcoderBase()}/v1/chat/completions`;
  if ($('responsesUrl')) $('responsesUrl').textContent = payload?.local_responses || `${engcoderBase()}/v1/responses`;
  if ($('agentTasksUrl')) $('agentTasksUrl').textContent = payload?.local_agent_tasks || `${engcoderBase()}/api/tasks`;
  if ($('agentEventsUrl')) $('agentEventsUrl').textContent = payload?.local_agent_events || `${engcoderBase()}/api/sessions/{session_id}/events?after=0`;
  if ($('mcpEndpointUrl')) $('mcpEndpointUrl').textContent = payload?.mcp_endpoint || 'Not exposed — MCP is a tool protocol, not inference';
  if ($('servingStatus')) {
    $('servingStatus').textContent = payload?.remote_enabled ? 'LAN enabled' : 'Local only';
    $('servingStatus').dataset.state = 'ready';
  }
  return payload;
}

async function refreshProviderStatus() {
  const response = await fetch(`${engcoderBase()}/api/engcoder/provider`, { cache: 'no-store' });
  const payload = await response.json().catch(() => null);
  if (!response.ok) throw new Error(payload?.error || `Provider HTTP ${response.status}`);
  if ($('providerName')) $('providerName').textContent = payload?.provider || 'LM Studio / OpenAI-compatible';
  if ($('providerUrl')) $('providerUrl').textContent = payload?.base_url || 'Not configured';
  if ($('providerStatus')) {
    $('providerStatus').textContent = payload?.ok ? 'Connected' : (payload?.token_configured ? 'Unavailable' : 'Needs attention');
    $('providerStatus').dataset.state = payload?.ok ? 'ready' : 'error';
  }
  if ($('providerHint')) {
    const error = String(payload?.error || '');
    $('providerHint').textContent = payload?.ok
      ? `${payload?.token_configured ? 'Authenticated' : 'No API token required'} · provider reachable.`
      : (error || 'Provider is not reachable. Start the model server or check its authentication settings.');
  }
  return payload;
}

async function updateProviderToken(token) {
  const payload = await postEngCoder('/api/engcoder/provider', { token: String(token || '') });
  if ($('providerToken')) $('providerToken').value = '';
  await refreshProviderStatus();
  await probeModels(false);
  return payload;
}

function useFirstExternalModel() {
  // Fill only the subordinate roles. Never replace a working KeyMaker Qwen
  // foundation as a side-effect of convenience setup.
  const external = (modelDiscovery?.models || []).map(item => {
    if (typeof item === 'string') return item;
    return item && item.internal !== true && item.native !== true ? String(item.id || '') : '';
  }).filter(value => value && !managedModelId(value) && value !== $('foundationModel')?.value);
  const distinct = [...new Set(external)];
  if (!distinct.length) throw new Error('No external LM Studio models discovered. Load models in LM Studio, start its API server, then Refresh.');
  const small = $('simpleResponseModel');
  const middle = $('reviewModel');
  if (small && [...small.options].some(option => option.value === distinct[0])) {
    small.value = distinct[0];
    localStorage.setItem(STORAGE_KEYS.simpleResponseModel, distinct[0]);
  }
  syncReviewModelChoices();
  const reviewerId = distinct.find(value => value !== (small?.value || '') && value !== $('foundationModel')?.value) || '';
  if (middle) middle.value = [...middle.options].some(option => option.value === reviewerId) ? reviewerId : '';
  localStorage.setItem(STORAGE_KEYS.reviewModel, middle?.value || '');
  if ($('simpleRoutingStatus')) $('simpleRoutingStatus').textContent = `Small model: ${small?.value || 'not assigned'}. KeyMaker is unchanged.`;
  if ($('reviewRoutingStatus')) $('reviewRoutingStatus').textContent = middle?.value
    ? `Middle reviewer: ${middle.value}. KeyMaker remains final.`
    : 'A second distinct external model is required for middle review. KeyMaker remains final.';
  saveSettings();
  return { small: small?.value || '', middle: middle?.value || '' };
}

async function probeModels(showStatus = true) {
  if (showStatus) setStatus('Connecting', 'busy');
  showError('');
  updateRuntimeEndpointFromMode();
  try {
    const base = normalizeBaseUrl(window.location.origin);
    const [healthResponse, discoveryResponse, architectureResponse] = await Promise.all([
      fetch(`${base}/health`, { cache: 'no-store' }),
      fetch(`${base}/api/model/discovery`, { cache: 'no-store' }),
      fetch(`${base}/api/engcoder/architecture`, { cache: 'no-store' })
    ]);

    const health = await healthResponse.json().catch(() => null);
    if (!healthResponse.ok || !health?.ok) throw new Error(health?.error || `Runtime HTTP ${healthResponse.status}`);
    modelDiscovery = await discoveryResponse.json().catch(() => null);
    runtimeArchitecture = await architectureResponse.json().catch(() => null);
    if (!discoveryResponse.ok || !modelDiscovery) throw new Error(modelDiscovery?.error || `Model discovery HTTP ${discoveryResponse.status}`);
    if (!architectureResponse.ok || !runtimeArchitecture) throw new Error(runtimeArchitecture?.error || `Architecture HTTP ${architectureResponse.status}`);
    modelRegistry = runtimeArchitecture?.model_registry || { components: [] };

    window.__engcoderArchitecture = runtimeArchitecture;
    if ($('endpoint')) $('endpoint').value = base;
    models = modelRegistry.components || [];
    populateArchitectureSelectors();
    renderModelRoles();
    renderResidency(runtimeArchitecture?.residency?.physical || null);
    renderTrainingStatus(runtimeArchitecture?.training || null);
    const healthLabel = $('runtimeHealth');
    if (healthLabel) { healthLabel.textContent = `Ready · v${health.version || APP_VERSION}`; healthLabel.dataset.state = 'ready'; }
    if (showStatus) setStatus('Ready', 'idle');
  } catch (error) {
    models = [];
    renderModelRoles();
    const healthLabel = $('runtimeHealth');
    if (healthLabel) { healthLabel.textContent = 'Unavailable'; healthLabel.dataset.state = 'error'; }
    if (showStatus) setStatus('Error', 'error');
    showError(String(error?.message || error));
  }
}

function saveSettings() {
  if ($('endpoint')) $('endpoint').value = normalizeBaseUrl(window.location.origin);
  localStorage.setItem(STORAGE_KEYS.model, $('model').value.trim());
  localStorage.setItem(STORAGE_KEYS.simpleResponseModel, $('simpleResponseModel')?.value || '');
  localStorage.setItem(STORAGE_KEYS.reviewModel, $('reviewModel')?.value || '');
  localStorage.setItem(STORAGE_KEYS.reviewMode, $('reviewMode')?.value || 'auto');
  localStorage.setItem(STORAGE_KEYS.thinkingMode, $('thinkingMode')?.value || 'auto');
  localStorage.removeItem(STORAGE_KEYS.apiKey);
  localStorage.setItem(STORAGE_KEYS.system, $('system').value);
  localStorage.setItem(STORAGE_KEYS.temp, $('temperature').value);
  localStorage.setItem(STORAGE_KEYS.maxTokens, $('max_tokens').value);
  localStorage.setItem(STORAGE_KEYS.stream, $('stream').checked ? '1' : '0');
  localStorage.setItem(STORAGE_KEYS.voiceLang, $('voiceLang')?.value || 'en-US');
  localStorage.setItem(STORAGE_KEYS.speakReplies, $('speakReplies')?.checked ? '1' : '0');
  localStorage.setItem(STORAGE_KEYS.includeHistory, $('includeHistory')?.checked ? '1' : '0');
  localStorage.setItem(STORAGE_KEYS.maxHistoryTurns, String($('maxHistoryTurns')?.value ?? 0));
  localStorage.setItem(STORAGE_KEYS.autoRelisten, $('autoRelisten')?.checked ? '1' : '0');
  localStorage.setItem(STORAGE_KEYS.silenceFinishMs, String($('silenceFinishMs')?.value ?? 1350));
  localStorage.setItem(STORAGE_KEYS.voiceRate, String($('voiceRate')?.value ?? 1));
  localStorage.setItem(STORAGE_KEYS.voicePitch, String($('voicePitch')?.value ?? 1));
  localStorage.setItem(STORAGE_KEYS.thinkingExpandDefault, $('thinkingExpandDefault')?.checked ? '1' : '0');
  localStorage.setItem(STORAGE_KEYS.thinkingShowEmpty, $('thinkingShowEmpty')?.checked ? '1' : '0');
  localStorage.setItem(STORAGE_KEYS.theme, $('theme')?.value || 'dark');
  localStorage.setItem(STORAGE_KEYS.messageStyle, $('messageStyle')?.value || 'bubble');
  localStorage.setItem(STORAGE_KEYS.fontSize, $('fontSize')?.value || 'm');
  for (const id of ['foundationModel', 'controllerModel', 'addonCppModel', 'addonDocsModel', 'addonPlannerModel', 'addonToolsModel', 'addonRerankerModel', 'forcedAddon', 'trainingProfile', 'trainingScope', 'trainingSteps', 'checkpointSteps']) {
    if ($(id)) localStorage.setItem(STORAGE_KEYS[id], $(id).value);
  }
  localStorage.setItem(STORAGE_KEYS.autoRouting, $('autoRouting')?.checked ? '1' : '0');
  localStorage.setItem(STORAGE_KEYS.enableAddons, $('enableAddons')?.checked ? '1' : '0');
  localStorage.setItem(STORAGE_KEYS.showActivityDetails, $('showActivityDetails')?.checked ? '1' : '0');
}

let chatSaveTimer = null;
function saveChats() {
  if (chatSaveTimer !== null) { clearTimeout(chatSaveTimer); chatSaveTimer = null; }
  try {
    localStorage.setItem(STORAGE_KEYS.chats, JSON.stringify(chats));
    localStorage.setItem(STORAGE_KEYS.currentChat, String(currentChatId || ''));
  } catch (error) {
    // Quota exhausted (large transcripts / attachments). Preserve existing data
    // and notify the operator instead of silently pretending the save succeeded.
    console.error('EngCoder conversation persistence failed', error);
    showError('Conversation storage is full or unavailable. Export/clear old chats to preserve new messages.');
  }
}
function scheduleChatSave() {
  if (chatSaveTimer !== null) return;
  chatSaveTimer = setTimeout(saveChats, 150);
}
window.addEventListener('pagehide', saveChats);
window.addEventListener('visibilitychange', () => { if (document.hidden) saveChats(); });

function saveAll() {
  saveSettings();
  saveChats();
}

// The same native runtime serves the Desktop and Web clients. Keep only
// the shared palette synchronized; the user's local font/message layout is
// intentionally browser-specific. Failed remote requests never overwrite local
// appearance or block UI startup.
let localThemeChangeAt = 0;
async function syncSharedTheme() {
  if (document.hidden || Date.now() - localThemeChangeAt < 3500) return;
  try {
    const response = await fetch('/api/engcoder/appearance', { headers: authHeaders() });
    if (!response.ok) return;
    const payload = await response.json();
    const select = $('theme');
    if (!select || !Array.from(select.options).some(option => option.value === payload.theme)) return;
    if (Date.now() - localThemeChangeAt < 3500) return;
    if (select.value !== payload.theme) {
      select.value = payload.theme;
      localStorage.setItem(STORAGE_KEYS.theme, payload.theme);
      applyAppearance();
    }
  } catch { /* offline/local storage remains available */ }
}
async function persistSharedTheme() {
  const theme = $('theme')?.value;
  if (!theme) return;
  localThemeChangeAt = Date.now();
  try {
    const response = await fetch('/api/engcoder/appearance', {
      method: 'POST', headers: {'Content-Type':'application/json', ...authHeaders()},
      body: JSON.stringify({theme})
    });
    if (!response.ok) console.warn('EngCoder could not persist the shared theme');
  } catch { console.warn('EngCoder shared theme unavailable; browser-local theme retained'); }
}

// G4: apply appearance settings (theme, message style, font size).
function applyAppearance() {
  const theme = $('theme')?.value || 'dark';
  const messageStyle = $('messageStyle')?.value || 'bubble';
  const fontSize = $('fontSize')?.value || 'm';

  // Theme: set data-theme attribute on <html>.
  document.documentElement.dataset.theme = theme;

  // Message style: set data-message-style attribute on <body>.
  document.body.dataset.messageStyle = messageStyle;

  // Font size: set CSS custom property on :root.
  const sizeMap = { s: '14px', m: '16px', l: '18px', xl: '20px' };
  document.documentElement.style.setProperty('--app-font-size', sizeMap[fontSize] || '16px');
}

function loadAll() {
  const get = key => localStorage.getItem(key);
  loadModelRoles();
  if ($('endpoint')) $('endpoint').value = normalizeBaseUrl(window.location.origin);
  $('model').value = get(STORAGE_KEYS.model) || 'auto';
  localStorage.removeItem(STORAGE_KEYS.apiKey);
  $('apiKey').value = '';
  $('system').value = get(STORAGE_KEYS.system) || '';
  $('temperature').value = get(STORAGE_KEYS.temp) || '0.15';
  $('max_tokens').value = get(STORAGE_KEYS.maxTokens) || '-1';
  $('stream').checked = (get(STORAGE_KEYS.stream) || '1') === '1';
  if ($('voiceLang')) $('voiceLang').value = get(STORAGE_KEYS.voiceLang) || navigator.language || 'en-US';
  if ($('speakReplies')) $('speakReplies').checked = (get(STORAGE_KEYS.speakReplies) || '0') === '1';
  if ($('includeHistory')) $('includeHistory').checked = (get(STORAGE_KEYS.includeHistory) || '1') === '1';
  if ($('maxHistoryTurns')) $('maxHistoryTurns').value = get(STORAGE_KEYS.maxHistoryTurns) || '0';
  if ($('autoRelisten')) $('autoRelisten').checked = (get(STORAGE_KEYS.autoRelisten) || '1') === '1';
  if ($('silenceFinishMs')) $('silenceFinishMs').value = get(STORAGE_KEYS.silenceFinishMs) || '1350';
  if ($('voiceRate')) $('voiceRate').value = get(STORAGE_KEYS.voiceRate) || '1';
  if ($('voicePitch')) $('voicePitch').value = get(STORAGE_KEYS.voicePitch) || '1';
  if ($('thinkingExpandDefault')) $('thinkingExpandDefault').checked = (get(STORAGE_KEYS.thinkingExpandDefault) || '0') === '1';
  if ($('thinkingShowEmpty')) $('thinkingShowEmpty').checked = (get(STORAGE_KEYS.thinkingShowEmpty) || '0') === '1';
  if ($('theme')) $('theme').value = get(STORAGE_KEYS.theme) || 'dark';
  if ($('messageStyle')) $('messageStyle').value = get(STORAGE_KEYS.messageStyle) || 'bubble';
  if ($('fontSize')) $('fontSize').value = get(STORAGE_KEYS.fontSize) || 'm';
  if ($('forcedAddon')) $('forcedAddon').value = get(STORAGE_KEYS.forcedAddon) || '';
  if ($('reviewMode')) $('reviewMode').value = get(STORAGE_KEYS.reviewMode) || 'auto';
  if ($('thinkingMode')) $('thinkingMode').value = get(STORAGE_KEYS.thinkingMode) || 'auto';
  if ($('autoRouting')) $('autoRouting').checked = (get(STORAGE_KEYS.autoRouting) || '1') === '1';
  if ($('enableAddons')) $('enableAddons').checked = (get(STORAGE_KEYS.enableAddons) || '1') === '1';
  if ($('showActivityDetails')) $('showActivityDetails').checked = (get(STORAGE_KEYS.showActivityDetails) || '1') === '1';
  if ($('trainingProfile')) $('trainingProfile').value = get(STORAGE_KEYS.trainingProfile) || 'desktop-16gb';
  if ($('trainingScope')) $('trainingScope').value = get(STORAGE_KEYS.trainingScope) || 'keymaker-foundation';
  if ($('trainingSteps')) $('trainingSteps').value = get(STORAGE_KEYS.trainingSteps) || '500';
  if ($('checkpointSteps')) $('checkpointSteps').value = get(STORAGE_KEYS.checkpointSteps) || '50';

  const raw = get(STORAGE_KEYS.chats);
  if (raw) {
    try { chats = sanitizeChats(JSON.parse(raw)); } catch { chats = []; }
  }

  if (!chats.length) chats = [{ id: Date.now(), name: 'Conversation 1', messages: [], runtimeSessionId: '' }];
  currentChatId = Number(get(STORAGE_KEYS.currentChat)) || chats[0].id;
  if (!currentChat()) currentChatId = chats[0].id;
  for (const chat of chats) {
    if (!Array.isArray(chat.messages)) continue;
    for (const msg of chat.messages) {
      if (msg && msg.role === 'assistant' && !msg.imageData && !msg.thinking && typeof msg.content === 'string') {
        const parts = splitThinkingFromText(msg.content);
        if (parts.thinking) {
          msg.content = parts.content;
          msg.thinking = parts.thinking;
          msg.thinkingOpen = false;
          msg.thinkingLabel = 'Thinking';
          msg.hasThinking = true;
        }
      }
    }
  }
  renderChatList();
  renderActiveChat();
  refreshLastUserFromChat();
  saveAll();
}


