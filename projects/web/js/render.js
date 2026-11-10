function closeThinkingDrawer() {
  thinkingDrawer.classList.remove('open');
  applyResponsiveShell();
}

function assignedKeyMakerModel(state) {
  function shortModelLabel(value) {
    const model = String(value || '').trim();
    if (!model) return '';
    // Only presentation is shortened; actual request and Retry still use the full ID.
    if (model.startsWith('qwenflash://')) {
      const known = ((typeof nativeModelLibrary !== 'undefined' && nativeModelLibrary?.models) || []).find(entry => entry.uri === model);
      return known?.display_name || model.slice('qwenflash://'.length);
    }
    if (/[/\\]/.test(model) && /\.gguf$/i.test(model)) {
      return model.split(/[/\\]/).pop().replace(/-00001-of-\d{5}\.gguf$/i, '.gguf');
    }
    return model;
  }

  const keymaker = state?.executionStats?.keymaker || {};
  const candidate = String(
    keymaker.resolvedModel || keymaker.model || state?.completionModel ||
    keymaker.requestedModel || runtimeArchitecture?.foundation?.model || ''
  ).trim();
  if (!candidate || candidate === 'auto') return 'Provider-selected model';
  return shortModelLabel(candidate);
}

function routeSourceLabel(source) {
  switch (String(source || '')) {
    case 'architect-model': return 'Architect model';
    case 'deterministic-fallback': return 'Deterministic fallback';
    case 'session-forced': return 'Session-forced';
    case 'session-core-only': return 'Foundation only';
    default: return source || 'N/A';
  }
}

function architectModelLabel(architect) {
  const configured = String(architect?.configuredModel || architect?.model || '').trim();
  if (architect?.modelUsed) return configured || 'Architect model';
  if (architect?.modelAttempted) return configured ? `${configured} (attempted; fallback used)` : 'Attempted; fallback used';
  return 'Not used for this route';
}

function selectedCapabilityLabel(architect) {
  return architect?.addon ? `${String(architect.addon).toUpperCase()} addon` : 'KeyMaker Foundation';
}

function addonAssignmentsLabel(addons) {
  const names = Array.isArray(addons?.names) ? addons.names : [];
  const models = addons?.models && typeof addons.models === 'object' ? addons.models : {};
  if (!names.length) return 'None';
  return names.map(name => models[name] ? `${name}: ${models[name]}` : String(name)).join(', ');
}

function preserveActivityOpenState() {
  if (!activeThinkingState) return;
  const details = thinkingDrawerBody.querySelector('[data-activity-thinking]');
  if (details) activeThinkingState.activityThinkingOpen = details.open;
}

function sectionHtml(title, body, extra = '') {
  return `<section class="activity-section">
    <div class="activity-section-header">${title}${extra}</div>
    <div class="activity-section-body">${body}</div>
  </section>`;
}

function runInfoRow(label, value) {
  const text = String(value ?? '').trim();
  return `<div class="k">${esc(label)}</div><div>${esc(text || 'N/A')}</div>`;
}

function percentText(value) {
  const number = Number(value);
  return Number.isFinite(number) ? `${Math.round(number * 100)}%` : 'N/A';
}

function syncGeneratedOutput() {
  if (!activeThinkingState) {
    thinkingDrawerBody.textContent = 'Select an assistant message to view generated output.';
    return;
  }

  const state = activeThinkingState;
  // The panel is output-only. Internal routing/tool/diagnostic events still
  // exist in the session store; none are rendered in this stream.
  const output = state.rawOutputFormat === 'generated-v1' && state.rawOutput
    ? String(state.rawOutput)
    : String(state.content || ''); // Legacy chats and non-streaming provider responses

  let pre = thinkingDrawerBody.querySelector('[data-generated-output]');
  if (!pre) {
    thinkingDrawerBody.replaceChildren();
    pre = document.createElement('pre');
    pre.className = 'raw-stream-output';
    pre.dataset.generatedOutput = '';
    thinkingDrawerBody.appendChild(pre);
  }

  // Keep following the output unless the reader scrolled up to inspect it.
  const atBottom = pre.scrollHeight - pre.scrollTop - pre.clientHeight < 48;
  const visible = output || 'Waiting for generated output…';
  if (pre.textContent !== visible) pre.textContent = visible;
  if (atBottom) pre.scrollTop = pre.scrollHeight;

}
// Quick-reference metrics belong at the top of Run; the detailed timing
// section remains unchanged. A trailing ~ marks the text-derived TPS estimate
// (provider tokenizer counts are not yet exposed by this backend).
function runElapsedMs(state) {
  const completed = Number(state?.completionDurationMs) || 0;
  if (!state?.thinkingLive && completed > 0) return completed;
  const started = Number(state?.taskStartedAtMs) || 0;
  return started > 0 && started <= Date.now() ? Date.now() - started : completed;
}

function runEstimatedTps(state, content) {
  if (!String(content || '').trim()) return 'N/A';
  const first = Number(state?.firstOutputAtMs) || 0;
  const duration = state?.thinkingLive && first > 0
    ? Math.max(0, Date.now() - first)
    : Number(state?.genDurationMs) || 0;
  const value = estimateTps(content, duration);
  return value === 'N/A' ? 'N/A' : `~${value} tok/s`;
}

function syncQwenLiveOutput() {
  let pre = thinkingDrawerBody.querySelector('[data-qwen-live-output]');
  if (!pre) {
    thinkingDrawerBody.replaceChildren();
    pre = document.createElement('pre');
    pre.className = 'raw-stream-output';
    pre.dataset.qwenLiveOutput = '';
    thinkingDrawerBody.appendChild(pre);
  }
  const atBottom = pre.scrollHeight - pre.scrollTop - pre.clientHeight < 48;
  const visible = qwenLiveState.rawOutput || (qwenLiveState.live
    ? 'Waiting for generated output…' : 'No Qwen generation recorded yet.');
  if (pre.textContent !== visible) pre.textContent = visible;
  if (atBottom) pre.scrollTop = pre.scrollHeight;
}

function syncRunInfo() {
  preserveActivityOpenState();
  if (!activeThinkingState) {
    thinkingDrawerBody.textContent = 'Select an assistant message to inspect its run information.';
    thinkingStateEl.textContent = 'Idle';
    return;
  }

  const state = activeThinkingState;
  const stats = state.executionStats && typeof state.executionStats === 'object' ? state.executionStats : {};
  const architect = stats.architect || {};
  const keymaker = stats.keymaker || {};
  const modelRoles = stats.modelRoles || {};
  const addons = stats.addons || {};
  const tools = stats.tools || {};
  const content = cleanAssistantContent(state.content || '');
  const totalMs = Number(state.completionDurationMs) || 0;
  const hasAnything = !!(state.thinking || content || state.completionReason || state.thinkingLive || stats.runtimeEvents);
  if (!hasAnything) {
    thinkingDrawerBody.innerHTML = '<div class="thinking-empty">Run information will appear when EngCoder starts the request.</div>';
    thinkingStateEl.textContent = 'Idle';
    return;
  }

  const overview = `<div class="activity-meta-grid">
    ${runInfoRow('State', state.thinkingLive ? 'Running' : (state.completionState || 'Finished'))}
    ${runInfoRow('Total time', formatDurationMs(runElapsedMs(state)))}
    ${runInfoRow('Generation speed (est.)', runEstimatedTps(state, content))}
    ${runInfoRow('Finish', state.thinkingLive ? 'In progress' : displayCompletionReason(state))}
    ${runInfoRow('Timestamp', state.timestampFull || formatTimestampFull(state.completionTimestamp) || '')}
    ${runInfoRow('Runtime events', Number(stats.runtimeEvents) || 0)}
  </div>`;

  const assignedModel = assignedKeyMakerModel(state);
  const requestedModel = String(keymaker.requestedModel || '').trim();
  const modelRows = [
    runInfoRow('Final response · actual model', state.respondingModel || modelRoles.finalModel || assignedModel),
    runInfoRow('Final response · role', (state.respondingRole || modelRoles.finalRole) === 'small-response' ? 'Small responder' : 'KeyMaker Foundation'),
    runInfoRow('KeyMaker · configured for this run', modelRoles.foundation || keymaker.requestedModel || 'Not recorded'),
    runInfoRow('Small preparation · completed by', modelRoles.draftUsed || 'Not completed'),
    runInfoRow('Small preparation · state', modelRoles.draftStatus || 'Not used'),
    runInfoRow('Middle review · completed by', modelRoles.middleUsed || 'Not completed'),
    runInfoRow('Middle review · state', modelRoles.middleStatus || 'Not used'),
    ...(requestedModel && requestedModel !== assignedModel ? [runInfoRow('Requested KeyMaker', requestedModel)] : []),
    runInfoRow('KeyMaker turns', Number(keymaker.turns) || 0),
    runInfoRow('Architect', architectModelLabel(architect)),
    runInfoRow('Routing', routeSourceLabel(architect.source)),
    runInfoRow('Capability', selectedCapabilityLabel(architect)),
    runInfoRow('Addon assignments', addonAssignmentsLabel(addons)),
    runInfoRow('Route confidence', percentText(architect.confidence)),
    runInfoRow('Retrieval requested', architect.retrieve ? 'Yes' : 'No'),
    runInfoRow('Architect reason', architect.reason || 'N/A'),
    ...(architect.modelFallbackReason ? [runInfoRow('Architect model fallback', architect.modelFallbackReason)] : [])
  ].join('');

  const timingRows = [
    runInfoRow('Total task', formatDurationMs(totalMs)),
    runInfoRow('Architect total', formatDurationMs(architect.durationMs)),
    runInfoRow('KeyMaker inference', formatDurationMs(keymaker.durationMs)),
    runInfoRow('Tool execution', formatDurationMs(tools.durationMs)),
    runInfoRow('Addon inference', formatDurationMs(addons.durationMs)),
    runInfoRow('First token', formatDurationMs(stats.timeToFirstTokenMs)),
    runInfoRow('Output tokens', stats.outputTokensEstimate || estimateTokenCount(content) || 0),
    runInfoRow('Output TPS ~', estimateTps(content, state.genDurationMs))
  ].join('');

  const toolNames = Array.isArray(tools.names) && tools.names.length ? tools.names.join(', ') : 'None';
  const toolRows = [
    runInfoRow('Calls', Number(tools.requested) || 0),
    runInfoRow('Completed', Number(tools.completed) || 0),
    runInfoRow('Failed', Number(tools.failed) || 0),
    runInfoRow('Blocked', Number(tools.denied) || 0),
    runInfoRow('Approval waits', Number(tools.approval) || 0),
    runInfoRow('Tools used', toolNames),
    runInfoRow('Addon calls', Number(addons.calls) || 0),
    runInfoRow('Addons used', addonAssignmentsLabel(addons)),
    runInfoRow('Memory recalled', Number(stats.memoryItems) || 0)
  ].join('');

  const events = Array.isArray(state.activityEvents) && state.activityEvents.length
    ? state.activityEvents
    : cleanThinkingContent(state.thinking || '').split('\n').filter(Boolean).map(text => ({ text }));
  const timelineOpen = !!state.activityThinkingOpen;
  const candidateEvidence = events.filter(item => item.type === 'iteration.escalated' || item.type === 'iteration.lesson');
  const candidate = candidateEvidence.length ? sectionHtml('Candidate repair evidence', candidateEvidence.map(item =>
    `<div class="activity-line">${esc(item.text || '')}</div>`).join('')) : '';
  const timeline = events.length ? `<section class="activity-section">
    <div class="activity-section-header">Runtime timeline</div>
    <details class="activity-details" data-activity-thinking ${timelineOpen ? 'open' : ''}>
      <summary>${events.length} stored event${events.length === 1 ? '' : 's'}</summary>
      <div class="activity-lines activity-drawer-lines">${events.map(item => {
        const when = item.timestampMs ? new Date(item.timestampMs).toLocaleTimeString([], { hour:'2-digit', minute:'2-digit', second:'2-digit' }) : '';
        return `<div class="activity-line activity-drawer-event">${when ? `<span class="activity-event-time">${esc(when)}</span>` : '<span></span>'}<span>${esc(item.text || item.type || '')}</span></div>`;
      }).join('')}</div>
    </details>
  </section>` : '';

  thinkingDrawerBody.innerHTML = `<div class="activity-sections">
    ${sectionHtml('Run', overview)}
    ${candidate}
    ${sectionHtml('Models & routing', `<div class="activity-meta-grid">${modelRows}</div>`)}
    ${sectionHtml('Timing', `<div class="activity-meta-grid">${timingRows}</div>`)}
    ${sectionHtml('Tools & context', `<div class="activity-meta-grid">${toolRows}</div>`)}
    ${timeline}
  </div>`;

  const details = thinkingDrawerBody.querySelector('[data-activity-thinking]');
  if (details) details.addEventListener('toggle', () => { if (activeThinkingState) activeThinkingState.activityThinkingOpen = details.open; });

}

function syncThinkingDrawer() {
  const view = rightPanelView === 'info' ? 'info' : rightPanelView === 'qwen' ? 'qwen' : 'output';
  for (const tab of [generatedOutputTab, typeof qwenLiveTab === 'undefined' ? null : qwenLiveTab, runInfoTab]) {
    if (!tab) continue;
    const selected = tab.dataset.runView === view;
    tab.classList.toggle('active', selected);
    tab.setAttribute('aria-selected', String(selected));
    tab.tabIndex = selected ? 0 : -1;
  }
  thinkingDrawerBody.setAttribute('aria-label', view === 'info' ? 'Run information' : view === 'qwen' ? 'Live Qwen output' : 'Generated output');
  if (view === 'info') syncRunInfo();
  else if (view === 'qwen') syncQwenLiveOutput();
  else syncGeneratedOutput();
  const state = activeThinkingState;
  thinkingStateEl.textContent = view === 'qwen' ? (qwenLiveState.live
    ? (qwenLiveState.awaitingNewOutput && qwenLiveState.rawOutput ? 'Qwen preparing · last output retained' : 'Qwen generating')
    : qwenLiveState.rawOutput ? 'Idle · last output retained' : 'Qwen idle') : !state ? 'Idle' : state.thinkingLive ? 'Generating' :
    (state.completionAt ? `Finished at ${state.completionAt}` : 'Finished');
}
// Opening the inspector is UI-only: never wait for inference, model loading,
// model discovery or expensive Run info rendering before showing the drawer.
let rightDrawerFrame = 0;
function scheduleRightDrawerRender({ layout = false } = {}) {
  if (rightDrawerFrame) return;
  rightDrawerFrame = requestAnimationFrame(() => {
    rightDrawerFrame = 0;
    if (!thinkingDrawer.classList.contains('open')) return;
    syncThinkingDrawer();
    if (layout) applyResponsiveShell();
  });
}
function openThinkingDrawer(state) {
  if (state) activeThinkingState = state;
  if (!activeThinkingState && rightPanelView !== 'qwen') {
    // Even without a selected response, let the user open the panel and
    // switch to Qwen Live / Run info while a native model is loading.
    rightPanelView = 'qwen';
  }
  thinkingDrawer.classList.add('open');
  syncShellLayout();
  scheduleRightDrawerRender({ layout: true });
}

function createMessageShell(role) {
  const wrap = document.createElement('div');
  wrap.className = `msg ${role}`;

  const avatar = document.createElement('div');
  avatar.className = 'avatar';
  avatar.textContent = role === 'assistant' ? 'A' : 'U';

  const bubble = document.createElement('div');
  bubble.className = 'bubble';

  const meta = document.createElement('div');
  meta.className = 'meta';
  meta.textContent = role === 'assistant' ? '' : role.charAt(0).toUpperCase() + role.slice(1);

  const body = document.createElement('div');
  body.className = 'bubble-body';

  let actions = null;
  let copy = null;
  if (role === 'user') {
    actions = document.createElement('div');
    actions.className = 'message-actions';

    copy = document.createElement('button');
    copy.className = 'message-action-btn';
    copy.type = 'button';
    copy.title = 'Copy';
    copy.setAttribute('aria-label', 'Copy');
    copy.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="9" y="9" width="13" height="13" rx="2"></rect><rect x="2" y="2" width="13" height="13" rx="2"></rect></svg>';
    actions.appendChild(copy);
    bubble.append(meta, body);
  } else {
    bubble.append(meta, body);
  }
  if (role === 'user') {
    const userStack = document.createElement('div');
    userStack.className = 'user-stack';
    userStack.appendChild(bubble);
    if (actions) {
      const actionRow = document.createElement('div');
      actionRow.className = 'user-action-row';
      actionRow.appendChild(actions);
      userStack.appendChild(actionRow);
    }
    wrap.append(userStack);
  } else {
    wrap.append(avatar, bubble);
  }
  chatEl.appendChild(wrap);
  maybeScroll();

  return { wrap, bubble, body, copy, actions };
}


function appendTextWithImages(block, source) {
  const text = String(source || '').replace(/\r\n?/g, '\n').replace(/\u0000/g, '');
  if (!text) return;
  const imagePattern = /(!\[[^\]]*\]\([^)]+\))|(data:image\/[^\s;]+;base64,[A-Za-z0-9+/=]+)/g;
  let lastIndex = 0;
  let matched = false;
  for (const m of text.matchAll(imagePattern)) {
    matched = true;
    const before = text.slice(lastIndex, m.index);
    if (before) block.appendChild(document.createTextNode(before));
    const img = document.createElement('img');
    img.className = 'inline-image';
    if (m[1]) {
      const closeBracket = m[1].indexOf(']');
      img.alt = m[1].slice(2, closeBracket) || 'Image';
      img.src = m[1].slice(closeBracket + 2, m[1].length - 1);
    } else {
      img.src = m[2];
      img.alt = 'Inline image';
    }
    block.appendChild(img);
    lastIndex = (m.index ?? 0) + m[0].length;
  }
  if (matched) {
    const after = text.slice(lastIndex);
    if (after) block.appendChild(document.createTextNode(after));
  } else if (text) {
    block.appendChild(document.createTextNode(text));
  }
}

function renderFormattedContent(container, text, { allowCode = true, clear = true } = {}) {
  if (clear) container.innerHTML = '';
  const host = document.createElement('div');
  host.className = 'content-blocks';
  const source = String(text || '').replace(/\r\n?/g, '\n').replace(/\u0000/g, '');
  if (!allowCode) {
    const block = document.createElement('div');
    block.className = 'text-block';
    appendTextWithImages(block, source);
    host.appendChild(block);
    container.appendChild(host);
    globalThis.EngCoderMath?.renderMathInChat(host);
    return;
  }

  const pattern = /\`\`\`([\w+-]*)\n?([\s\S]*?)\`\`\`/g;
  let lastIndex = 0;
  let matched = false;
  for (const match of source.matchAll(pattern)) {
    matched = true;
    const before = source.slice(lastIndex, match.index);
    if (before.trim() || before.includes('\n')) {
      const textBlock = document.createElement('div');
      textBlock.className = 'text-block';
      appendTextWithImages(textBlock, before.trim() ? before : before.replace(/^\n+|\n+$/g, ''));
      host.appendChild(textBlock);
    }
    const code = document.createElement('pre');
    code.className = 'code-block';
    code.textContent = match[2] || '';
    host.appendChild(code);
    lastIndex = (match.index ?? 0) + match[0].length;
  }
  const after = source.slice(lastIndex);
  if (!matched || after.trim() || after.includes('\n')) {
    const textBlock = document.createElement('div');
    textBlock.className = 'text-block';
    appendTextWithImages(textBlock, matched ? after.replace(/^\n+|\n+$/g, '') : source);
    host.appendChild(textBlock);
  }
  container.appendChild(host);
  // MathML is rendered from text nodes only; fenced code remains literal.
  globalThis.EngCoderMath?.renderMathInChat(host);
}

function renderAttachmentList(container, attachments, className = 'user-attachments') {
  if (!Array.isArray(attachments) || !attachments.length) return;
  const list = document.createElement('div');
  list.className = className;
  for (const att of attachments) {
    const card = document.createElement('div');
    card.className = 'attachment-card';
    if (att.kind === 'image' && att.preview) {
      const img = document.createElement('img');
      img.src = att.preview;
      img.alt = att.name || 'image';
      card.appendChild(img);
    }
    const meta = document.createElement('div');
    meta.className = 'attachment-meta';
    const name = document.createElement('div');
    name.className = 'attachment-name';
    name.textContent = att.name || (att.kind === 'image' ? 'Image attachment' : 'Text attachment');
    const kind = document.createElement('div');
    kind.className = 'attachment-kind';
    kind.textContent = att.kind === 'image' ? 'Image' : 'Text added to prompt';
    meta.append(name, kind);
    card.appendChild(meta);
    list.appendChild(card);

    if (att.kind === 'text' && att.text) {
      const block = document.createElement('details');
      block.className = 'thinking-wrap attachment-text';
      const summary = document.createElement('summary');
      summary.textContent = `${att.name || 'text file'} (content)`;
      const pre = document.createElement('pre');
      pre.className = 'activity-pre';
      pre.textContent = att.text;
      block.append(summary, pre);
      list.appendChild(block);
    }
  }
  container.appendChild(list);
}

function addAttachmentMessage(role, attachments, text) {
  const shell = createMessageShell(role);
  shell.copy.addEventListener('click', () => navigator.clipboard.writeText(text || ''));
  // Render the typed text for BOTH roles. Previously a user message that had
  // attachments AND text fell through both branches below and the text was
  // silently dropped from the UI (the "text bug"). Attachments render first
  // (matching the original assistant ordering), then the text.
  renderAttachmentList(shell.body, attachments, role === 'assistant' ? 'assistant-attachments' : 'user-attachments');
  if (text) renderFormattedContent(shell.body, text, { allowCode: role !== 'user', clear: false });
  return shell.body;
}

function cleanAssistantContent(text) {
  return String(text || '').replace(/^\n+/, '');
}

function cleanThinkingContent(text) {
  return String(text || '').replace(/^\n+/, '').replace(/\n+$/, '');
}

function cleanSingleLine(text) {
  return String(text || '').replace(/[\r\n]+/g, ' ').trim();
}

function getAssistantStatsLines(message) {
  const content = cleanAssistantContent(message?.content || '');
  const generations = estimateTokenCount(content) || 'N/A';
  const totalDisplayMs = (Number(message?.thinkDurationMs) > 0 || Number(message?.genDurationMs) > 0)
    ? Math.max(0, Number(message?.thinkDurationMs || 0) + Number(message?.genDurationMs || 0))
    : Number(message?.completionDurationMs || 0);
  const modelLine = `Model: ${cleanSingleLine(message?.completionModel || $('model')?.value?.trim() || 'N/A')}`;
  const metricsLine1 = `Finish Reason: ${displayCompletionReason(message)} • Generations: ${generations} • TPS~: ${estimateTps(content, message?.genDurationMs)}`;
  const metricsLine2 = `ThinkTime: ${formatDurationMs(message?.thinkDurationMs)} • GenTime: ${formatDurationMs(message?.genDurationMs)} • TotalTime: ${formatDurationMs(totalDisplayMs)}`;
  const metricsLine3 = cleanSingleLine(message?.timestampFull || formatTimestampFull(message?.completionTimestamp) || '');
  return { modelLine, metricsLine1, metricsLine2, metricsLine3, totalDisplayMs };
}

function getLastUserMessageText(chat = currentChat()) {
  if (!chat || !Array.isArray(chat.messages)) return '';
  for (let i = chat.messages.length - 1; i >= 0; --i) {
    if (chat.messages[i]?.role === 'user') return chat.messages[i].content || '';
  }
  return '';
}

function refreshLastUserFromChat() {
  lastUser = getLastUserMessageText();
}

function isLatestAssistantMessage(message) {
  const chat = currentChat();
  if (!chat || !Array.isArray(chat.messages) || !message) return false;
  for (let i = chat.messages.length - 1; i >= 0; --i) {
    if (chat.messages[i]?.role === 'assistant') return chat.messages[i] === message;
  }
  return false;
}



function startEditingUserMessage(messageIndex, content) {
  editingMessageIndex = Number.isInteger(messageIndex) ? messageIndex : -1;
  promptEl.value = content || '';
  autoresize();
  promptEl.focus();
}

function attachUserMessageActions(shell, messageIndex, content) {
  if (!shell?.actions) return;
  const editBtn = document.createElement('button');
  editBtn.type = 'button';
  editBtn.className = 'message-action-btn';
  editBtn.title = 'Edit';
  editBtn.setAttribute('aria-label', 'Edit');
  editBtn.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 20h9"></path><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4 12.5-12.5z"></path></svg>';
  editBtn.addEventListener('click', () => startEditingUserMessage(messageIndex, content || ''));
  shell.actions.appendChild(editBtn);
}

function setVerifyButtonState(button, saved, label = 'Saved for learning') {
  if (!button) return;
  button.classList.toggle('active', !!saved);
  button.setAttribute('aria-pressed', saved ? 'true' : 'false');
  button.title = saved ? label : 'Mark this response verified for EngCoder learning';
  button.setAttribute('aria-label', button.title);
  const status = button.parentElement?.querySelector('.assistant-feedback-status');
  if (status) {
    status.textContent = saved ? label : '';
    status.hidden = !saved;
  }
}

function isCandidateReply(message) {
  return message?.candidateIteration === true ||
    message?.activityEvents?.some(event => event?.type === 'iteration.started') === true;
}

function canSaveAssistantForLearning(message) {
  return !!message?.runtimeTaskId && !!message?.runtimeSessionId &&
    message.completionReason === 'engcoder-task' && !isCandidateReply(message);
}

async function markAssistantVerified(message, button) {
  if (!canSaveAssistantForLearning(message)) return;
  if (message?.verifiedForLearning) {
    setVerifyButtonState(button, true);
    return;
  }

  const prompt = String(message?.sourcePrompt || getLastUserMessageText() || '').trim();
  const responseText = cleanAssistantContent(message?.content || '').trim();
  if (!prompt || !responseText) {
    showError('This response cannot be saved for learning because its prompt or answer text is missing.');
    return;
  }

  const skillMap = {
    cpp: 'cpp23-core',
    docs: 'documentation',
    planner: 'planning',
    tools: 'tool-use',
    reranker: 'reranking'
  };
  const payload = {
    session_id: message.runtimeSessionId,
    task_id: message.runtimeTaskId,
    skill: skillMap[message.selectedAddon] || 'keymaker-foundation',
    prompt,
    response: cleanAssistantContent(message.content),
    source: 'user-accepted',
    confidence: 1.0,
    verified: true
  };

  if (button) button.disabled = true;
  try {
    const response = await fetch(`${engcoderBase()}/api/engcoder/learning/evidence`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const result = await response.json().catch(() => null);
    const duplicate = response.status === 400 && result?.stored === false;
    if (!response.ok && !duplicate) {
      throw new Error(result?.error || `Learning HTTP ${response.status}`);
    }

    message.sourcePrompt = prompt;
    message.verifiedForLearning = true;
    const chat = currentChat?.();
    if (chat?.messages) {
      for (let index = chat.messages.length - 1; index >= 0; --index) {
        const item = chat.messages[index];
        if (item?.role !== 'assistant') continue;
        if (cleanAssistantContent(item.content || '') === responseText &&
            item.runtimeTaskId === message.runtimeTaskId &&
            item.runtimeSessionId === message.runtimeSessionId) {
          item.sourcePrompt = prompt;
          item.verifiedForLearning = true;
          break;
        }
      }
    }

    setVerifyButtonState(button, true, duplicate ? 'Already saved' : 'Saved for learning');
    showError('');
    saveAll();
    void refreshLearningStatus?.().catch?.(() => {});
  } catch (error) {
    setVerifyButtonState(button, false);
    showError(`Could not save verified learning evidence: ${String(error?.message || error)}`);
  } finally {
    if (button) button.disabled = false;
  }
}

function renderAssistantBody(container, message) {
  container.innerHTML = '';
  const thinking = message?.thinking || '';
  const content = cleanAssistantContent(message?.content || '');
  // G4: respect thinking settings (show-even-when-empty + expand-by-default).
  const showEmpty = $('thinkingShowEmpty')?.checked === true;
  const expandDefault = $('thinkingExpandDefault')?.checked === true;
  const shouldShowThinking = !!(thinking || message?.thinkingLive || showEmpty);
  const isDone = !message?.thinkingLive && !!(content || thinking || message?.completionReason || message?.completionAt || message?.streamEndedBy);

  if (shouldShowThinking) {
    const controls = document.createElement('div');
    controls.className = 'assistant-controls';

    const details = document.createElement('details');
    details.className = 'thinking-wrap';
    if (message?.thinkingLive) details.classList.add('live');
    // G4: respect expand-by-default (user toggle wins once set).
    details.open = message?.thinkingOpen ?? expandDefault;

    const summary = document.createElement('summary');
    summary.className = 'activity-summary';
    const summaryText = document.createElement('span');
    const assignedModel = assignedKeyMakerModel(message);
    summaryText.textContent = message?.thinkingLive
      ? `Working · ${assignedModel}`
      : `${assignedModel} · ${formatDurationMs(message?.completionDurationMs || message?.thinkDurationMs || 0)}`;
    summary.append(summaryText);

    const stats = message?.executionStats || {};
    const architect = stats.architect || {};
    const tools = stats.tools || {};
    const addons = stats.addons || {};
    const compact = document.createElement('div');
    compact.className = 'inline-run-summary';
    const rows = [
      ['Model', assignedModel],
      ['Route', `${selectedCapabilityLabel(architect)} · ${routeSourceLabel(architect.source)}`],
      ['Architect', architectModelLabel(architect)],
      ['Tools', Number(tools.requested) ? `${Number(tools.requested)} call${Number(tools.requested) === 1 ? '' : 's'}` : 'None'],
      ['Addons', addonAssignmentsLabel(addons)],
      ['Status', message?.thinkingLive ? 'Running' : displayCompletionReason(message)]
    ];
    for (const [label, value] of rows) {
      const row = document.createElement('div');
      row.className = 'inline-run-row';
      const key = document.createElement('span');
      key.className = 'inline-run-key';
      key.textContent = label;
      const text = document.createElement('span');
      text.textContent = value;
      row.append(key, text);
      compact.appendChild(row);
    }

    details.append(summary, compact);
    details.addEventListener('toggle', () => {
      activeThinkingState = message;
      message.thinkingOpen = details.open;
      if (thinkingDrawer.classList.contains('open')) syncThinkingDrawer();
    });
    controls.appendChild(details);
    container.appendChild(controls);
  }

  const contentHost = document.createElement('div');
  contentHost.className = 'assistant-content-host';
  container.appendChild(contentHost);

  if (content) {
    renderFormattedContent(contentHost, content, { allowCode: true, clear: true });
  } else if (!thinking && !message?.thinkingLive) {
    const empty = document.createElement('div');
    empty.className = 'text-block';
    empty.textContent = '';
    contentHost.appendChild(empty);
  }


  const approval = message?.approvalRequest;
  if (approval) {
    const card = document.createElement('section');
    card.className = 'approval-card';
    card.setAttribute('aria-live', 'polite');

    const title = document.createElement('div');
    title.className = 'approval-title';
    title.textContent = `Approval required · ${approval.tool || 'tool'}`;
    card.appendChild(title);

    if (approval.reason) {
      const reason = document.createElement('div');
      reason.className = 'approval-reason';
      reason.textContent = approval.reason;
      card.appendChild(reason);
    }

    const args = approval.arguments && typeof approval.arguments === 'object' ? approval.arguments : {};
    const detail = document.createElement('pre');
    detail.className = 'approval-detail';
    if (approval.tool === 'process.run') {
      const executable = String(args.executable || '');
      const processArgs = Array.isArray(args.arguments) ? args.arguments.map(value => String(value)) : [];
      const command = [executable, ...processArgs].filter(Boolean).join(' ');
      const cwd = String(args.cwd || '.');
      detail.textContent = `${command || 'process.run'}\ncwd: ${cwd}`;
    } else {
      detail.textContent = approval.tool === 'workspace.patch'
        ? `File: ${String(args.path || '')}\n\nReplace exactly:\n${String(args.expected ?? '')}\n\nWith:\n${String(args.replacement ?? '')}`
        : JSON.stringify(args, null, 2);
    }
    card.appendChild(detail);

    if (approval.error) {
      const error = document.createElement('div');
      error.className = 'approval-error';
      error.textContent = approval.error;
      card.appendChild(error);
    }

    const actions = document.createElement('div');
    actions.className = 'approval-actions';

    const approve = document.createElement('button');
    approve.type = 'button';
    approve.className = 'approval-btn approval-btn-primary';
    approve.textContent = approval.decision === 'approved' ? 'Approved' : (approval.submitted ? 'Waiting…' : 'Approve once');
    approve.disabled = approval.pending || approval.submitted;
    approve.addEventListener('click', () => void resolveTaskApproval(message, true, container));

    const deny = document.createElement('button');
    deny.type = 'button';
    deny.className = 'approval-btn';
    deny.textContent = approval.decision === 'denied' ? 'Denied' : (approval.submitted ? 'Waiting…' : 'Deny');
    deny.disabled = approval.pending || approval.submitted;
    deny.addEventListener('click', () => void resolveTaskApproval(message, false, container));

    actions.append(approve, deny);
    card.appendChild(actions);
    container.appendChild(card);
  }

  if (isDone && message?.showRegenerate) {
    const footer = document.createElement('div');
    footer.className = 'assistant-footer';

    const actions = document.createElement('div');
    actions.className = 'assistant-actions';

      const regen = document.createElement('button');
      regen.type = 'button';
      regen.className = 'assistant-action-btn';
      const retryRun = message?.completionState === 'Error' || message?.completionState === 'Stopped' || !String(message?.content || '').trim();
      regen.title = retryRun ? 'Retry' : 'Regenerate';
      regen.setAttribute('aria-label', retryRun ? 'Retry' : 'Regenerate');
      regen.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 11a8 8 0 0 0-14.9-4"></path><path d="M4 4v5h5"></path><path d="M4 13a8 8 0 0 0 14.9 4"></path><path d="M20 20v-5h-5"></path></svg>';
      regen.addEventListener('click', () => {
        if (controller) return;
        refreshLastUserFromChat();
        if (lastUser) sendMessage({ regenerate: true, targetIndex: Number.isInteger(message?.retryTargetIndex) ? message.retryTargetIndex : -1 });
      });

      const copyBtn = document.createElement('button');
      copyBtn.type = 'button';
      copyBtn.className = 'assistant-action-btn';
      copyBtn.title = 'Copy';
      copyBtn.setAttribute('aria-label', 'Copy');
      copyBtn.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="9" y="9" width="13" height="13" rx="2"></rect><rect x="2" y="2" width="13" height="13" rx="2"></rect></svg>';
      copyBtn.addEventListener('click', () => {
        navigator.clipboard.writeText(cleanAssistantContent(message?.content || ''));
      });

      const verifyBtn = document.createElement('button');
      verifyBtn.type = 'button';
      verifyBtn.className = 'assistant-action-btn verify-action-btn';
      verifyBtn.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 10v10"></path><path d="M3 10h4v10H3z"></path><path d="M7 18h9.5a2 2 0 0 0 1.9-1.4l2-6A2 2 0 0 0 18.5 8H14l1-4a2 2 0 0 0-2-2l-1 1-5 7"></path></svg>';
      verifyBtn.addEventListener('click', () => void markAssistantVerified(message, verifyBtn));

      const feedbackStatus = document.createElement('span');
      feedbackStatus.className = 'assistant-feedback-status';
      feedbackStatus.hidden = !isCandidateReply(message);
      if (!feedbackStatus.hidden) feedbackStatus.textContent = 'Candidate reply · chat feedback unavailable';
      if (!isCandidateReply(message)) actions.append(regen);
      actions.append(copyBtn);
      const canLearn = canSaveAssistantForLearning(message);
      if (canLearn) actions.append(verifyBtn);
      if (canLearn && message?.verifiedForLearning === true) {
        const removeLearn = document.createElement('button');
        removeLearn.type = 'button';
        removeLearn.className = 'assistant-action-btn';
        removeLearn.title = 'Remove from learning';
        removeLearn.setAttribute('aria-label', 'Remove from learning');
        removeLearn.textContent = 'Remove';
        removeLearn.addEventListener('click', async () => {
          const prompt = String(message?.sourcePrompt || getLastUserMessageText() || '').trim();
          const answer = cleanAssistantContent(message?.content || '').trim();
          if (!prompt || !answer) return;
          const recent = await fetch(`${engcoderBase()}/api/engcoder/memory/recent?limit=100`, { cache: 'no-store' }).then(r => r.json());
          const episode = (recent?.items || []).find(item => String(item.prompt || '') === prompt && String(item.response || '') === answer);
          if (!episode) { showError('Learning item was not found. Open Learning & memory and refresh.'); return; }
          await removeLearningEpisode(episode);
          message.verifiedForLearning = false;
          saveAll();
          setVerifyButtonState(verifyBtn, false);
          removeLearn.remove();
          banLearn.remove();
        });
        const banLearn = document.createElement('button');
        banLearn.type = 'button';
        banLearn.className = 'assistant-action-btn';
        banLearn.title = 'Remove and ban from all future learning';
        banLearn.setAttribute('aria-label', 'Remove and ban from learning');
        banLearn.textContent = 'Ban';
        banLearn.addEventListener('click', async () => {
          const episode = { prompt: String(message?.sourcePrompt || getLastUserMessageText() || '').trim(), response: cleanAssistantContent(message?.content || '').trim() };
          const result = await banLearningEpisode(episode);
          if (result?.cancelled) return;
          message.verifiedForLearning = false;
          saveAll();
          setVerifyButtonState(verifyBtn, false);
          removeLearn.remove();
          banLearn.remove();
        });
        actions.append(removeLearn, banLearn);
      }
      actions.append(feedbackStatus);
      setVerifyButtonState(verifyBtn, message?.verifiedForLearning === true);
      footer.appendChild(actions);
    container.appendChild(footer);
  }
}

function addTextMessage(role, content, options = {}) {
  const shell = createMessageShell(role);
  const copyText = () => {
    if (role === 'assistant' && options && typeof options === 'object' && ('thinking' in options || 'content' in options)) {
      return cleanAssistantContent(options.content || content || '');
    }
    return content || '';
  };
  if (shell.copy) shell.copy.addEventListener('click', () => navigator.clipboard.writeText(copyText()));

  if (role === 'assistant' && options && typeof options === 'object' && ('thinking' in options || 'content' in options)) {
    const state = {
      content: cleanAssistantContent(options.content || content || ''),
      thinking: options.thinking || '',
      thinkingOpen: options.thinkingOpen === true,
      thinkingLive: !!options.thinkingLive,
      thinkingLabel: options.thinkingLabel || 'Activity',
      completionReason: options.completionReason || '',
      streamEndedBy: options.streamEndedBy || '',
      completionAt: options.completionAt || '',
      completionTimestamp: Number(options.completionTimestamp) || 0,
      completionModel: options.completionModel || '',
      respondingModel: options.respondingModel || '',
      respondingRole: options.respondingRole || '',
      retryTargetIndex: Number.isInteger(options.retryTargetIndex) ? options.retryTargetIndex : -1,
      rawOutput: options.rawOutput || '',
      rawOutputFormat: options.rawOutputFormat || '',
      reasoningOutput: options.reasoningOutput || '',
      completionDurationMs: Number(options.completionDurationMs) || 0,
      thinkDurationMs: Number(options.thinkDurationMs) || 0,
      genDurationMs: Number(options.genDurationMs) || 0,
      taskStartedAtMs: Number(options.taskStartedAtMs) || 0,
      firstOutputAtMs: Number(options.firstOutputAtMs) || 0,
      completionState: options.completionState || 'Idle',
      timestampFull: options.timestampFull || '',
      activityThinkingOpen: options.activityThinkingOpen === true,
      sourcePrompt: options.sourcePrompt || '',
      runtimeTaskId: options.runtimeTaskId || '',
      runtimeSessionId: options.runtimeSessionId || '',
      candidateIteration: options.candidateIteration === true,
      selectedAddon: options.selectedAddon || '',
      verifiedForLearning: options.verifiedForLearning === true,
      executionStats: options.executionStats && typeof options.executionStats === 'object' ? options.executionStats : null,
      activityEvents: Array.isArray(options.activityEvents) ? options.activityEvents : [],
      approvalRequest: options.approvalRequest && typeof options.approvalRequest === 'object' ? options.approvalRequest : null,
      showRegenerate: options.showRegenerate === true
    };
    renderAssistantBody(shell.body, state);
    shell.wrap.__assistantState = state;
    if (thinkingDrawer.classList.contains('open') && state.thinking && !activeThinkingState) {
      activeThinkingState = state;
      syncThinkingDrawer();
    }
    return shell.body;
  }

  renderFormattedContent(shell.body, content || '', { allowCode: role !== 'user' });
  if (role === 'user') {
    if (shell.copy) shell.copy.onclick = () => navigator.clipboard.writeText(content || '');
    attachUserMessageActions(shell, Number(options?.messageIndex), content || '');
  }
  return shell.body;
}

function updateAssistantMessage(container, state) {
  if (!container) return;
  const existingDetails = container.querySelector('.thinking-wrap');
  if (existingDetails && state.thinkingLive) state.thinkingOpen = existingDetails.open;
  container.closest('.msg').__assistantState = state;
  renderAssistantBody(container, state);
  if (activeThinkingState === state || (thinkingDrawer.classList.contains('open') && !activeThinkingState)) {
    activeThinkingState = state;
    syncThinkingDrawer();
  }
  maybeScroll();
}

function renderActiveChat() {
  // Restoring a transcript must not run one scroll animation per old message.
  // Starting/switching a chat always shows its latest content.
  renderingChat = true;
  chatEl.innerHTML = '';
  activeThinkingState = null;
  syncThinkingDrawer();
  const chat = currentChat();
  for (const msg of chat?.messages || []) {
    if (Array.isArray(msg.attachments) && msg.attachments.length) {
      addAttachmentMessage(msg.role, msg.attachments, msg.content || '');
    } else if (msg.role === 'assistant') {
      addTextMessage(msg.role, msg.content || '', {
        content: msg.content || '',
        thinking: msg.thinking || '',
        thinkingOpen: msg.thinkingOpen === true,
        thinkingLive: !!msg.runtimeTaskId && !msg.completionReason,
        thinkingLabel: msg.thinkingLabel || 'Activity',
        completionReason: msg.completionReason || '',
        streamEndedBy: msg.streamEndedBy || '',
        finishReasonRaw: msg.finishReasonRaw || '',
        completionAt: msg.completionAt || '',
        completionTimestamp: msg.completionTimestamp || 0,
        completionModel: msg.completionModel || '',
        respondingModel: msg.respondingModel || '',
        respondingRole: msg.respondingRole || '',
        retryTargetIndex: chat.messages.indexOf(msg),
        completionDurationMs: msg.completionDurationMs || 0,
        thinkDurationMs: msg.thinkDurationMs || 0,
        genDurationMs: msg.genDurationMs || 0,
        taskStartedAtMs: msg.taskStartedAtMs || 0,
        firstOutputAtMs: msg.firstOutputAtMs || 0,
        completionState: msg.completionState || 'Idle',
        timestampFull: msg.timestampFull || '',
        sourcePrompt: msg.sourcePrompt || '',
        selectedAddon: msg.selectedAddon || '',
        verifiedForLearning: msg.verifiedForLearning === true,
        executionStats: msg.executionStats || null,
        activityEvents: Array.isArray(msg.activityEvents) ? msg.activityEvents : [],
        rawOutput: msg.rawOutput || '',
        rawOutputFormat: msg.rawOutputFormat || '',
        reasoningOutput: msg.reasoningOutput || '',
        runtimeTaskId: msg.runtimeTaskId || '',
        runtimeSessionId: msg.runtimeSessionId || chat.runtimeSessionId || '',
        candidateIteration: msg.candidateIteration === true,
        approvalRequest: msg.approvalRequest || null,
        lastEventSequence: msg.lastEventSequence || 0,
        showRegenerate: isLatestAssistantMessage(msg)
      });
    } else {
      addTextMessage(msg.role, msg.content || '', { messageIndex: chat.messages.indexOf(msg) });
    }
  }
  renderingChat = false;
  maybeScroll(false, true);
}

function renderChatList() {
  chatListEl.innerHTML = '';
  for (const chat of chats) {
    const li = document.createElement('li');
    li.textContent = chat.name;
    li.dataset.id = String(chat.id);
    if (chat.id === currentChatId) li.classList.add('active');
    li.addEventListener('click', () => {
      currentChatId = chat.id;
      renderChatList();
      renderActiveChat();
      refreshLastUserFromChat();
      saveAll();
    });
    li.addEventListener('contextmenu', event => {
      event.preventDefault();
      ctxMenu.style.left = `${event.pageX}px`;
      ctxMenu.style.top = `${event.pageY}px`;
      ctxMenu.style.display = 'block';
      ctxMenu.dataset.id = String(chat.id);
    });
    chatListEl.appendChild(li);
  }
}

function newChat() {
  const chat = currentChat();
  if (chat && chat.messages.length > 0) {
    const confirmed = confirm('Start a new chat? Your current conversation will be saved.');
    if (!confirmed) return;
  }
  const id = Date.now();
  chats.unshift({ id, name: `Conversation ${chats.length + 1}`, messages: [], runtimeSessionId: '' });
  currentChatId = id;
  renderChatList();
  renderActiveChat();
  refreshLastUserFromChat();
  saveAll();
}

function deleteChat(id) {
  const removed = chats.find(chat => chat.id === id);
  if (controller && removed?.runtimeSessionId && removed.runtimeSessionId === activeEngCoderSessionId) {
    showError('Stop the active run before deleting this chat.');
    return;
  }
  if (removed?.runtimeSessionId) void deleteRuntimeSession(removed.runtimeSessionId);
  chats = chats.filter(chat => chat.id !== id);
  if (!chats.length) chats = [{ id: Date.now(), name: 'Conversation 1', messages: [], runtimeSessionId: '' }];
  if (!currentChat()) currentChatId = chats[0].id;
  renderChatList();
  renderActiveChat();
  refreshLastUserFromChat();
  saveAll();
}


function splitThinkingFromText(text, options = {}) {
  const source = String(text || '');
  if (!source) return { content: '', thinking: '' };

  const marker = source.match(/(?:^|\n)(Thinking(?:\s+Process)?\s*:)/i);
  if (!marker) return { content: source, thinking: '' };

  const start = marker.index + (marker[0].startsWith('\n') ? 1 : 0);
  const after = source.slice(start);
  const finalMatch = after.match(/\n\s*(Final(?:\s+Answer)?|Answer|Response)\s*:\s*/i);
  if (finalMatch) {
    const split = start + finalMatch.index;
    const thinking = source.slice(start, split).trim();
    const content = after.slice(finalMatch.index + finalMatch[0].length).trim();
    return { content, thinking };
  }

  /*
    No Final/Answer/Response marker found, so we cannot reliably tell where
    the thinking ends and the answer begins. Keep the full text as visible
    content instead of burying the whole answer in the Thinking drawer
    (Qwen3-style inline "Thinking: ..." without a closing marker).
    Callers that are extracting an answer OUT of the thinking field pass
    keepAsContent:false to preserve the old behavior.
  */
  if (options.keepAsContent === false) return { content: '', thinking: after.trim() };
  return { content: source, thinking: '' };
}

function normalizeAssistantStreamOutput(content, thinking) {
  let finalContent = cleanAssistantContent(content || '');
  let finalThinking = cleanThinkingContent(thinking || '');

  const contentLooksLikeThinking = /(^|\n)\s*(Thinking(?:\s+Process)?|Reasoning)\s*:/i.test(finalContent);
  if (contentLooksLikeThinking) {
    const parts = splitThinkingFromText(finalContent);
    finalContent = cleanAssistantContent(parts.content || '');
    finalThinking = cleanThinkingContent([finalThinking, parts.thinking].filter(Boolean).join('\n\n'));
  }

  const thinkingLooksStructured = /(^|\n)\s*(Thinking(?:\s+Process)?|Reasoning)\s*:/i.test(finalThinking);
  const hasFinalMarkerInThinking = /(^|\n)\s*(Final(?:\s+Answer)?|Answer|Response)\s*:/i.test(finalThinking);
  if ((!finalContent || hasFinalMarkerInThinking) && thinkingLooksStructured) {
    const parts = splitThinkingFromText(finalThinking, { keepAsContent: false });
    finalContent = cleanAssistantContent(finalContent || parts.content || '');
    finalThinking = cleanThinkingContent(parts.thinking || finalThinking);
  }

  return { content: finalContent, thinking: finalThinking };
}


function renderToolCallsLog(container, toolCallsLog) {
  if (!toolCallsLog || !Array.isArray(toolCallsLog) || toolCallsLog.length === 0) return;
  const wrapper = document.createElement('div');
  wrapper.className = 'tool-calls-log';

  const header = document.createElement('div');
  header.className = 'tool-calls-header';
  header.innerHTML = '<span class="tool-calls-icon">🔧</span> <span class="tool-calls-count">' + toolCallsLog.length + ' tool' + (toolCallsLog.length > 1 ? 's' : '') + ' used</span> <span class="tool-calls-toggle">▸</span>';
  wrapper.appendChild(header);

  const body = document.createElement('div');
  body.className = 'tool-calls-body';
  body.style.display = 'none';

  for (const tc of toolCallsLog) {
    const item = document.createElement('div');
    item.className = 'tool-call-item' + (tc.error ? ' tool-call-error' : '');

    const nameEl = document.createElement('div');
    nameEl.className = 'tool-call-name';
    nameEl.textContent = tc.name;
    item.appendChild(nameEl);

    const argsEl = document.createElement('div');
    argsEl.className = 'tool-call-args';
    argsEl.textContent = 'Args: ' + JSON.stringify(tc.args || {});
    item.appendChild(argsEl);

    const resultEl = document.createElement('div');
    resultEl.className = 'tool-call-result';
    const resultText = typeof tc.result === 'string' ? tc.result : JSON.stringify(tc.result);
    resultEl.textContent = resultText.length > 500 ? resultText.slice(0, 500) + '…' : resultText;
    item.appendChild(resultEl);

    if (tc.images && tc.images.length > 0) {
      for (const img of tc.images) {
        const imgEl = document.createElement('img');
        imgEl.className = 'tool-call-image';
        imgEl.src = 'data:' + (img.mimeType || 'image/png') + ';base64,' + img.data;
        imgEl.alt = 'Tool result image';
        item.appendChild(imgEl);
      }
    }

    body.appendChild(item);
  }

  wrapper.appendChild(body);
  container.appendChild(wrapper);

  header.addEventListener('click', () => {
    const isHidden = body.style.display === 'none';
    body.style.display = isHidden ? 'block' : 'none';
    header.querySelector('.tool-calls-toggle').textContent = isHidden ? '▾' : '▸';
  });
}

function renderInlineImages(container, text) {
  const imagePattern = /data:(image\/[\w+]+);base64,([A-Za-z0-9+/=]+)/g;
  let match;
  while ((match = imagePattern.exec(text)) !== null) {
    const imgEl = document.createElement('img');
    imgEl.className = 'inline-image';
    imgEl.src = match[0];
    imgEl.alt = 'Inline image';
    container.appendChild(imgEl);
  }
}

function renderMarkdownImages(container, text) {
  const mdPattern = /!\[([^\]]*)\]\(([^)]+)\)/g;
  let match;
  while ((match = mdPattern.exec(text)) !== null) {
    const imgEl = document.createElement('img');
    imgEl.className = 'inline-image';
    imgEl.src = match[2];
    imgEl.alt = match[1] || 'Image';
    container.appendChild(imgEl);
  }
}
