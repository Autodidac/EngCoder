let activeEngCoderTaskId = null;
let activeEngCoderSessionId = null;
let stopRequested = false;
// The right panel displays only bytes emitted as model output fragments.
// Fragments are not necessarily individual tokenizer IDs. Session events remain
// available in the native log, but never belong in the user-facing output stream.
const RAW_OUTPUT_LIMIT = 96 * 1024;
const GENERATED_OUTPUT_FORMAT = 'generated-v1';
function captureRawOutput(state, event) {
  if (!state || !event) return;
  // Advance the resume cursor for ALL events, including ones not rendered here.
  state.lastEventSequence = Math.max(Number(state.lastEventSequence) || 0, Number(event.sequence) || 0);

  // Older browser transcripts stored diagnostic JSON in rawOutput. Drop those
  // cached diagnostics but keep already generated assistant text and the cursor.
  if (state.rawOutputFormat !== GENERATED_OUTPUT_FORMAT) {
    state.rawOutput = String(state.content || '').slice(-RAW_OUTPUT_LIMIT);
    state.rawOutputFormat = GENERATED_OUTPUT_FORMAT;
  }

  const type = String(event.type || '');
  if (type !== 'model.text_delta' && type !== 'model.reasoning_delta' && type !== 'model.tool_argument_delta') return;
  const delta = event.data?.delta;
  if (typeof delta !== 'string' || !delta.length) return;
  const next = String(state.rawOutput || '') + delta;
  state.rawOutput = next.length > RAW_OUTPUT_LIMIT ? next.slice(-RAW_OUTPUT_LIMIT) : next;
}

function formatCompletionTime(timestamp) {
  const value = Number(timestamp) || Date.now();
  return new Date(value).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function formatDurationMs(durationMs) {
  const value = Number(durationMs);
  if (!Number.isFinite(value) || value <= 0) return 'N/A';
  if (value < 1000) return `${Math.round(value)}ms`;
  if (value < 60000) return `${(value / 1000).toFixed(value < 10000 ? 2 : 1)}s`;
  const minutes = Math.floor(value / 60000);
  const seconds = Math.round((value % 60000) / 1000);
  return `${minutes}m ${seconds}s`;
}

function formatTimestampFull(value) {
  const timestamp = Number(value);
  if (!Number.isFinite(timestamp) || timestamp <= 0) return '';
  return new Date(timestamp).toLocaleString();
}

function estimateTokenCount(text) {
  const source = String(text || '').trim();
  if (!source) return 0;
  return Math.max(1, Math.round(source.length / 4));
}

function estimateTps(text, durationMs) {
  const duration = Number(durationMs);
  if (!Number.isFinite(duration) || duration <= 0) return 'N/A';
  return (estimateTokenCount(text) / (duration / 1000)).toFixed(1);
}

function displayCompletionReason(message) {
  const reason = String(message?.completionReason || '');
  if (!reason) return 'N/A';
  if (reason === 'engcoder-task') return 'EngCoder completed';
  if (reason === 'engcoder-cancelled') return 'Cancelled';
  if (reason === 'engcoder-approval') return 'Approval required';
  if (reason === 'engcoder-error') return 'Failed';
  return reason;
}

function engcoderBase() {
  return normalizeBaseUrl(window.location.origin);
}

function makeRuntimeSessionId(chat) {
  if (!chat.runtimeSessionId) {
    chat.runtimeSessionId = `web-${Number(chat.id).toString(36)}-${Date.now().toString(36)}`;
  }
  return chat.runtimeSessionId;
}

function rotateRuntimeSession(chat) {
  chat.runtimeSessionId = `web-${Number(chat.id).toString(36)}-${Date.now().toString(36)}`;
  return chat.runtimeSessionId;
}

function attachmentTaskText(text, attachments) {
  const blocks = [];
  if (String(text || '').trim()) blocks.push(String(text).trim());

  for (const attachment of attachments || []) {
    if (attachment?.kind !== 'text') continue;
    blocks.push([
      `[Attached text file: ${attachment.name || 'attachment'}]`,
      String(attachment.text || ''),
      `[End attached file: ${attachment.name || 'attachment'}]`
    ].join('\n'));
  }

  return blocks.join('\n\n').trim();
}

function transcriptTaskText(chat) {
  const lines = [
    'Continue this conversation from the transcript below. Preserve the roles and answer the final USER request.',
    ''
  ];

  for (const message of chat.messages || []) {
    if (!message || (message.role !== 'user' && message.role !== 'assistant')) continue;
    let text = String(message.content || '');
    if (message.role === 'user' && Array.isArray(message.attachments)) {
      text = attachmentTaskText(text, message.attachments);
    }
    lines.push(`${message.role.toUpperCase()}:`);
    lines.push(text);
    lines.push('');
  }

  return lines.join('\n').trim();
}

function appendActivity(state, text) {
  if (!state || !text) return;
  const line = String(text).replace(/\s+/g, ' ').trim();
  if (!line) return;
  const next = state.thinking ? `${state.thinking}\n${line}` : line;
  state.thinking = next.slice(-24000);
  state.hasThinking = true;
  state.thinkingLabel = 'Activity';
}


function freshExecutionStats() {
  return {
    architect: { model: '', configuredModel: '', modelAttempted: false, modelUsed: false, source: '', addon: '', confidence: null, retrieve: false, reason: '', modelFallbackReason: '', durationMs: 0 },
    keymaker: { model: '', requestedModel: '', resolvedModel: '', turns: 0, durationMs: 0 },
    modelRoles: { foundation: '', small: '', middle: '', draftUsed: '', draftStatus: '', middleUsed: '', middleStatus: '', finalRole: '', finalModel: '' },
    addons: { calls: 0, durationMs: 0, names: [], models: {} },
    tools: { requested: 0, completed: 0, failed: 0, denied: 0, approval: 0, durationMs: 0, names: [] },
    memoryItems: 0,
    runtimeEvents: 0,
    firstRuntimeEventMs: 0,
    lastRuntimeEventMs: 0
  };
}

function ensureExecutionStats(state) {
  if (!state.executionStats || typeof state.executionStats !== 'object') state.executionStats = freshExecutionStats();
  if (!state.executionStats.modelRoles) state.executionStats.modelRoles = freshExecutionStats().modelRoles;
  return state.executionStats;
}

function pushUnique(list, value) {
  const text = String(value || '').trim();
  if (text && !list.includes(text)) list.push(text);
}

function recordExecutionEvent(state, event, activityLine = '') {
  if (!state || !event) return;
  const stats = ensureExecutionStats(state);
  const data = event.data || {};
  const roles = stats.modelRoles;
  const eventMs = Number(event.timestamp_ms) || Date.now();
  stats.runtimeEvents += 1;
  if (!stats.firstRuntimeEventMs) stats.firstRuntimeEventMs = eventMs;
  stats.lastRuntimeEventMs = eventMs;

  switch (event.type) {
    case 'controller.model_selected':
      state.respondingRole = data.role === 'small-response' ? 'small-response' : 'keymaker-foundation';
      roles.finalRole = state.respondingRole;
      if (data.model) roles.finalModel = String(data.model);
      break;
    case 'controller.decision':
      stats.architect.source = String(data.source || '');
      stats.architect.configuredModel = String(data.model || stats.architect.configuredModel || '');
      stats.architect.modelAttempted = data.model_attempted === true;
      stats.architect.modelUsed = data.model_used === true || stats.architect.source === 'architect-model';
      stats.architect.model = stats.architect.modelUsed ? stats.architect.configuredModel : '';
      stats.architect.addon = String(data.addon || '');
      stats.architect.confidence = Number.isFinite(Number(data.confidence)) ? Number(data.confidence) : null;
      stats.architect.retrieve = data.retrieve === true;
      stats.architect.reason = String(data.reason || '');
      stats.architect.modelFallbackReason = String(data.model_fallback_reason || '');
      stats.architect.durationMs += Math.max(0, Number(data.duration_ms) || 0);
      break;
    case 'residency.prepared': {
      const physical = data.physical && typeof data.physical === 'object' ? data.physical.model : data.physical;
      const assigned = String(data.assigned_model || physical || data.model || '').trim();
      if (assigned && assigned !== 'auto' && state.respondingRole !== 'small-response') {
        stats.keymaker.resolvedModel = assigned;
        stats.keymaker.model = assigned;
      }
      if (assigned && assigned !== 'auto') roles.finalModel = assigned;
      break;
    }
    case 'model.request':
      state.respondingRole = data.role === 'small-response' ? 'small-response' : 'keymaker-foundation';
      roles.finalRole = state.respondingRole;
      if (state.respondingRole !== 'small-response') {
        stats.keymaker.requestedModel = String(data.model || stats.keymaker.requestedModel || '');
        stats.keymaker.turns = Math.max(stats.keymaker.turns, Number(data.turn || 0) + 1);
      }
      if (data.model) state.respondingModel = String(data.model);
      if (data.model) roles.finalModel = String(data.model);
      if (state.respondingRole !== 'small-response' && !stats.keymaker.model && stats.keymaker.requestedModel !== 'auto') stats.keymaker.model = stats.keymaker.requestedModel;
      break;
    case 'model.response': {
      const resolved = String(data.model || '').trim();
      if (resolved) {
        state.respondingModel = resolved;
        roles.finalModel = resolved;
        if (state.respondingRole !== 'small-response') {
          stats.keymaker.resolvedModel = resolved;
          stats.keymaker.model = resolved;
        }
      }
      stats.keymaker.durationMs += Math.max(0, Number(data.duration_ms) || 0);
      break;
    }
    case 'handoff.draft_requested':
      roles.draftStatus = 'Preparing';
      break;
    case 'handoff.draft_completed':
      if (data.model) roles.draftUsed = String(data.model);
      roles.draftStatus = 'Completed';
      break;
    case 'handoff.draft_failed':
      roles.draftStatus = 'Failed · KeyMaker continues';
      break;
    case 'review.requested':
      roles.middleStatus = 'Reviewing';
      break;
    case 'review.completed':
      if (data.model) roles.middleUsed = String(data.model);
      roles.middleStatus = 'Completed';
      break;
    case 'review.failed':
      roles.middleStatus = 'Failed · KeyMaker continues';
      break;
    case 'addon.requested':
      stats.addons.calls += 1;
      pushUnique(stats.addons.names, data.addon);
      if (!stats.addons.models || typeof stats.addons.models !== 'object') stats.addons.models = {};
      if (data.addon && data.model) stats.addons.models[String(data.addon)] = String(data.model);
      break;
    case 'addon.activated':
      stats.addons.calls += 1;
      pushUnique(stats.addons.names, data.addon);
      if (!stats.addons.models || typeof stats.addons.models !== 'object') stats.addons.models = {};
      if (data.addon && data.model) stats.addons.models[String(data.addon)] = String(data.model);
      break;
    case 'addon.completed':
    case 'addon.failed':
      if (event.type === 'addon.completed' && data.mode === 'small-checklist-only' && data.model) {
        roles.draftUsed = String(data.model);
        roles.draftStatus = 'Completed as specialist';
      }
      stats.addons.durationMs += Math.max(0, Number(data.duration_ms) || 0);
      pushUnique(stats.addons.names, data.addon);
      if (!stats.addons.models || typeof stats.addons.models !== 'object') stats.addons.models = {};
      if (data.addon && data.model) stats.addons.models[String(data.addon)] = String(data.model);
      break;
    case 'memory.retrieved':
      stats.memoryItems += Math.max(0, Number(data.count) || 0);
      break;
    case 'tool.requested':
      stats.tools.requested += 1;
      pushUnique(stats.tools.names, data.name);
      break;
    case 'tool.completed':
      stats.tools.completed += 1;
      stats.tools.durationMs += Math.max(0, Number(data.duration_ms) || 0);
      pushUnique(stats.tools.names, data.name);
      break;
    case 'tool.review_failed':
      stats.tools.failed += 1;
      pushUnique(stats.tools.names, data.name);
      break;
    case 'tool.failed':
      stats.tools.failed += 1;
      stats.tools.durationMs += Math.max(0, Number(data.duration_ms) || 0);
      pushUnique(stats.tools.names, data.name);
      break;
    case 'tool.denied':
      stats.tools.denied += 1;
      pushUnique(stats.tools.names, data.name);
      break;
    case 'tool.approval_required':
      stats.tools.approval += 1;
      pushUnique(stats.tools.names, data.name);
      break;
    case 'tool.approved':
      pushUnique(stats.tools.names, data.name);
      break;
  }

  if (!Array.isArray(state.activityEvents)) state.activityEvents = [];
  if (activityLine) {
    state.activityEvents.push({
      sequence: Number(event.sequence) || 0,
      timestampMs: eventMs,
      type: String(event.type || ''),
      text: String(activityLine)
    });
    if (state.activityEvents.length > 300) state.activityEvents.splice(0, state.activityEvents.length - 300);
  }
}

async function deleteRuntimeSession(sessionId) {
  const id = String(sessionId || '').trim();
  if (!id) return;
  try {
    await fetch(`${engcoderBase()}/api/sessions/${encodeURIComponent(id)}`, { method: 'DELETE' });
  } catch {}
}

function conciseRuntimeError(value) {
  const text = String(value || '').replace(/\r\n?/g, '\n').trim();
  if (/\bHTTP 401\b|authentication/i.test(text)) return 'LM Studio authentication required';
  const first = text.split('\n').map(line => line.trim()).find(Boolean) || 'unknown error';
  return first.length > 220 ? `${first.slice(0, 217)}…` : first;
}

function eventActivityLine(event) {
  const data = event?.data || {};
  const detailed = $('showActivityDetails')?.checked !== false;
  switch (event?.type) {
    case 'task.created': return 'Accepted request';
    case 'controller.decision': {
      const capability = data.addon ? `${String(data.addon).toUpperCase()} addon` : 'KeyMaker Foundation';
      if (data.source === 'architect-model') return `Route · ${capability} · Architect ${data.model || 'model'}`;
      if (data.model_attempted === true) return `Route · ${capability} · deterministic fallback after Architect ${data.model || 'model'}`;
      if (data.source === 'session-forced') return `Route · ${capability} · session forced`;
      if (data.source === 'session-core-only') return `Route · ${capability} · automatic routing off`;
      return `Route · ${capability} · deterministic`;
    }
    case 'addon.activated': return `Assigned addon · ${data.addon || 'capability'} · ${data.model || 'model'}`;
    case 'addon.requested': return `Consulting ${data.addon || 'capability'} addon${data.model ? ` · ${data.model}` : ''}`;
    case 'addon.completed': return `${data.addon || 'Capability'} addon completed${data.model ? ` · ${data.model}` : ''}`;
    case 'addon.failed': return `${data.addon || 'Capability'} addon failed · ${conciseRuntimeError(data.error)}`;
    case 'memory.retrieved': return `Recalled ${Number(data.count || 0)} verified memor${Number(data.count || 0) === 1 ? 'y' : 'ies'}`;
    case 'residency.prepared': {
      if (!detailed) return '';
      const physical = data.physical && typeof data.physical === 'object' ? data.physical.model : data.physical;
      return `Assigned KeyMaker · ${data.assigned_model || physical || data.model || 'model'}`;
    }
    case 'residency.failed': return `Residency failed · ${conciseRuntimeError(data.error)}`;
    case 'context.policy': return detailed ? `Context · ${Number(data.context_tokens || 0).toLocaleString()} tokens · compact ${data.auto_compact ? `${Math.round(Number(data.trigger_ratio || .90) * 100)}%→${Math.round(Number(data.target_ratio || .70) * 100)}%` : 'off'}` : '';
    case 'context.compacted': return `Context compacted · ${Number(data.estimated_tokens_before || 0).toLocaleString()} → ${Number(data.estimated_tokens_after || 0).toLocaleString()} tokens`;
    case 'expert.route': return detailed ? `Legacy route · ${data.expert || 'KeyMaker Foundation'}` : '';
    case 'expert.requested': return detailed ? `Legacy specialist · ${data.expert || 'unknown'}` : '';
    case 'expert.completed': return detailed ? `Legacy specialist completed · ${data.expert || 'unknown'}` : '';
    case 'model.request': return detailed ? `${data.role === 'small-response' ? 'Small responder' : 'KeyMaker'} turn ${Number(data.turn || 0) + 1} · ${data.model || 'model'}` : '';
    case 'model.tool_call_started': return detailed ? `Prepared tool call · ${data.name || 'unknown'}` : '';
    case 'tool.requested': return `Tool · ${data.name || 'unknown'}`;
    case 'tool.reviewed': return detailed ? `Policy · ${data.name || 'tool'} ${data.disposition || 'reviewed'}` : '';
    case 'tool.completed': return `Completed tool · ${data.name || 'unknown'}`;
    case 'tool.denied': return `Blocked tool · ${data.name || 'unknown'} · ${conciseRuntimeError(data.error || 'policy')}`;
    case 'tool.approval_required': return `Approval required · ${data.name || 'tool'}`;
    case 'tool.approved': return `Approved tool · ${data.name || 'tool'}`;
    case 'tool.review_failed': return `Tool review failed · ${data.name || 'unknown'}`;
    case 'tool.failed': return `Tool failed · ${data.name || 'unknown'} · ${conciseRuntimeError(data.error)}`;
    case 'agent.stall_detected': return 'Changed strategy after repeated action';
    case 'iteration.started': return 'Candidate self-iteration started';
    case 'iteration.check_started': return `${data.stage === 'baseline' ? 'Baseline' : 'Candidate'} ${data.check} · running`;
    case 'iteration.check_finished': return `${data.stage === 'baseline' ? 'Baseline' : 'Candidate'} ${data.check} · ${data.exit_code === 0 ? 'passed' : 'failed'}`;
    case 'iteration.repair': return `Candidate repair ${data.attempt} of 2`;
    case 'iteration.escalated': return `Escalated · ${data.from} → ${data.to} · ${data.reason}`;
    case 'iteration.escalation_unavailable': return `Escalation unavailable · ${data.model} · keeping current model`;
    case 'iteration.lesson': return data.stored === true
      ? `Saved verified memory · ${data.lesson} · No weight update`
      : `Verified lesson · ${data.lesson} · No new memory or weight update`;
    case 'iteration.lesson_reused': return `Recalled C++ boundary memory · ${data.lesson}`;
    case 'verification.completed': return `Verified · ${data.status || 'completed'}`;
    case 'task.completed': return 'Completed';
    case 'task.failed': return `Failed · ${conciseRuntimeError(data.error)}`;
    case 'task.cancelled': return 'Stopped';
    case 'task.awaiting_approval': return `Waiting for approval${data.tool ? ` · ${data.tool}` : ''}`;
    default: return '';
  }
}

function terminalFromEvent(event) {
  if (!event) return null;
  if (event.type === 'task.completed') return { kind: 'completed', result: String(event.data?.result || '') };
  if (event.type === 'task.failed') return { kind: 'failed', error: String(event.data?.error || 'EngCoder task failed') };
  if (event.type === 'task.cancelled') return { kind: 'cancelled' };
  return null;
}

async function resolveTaskApproval(state, approved, container) {
  const approval = state?.approvalRequest;
  if (!approval || approval.pending) return;

  const taskId = String(approval.taskId || '').trim();
  const callId = String(approval.callId || '').trim();
  if (!taskId || !callId) {
    approval.error = 'Approval request is missing its task or call identifier.';
    updateAssistantMessage(container, state);
    return;
  }

  approval.pending = true;
  approval.error = '';
  updateAssistantMessage(container, state);

  try {
    const response = await fetch(`${engcoderBase()}/api/tasks/${encodeURIComponent(taskId)}/approval`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ call_id: callId, approved: approved === true }),
      signal: controller?.signal
    });
    const payload = await response.json().catch(() => null);
    if (!response.ok) throw new Error(payload?.error || `Approval request failed: HTTP ${response.status}`);

    approval.pending = false;
    approval.submitted = true;
    approval.decision = approved ? 'approved' : 'denied';
    setStatus(approved ? 'Resuming' : 'Continuing', 'busy');
    updateAssistantMessage(container, state);
  } catch (error) {
    if (error?.name === 'AbortError') return;
    approval.pending = false;
    approval.error = String(error?.message || error);
    updateAssistantMessage(container, state);
  }
}

async function submitEngCoderTask(sessionId, message, retryModel = '', retryRole = '', candidateRun = false) {
  const addonsEnabled = $('enableAddons')?.checked !== false;
  const addonModels = {};
  const mappings = [
    ['cpp', 'addonCppModel'],
    ['docs', 'addonDocsModel'],
    ['planner', 'addonPlannerModel'],
    ['tools', 'addonToolsModel'],
    ['reranker', 'addonRerankerModel']
  ];
  if (addonsEnabled) {
    for (const [addon, control] of mappings) {
      const value = $(control)?.value || '';
      if (value) addonModels[addon] = value;
    }
  }
  const requestPayload = {
    project_id: 'web',
    session_id: sessionId,
    message,
    self_iteration: candidateRun,
    foundation_model: $('foundationModel')?.value || '',
    simple_model: $('simpleResponseModel')?.value || '',
    retry_model: retryModel,
    review_model: $('reviewModel')?.value || '',
    review_mode: $('reviewMode')?.value || 'auto',
    thinking_mode: $('thinkingMode')?.value || 'auto',
    controller_model: $('controllerModel')?.value || '',
    forced_addon: addonsEnabled ? ($('forcedAddon')?.value || '') : '',
    // Small-response routing must remain available when capability addons are disabled.
    auto_route: $('autoRouting')?.checked !== false,
    addons_enabled: addonsEnabled,
    addon_models: addonModels,
    session_instructions: String($('system')?.value || '').trim(),
    temperature: Math.max(0, Math.min(2, Number($('temperature')?.value || 0.15))),
    max_tokens: Number.isFinite(Number($('max_tokens')?.value)) ? Math.trunc(Number($('max_tokens')?.value)) : -1,
    include_history: $('includeHistory')?.checked !== false,
    max_history_turns: Math.max(0, Math.trunc(Number($('maxHistoryTurns')?.value || 0)))
  };
  // The original answer may have used the lightweight external responder or
  // the foundation, regardless of what the current session selector displays.
  if (retryModel) {
    // Role is captured from the original model.request event, not guessed from
    // URI syntax. An external LM Studio model can itself be the foundation.
    if (retryRole === 'small-response') {
      requestPayload.simple_model = retryModel;
      if (requestPayload.foundation_model === retryModel) requestPayload.foundation_model = '';
    } else {
      requestPayload.foundation_model = retryModel;
      if (requestPayload.simple_model === retryModel) requestPayload.simple_model = '';
    }
  }
  // Retry does not consult the middle tier. Avoid submitting a stale reviewer
  // that now points at the pinned responder, which native validation rejects.
  if (requestPayload.review_model &&
      (requestPayload.review_model === requestPayload.simple_model ||
       requestPayload.review_model === requestPayload.foundation_model)) {
    requestPayload.review_model = '';
  }
  const response = await fetch(`${engcoderBase()}/api/tasks`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(requestPayload),
    signal: controller?.signal
  });
  const payload = await response.json().catch(() => null);
  if (!response.ok) throw new Error(payload?.error || `EngCoder task submission failed: HTTP ${response.status}`);
  if (!payload?.task_id || !payload?.session_id) throw new Error('EngCoder task submission returned an invalid response.');
  const candidateControl = $('candidateIteration');
  if (candidateRun && candidateControl) candidateControl.checked = false;
  return payload;
}

async function pollEngCoderTask(submission, assistantBody, assistantState, timing, persistProgress = () => {}) {
  let after = Math.max(0, Number(assistantState.lastEventSequence) || 0);
  let pollFailures = 0;
  let pollDelayMs = 100;

  while (!controller?.signal.aborted) {
    let sawTaskEvent = false;
    try {
      const response = await fetch(
        `${engcoderBase()}/api/sessions/${encodeURIComponent(submission.session_id)}/events?after=${after}`,
        { cache: 'no-store', signal: controller.signal }
      );
      const payload = await response.json().catch(() => null);
      if (!response.ok) throw new Error(payload?.error || `Event polling failed: HTTP ${response.status}`);
      pollFailures = 0;

      let renderPending = false;
      let progressPending = false;
      let processedEvents = 0;
      for (const event of payload?.events || []) {
        after = Math.max(after, Number(event?.sequence) || 0);
        if (event?.task_id !== submission.task_id) continue;
        sawTaskEvent = true;
        if (event.type === 'iteration.started') assistantState.candidateIteration = true;

        const activity = eventActivityLine(event);
        recordExecutionEvent(assistantState, event, activity);
        captureRawOutput(assistantState, event);
        if (activity) appendActivity(assistantState, activity);
        if (event.type === 'model.reasoning_delta') {
          // Provider-exposed reasoning is not synthesized or extracted from private internals.
          assistantState.reasoningOutput = (String(assistantState.reasoningOutput || '') + String(event.data?.delta || '')).slice(-RAW_OUTPUT_LIMIT);
        }

        if (event.type === 'controller.decision') {
          assistantState.selectedAddon = event.data?.addon || '';
        }

        let shouldRender = true;
        if (event.type === 'model.text_delta') {
          const delta = String(event.data?.delta || '');
          if (delta) {
            if (!timing.firstContentAt) {
              timing.firstContentAt = Date.now();
              assistantState.firstOutputAtMs = timing.firstContentAt;
            }
            assistantState.content += delta;
            if ($('stream')?.checked !== false) setStatus('Streaming', 'busy');
            shouldRender = $('stream')?.checked !== false;
          }
        } else if (event.type === 'addon.requested' || event.type === 'addon.activated' || event.type === 'expert.requested') {
          setStatus(`Addon: ${event.data?.addon || event.data?.expert || 'capability'}`, 'busy');
        } else if (event.type === 'tool.requested') {
          setStatus(`Tool: ${event.data?.name || 'request'}`, 'busy');
        } else if (event.type === 'tool.approval_required') {
          // The runtime registers the call after this policy event.
          setStatus('Preparing approval', 'busy');
        } else if (event.type === 'task.awaiting_approval') {
          const priorApproval = assistantState.approvalRequest || {};
          assistantState.approvalRequest = {
            taskId: String(event.task_id || submission.task_id || priorApproval.taskId || ''),
            callId: String(event.data?.call_id || priorApproval.callId || ''),
            tool: String(event.data?.tool || priorApproval.tool || 'tool'),
            arguments: event.data?.arguments && typeof event.data.arguments === 'object'
              ? event.data.arguments
              : (priorApproval.arguments || {}),
            reason: String(event.data?.reason || priorApproval.reason || ''),
            reviewer: String(event.data?.reviewer || priorApproval.reviewer || ''),
            pending: false,
            submitted: false,
            error: ''
          };
          showError('');
          setStatus('Approval required', 'busy');
        } else if (event.type === 'tool.approved') {
          if (!assistantState.approvalRequest || assistantState.approvalRequest.callId === String(event.data?.id || '')) {
            assistantState.approvalRequest = null;
          }
          setStatus('Running', 'busy');
        } else if (event.type === 'tool.denied') {
          if (assistantState.approvalRequest?.callId === String(event.data?.id || '')) {
            assistantState.approvalRequest = null;
          }
          setStatus('Thinking', 'busy');
        } else if (event.type === 'model.request') {
          setStatus('Thinking', 'busy');
        } else if (event.type === 'iteration.check_started') {
          setStatus(event.data?.check === 'tests' ? 'Testing candidate' : 'Building candidate', 'busy');
        } else if (event.type === 'iteration.repair') {
          setStatus(`Preparing repair ${event.data?.attempt || 1} of 2`, 'busy');
        } else if (event.type === 'iteration.escalated') {
          setStatus(`Repair escalated to ${event.data?.to || 'configured model'}`, 'busy');
        }

        const terminal = terminalFromEvent(event);
        if (terminal) shouldRender = true;
        renderPending ||= shouldRender;
        progressPending = true;
        if (terminal) {
          // Flush the completed content before returning, even on a very
          // busy backend with many text fragments in one response.
          updateAssistantMessage(assistantBody, assistantState);
          persistProgress();
          return terminal;
        }
        // A large event backlog must not monopolize the JS event loop and
        // prevent Qwen Live, sidebar clicks or Stop from being painted.
        if (++processedEvents % 128 === 0) {
          if (renderPending) updateAssistantMessage(assistantBody, assistantState);
          if (progressPending) persistProgress();
          renderPending = false;
          progressPending = false;
          await new Promise(resolve => setTimeout(resolve, 0));
        }
      }
      if (renderPending) updateAssistantMessage(assistantBody, assistantState);
      if (progressPending) persistProgress();
    } catch (error) {
      if (error?.name === 'AbortError') throw error;
      pollFailures += 1;
      if (pollFailures >= 40) throw error;
    }

    pollDelayMs = sawTaskEvent ? 100 : Math.min(400, pollDelayMs + 50);

    await new Promise((resolve, reject) => {
      const signal = controller?.signal;
      if (!signal || signal.aborted) {
        reject(new DOMException('Aborted', 'AbortError'));
        return;
      }

      const onAbort = () => {
        clearTimeout(timer);
        reject(new DOMException('Aborted', 'AbortError'));
      };
      const timer = setTimeout(() => {
        signal.removeEventListener('abort', onAbort);
        resolve();
      }, pollDelayMs);
      signal.addEventListener('abort', onAbort, { once: true });
    });
  }

  throw new DOMException('Aborted', 'AbortError');
}

function finalizeAssistantState(state, timing, reason, modelName) {
  state.approvalRequest = null;
  const completedAt = Date.now();
  const totalMs = Math.max(0, completedAt - timing.startedAt);
  const genMs = timing.firstContentAt ? Math.max(0, completedAt - timing.firstContentAt) : 0;
  const thinkMs = Math.max(0, totalMs - genMs);

  state.thinkingLive = false;
  state.thinkingOpen = false;
  state.thinkingLabel = 'Activity';
  state.completionReason = reason;
  state.streamEndedBy = 'engcoder-event';
  state.completionAt = formatCompletionTime(completedAt);
  state.completionTimestamp = completedAt;
  state.timestampFull = formatTimestampFull(completedAt);
  const stats = ensureExecutionStats(state);
  const assignedModel = String(stats.keymaker?.resolvedModel || stats.keymaker?.model || stats.keymaker?.requestedModel || '').trim();
  const respondingModel = String(state.respondingModel || '').trim();
  state.completionModel = respondingModel || ((assignedModel && assignedModel !== 'auto') ? assignedModel : (modelName || 'KeyMaker Foundation'));
  state.completionDurationMs = totalMs;
  state.thinkDurationMs = thinkMs;
  state.genDurationMs = genMs;
  state.completionState = reason === 'engcoder-task' ? 'Idle' : reason === 'engcoder-cancelled' ? 'Stopped' : 'Error';
  stats.outputTokensEstimate = estimateTokenCount(state.content || '');
  stats.timeToFirstTokenMs = timing.firstContentAt ? Math.max(0, timing.firstContentAt - timing.startedAt) : 0;
  state.showRegenerate = true;
}

function assistantMessageSnapshot(state, sourcePrompt, fallbackContent = '') {
  return {
    role: 'assistant',
    content: cleanAssistantContent(state.content || fallbackContent),
    thinking: state.thinking || '',
    thinkingOpen: false,
    thinkingLabel: 'Activity',
    hasThinking: !!state.thinking,
    completionReason: state.completionReason || '',
    streamEndedBy: state.streamEndedBy || '',
    completionAt: state.completionAt || '',
    completionTimestamp: Number(state.completionTimestamp) || 0,
    timestampFull: state.timestampFull || '',
    completionModel: state.completionModel || '',
    respondingModel: state.respondingModel || '',
    respondingRole: state.respondingRole || '',
    completionDurationMs: Number(state.completionDurationMs) || 0,
    thinkDurationMs: Number(state.thinkDurationMs) || 0,
    genDurationMs: Number(state.genDurationMs) || 0,
    taskStartedAtMs: Number(state.taskStartedAtMs) || 0,
    firstOutputAtMs: Number(state.firstOutputAtMs) || 0,
    completionState: state.completionState || 'Idle',
    sourcePrompt: String(sourcePrompt || ''),
    selectedAddon: state.selectedAddon || '',
    verifiedForLearning: state.verifiedForLearning === true,
    activityThinkingOpen: false,
    rawOutput: String(state.rawOutput || '').slice(-RAW_OUTPUT_LIMIT),
    rawOutputFormat: GENERATED_OUTPUT_FORMAT,
    reasoningOutput: String(state.reasoningOutput || '').slice(-RAW_OUTPUT_LIMIT),
    runtimeTaskId: state.runtimeTaskId || '',
    runtimeSessionId: state.runtimeSessionId || '',
    candidateIteration: state.candidateIteration === true,
    approvalRequest: state.approvalRequest && typeof state.approvalRequest === 'object'
      ? JSON.parse(JSON.stringify(state.approvalRequest)) : null,
    lastEventSequence: Number(state.lastEventSequence) || 0,
    executionStats: state.executionStats,
    activityEvents: state.activityEvents,
    showRegenerate: true
  };
}

// Attach to an already-running native task after a page refresh; do not submit
// another task or replay its request. Native events were persisted during runtime.
async function resumeSavedEngCoderTask() {
  if (controller) return;
  const chat = currentChat();
  const last = chat?.messages?.at(-1);
  if (!last || last.role !== 'assistant' || !last.runtimeTaskId || last.completionReason) return;
  const container = [...chatEl.querySelectorAll('.msg.assistant .bubble-body')].at(-1);
  if (!container) return;
  const state = container.closest('.msg')?.__assistantState;
  if (!state) return;
  Object.assign(state, last, { thinkingLive: true });
  state.runtimeSessionId = last.runtimeSessionId || chat.runtimeSessionId;
  updateAssistantMessage(container, state);
  controller = new AbortController();
  activeEngCoderTaskId = last.runtimeTaskId;
  activeEngCoderSessionId = chat.runtimeSessionId;
  setBusy(true);
  setStatus('Resuming task', 'busy');
  const persist = () => { Object.assign(last, assistantMessageSnapshot(state, state.sourcePrompt)); scheduleChatSave(); };
  // Recover the original clock anchors instead of resetting timing on refresh.
  // Legacy chat snapshots lack these fields, so retain the earlier fallback.
  const timing = {
    startedAt: Number(last.taskStartedAtMs) || Date.now() - (Number(last.completionDurationMs) || 0),
    firstContentAt: Number(last.firstOutputAtMs) || (last.content ? Date.now() : 0)
  };
  try {
    const result = await pollEngCoderTask(
      { task_id: last.runtimeTaskId, session_id: chat.runtimeSessionId }, container, state, timing, persist);
    if (result.kind === 'completed' && !state.content && result.result) state.content = cleanAssistantContent(result.result);
    finalizeAssistantState(state, timing,
      result.kind === 'completed' ? 'engcoder-task' : result.kind === 'cancelled' ? 'engcoder-cancelled' : 'engcoder-error',
      state.completionModel || 'KeyMaker');
    if (result.kind === 'failed') appendActivity(state, `Task failed · ${conciseRuntimeError(result.error)}`);
  } catch (error) {
    if (error?.name !== 'AbortError') {
      // Leave the task resumable if the server is unreachable; never duplicate it.
      appendActivity(state, `Reconnect pending · ${conciseRuntimeError(error?.message || error)}`);
    }
  } finally {
    updateAssistantMessage(container, state);
    persist();
    saveChats();
    activeEngCoderTaskId = null;
    activeEngCoderSessionId = null;
    controller = null;
    setBusy(false);
  }
}

async function sendMessage({ regenerate = false, targetIndex = -1 } = {}) {
  if (controller) return;
  const chat = currentChat();
  if (!chat) return;

  const wasEditing = editingMessageIndex >= 0 && editingMessageIndex < chat.messages.length;
  const candidateRun = !regenerate && $('candidateIteration')?.checked === true;
  const text = String(promptEl.value || '').trim();
  const attachments = pendingAttachments.map(attachment => ({ ...attachment }));

  if (!regenerate && !text && !attachments.length) return;
  if (attachments.some(attachment => attachment.kind === 'image')) {
    showError('Image attachments are not wired into the EngCoder vision transport yet. Use text/document attachments until the vision addon is added.');
    return;
  }

  showError('');
  stopRequested = false;

  let retryModel = '';
  let retryRole = '';
  if (regenerate) {
    const selectedIndex = Number.isInteger(targetIndex) && targetIndex >= 0 ? targetIndex :
      chat.messages.findLastIndex(message => message?.role === 'assistant');
    if (selectedIndex < 0 || chat.messages[selectedIndex]?.role !== 'assistant') return;
    const prior = chat.messages[selectedIndex];
    if (isCandidateReply(prior)) {
      showError('Start a fresh candidate run from EngCoder Lab to review and approve new edits.');
      return;
    }
    if (selectedIndex < chat.messages.length - 1) {
      if (!window.confirm('Retry this response? Messages after it will be discarded.')) return;
    }
    retryModel = String(prior.respondingModel || prior.completionModel || '').trim();
    retryRole = prior.respondingRole === 'small-response' ? 'small-response' : 'keymaker-foundation';
    if (retryModel === 'KeyMaker Foundation' || retryModel === 'KeyMaker' || retryModel === 'auto') retryModel = '';
    chat.messages.splice(selectedIndex);
    renderActiveChat();
  } else {
    if (wasEditing) chat.messages.splice(editingMessageIndex);

    const userMessage = { role: 'user', content: text };
    if (attachments.length) userMessage.attachments = attachments;
    chat.messages.push(userMessage);
    lastUser = text;

    editingMessageIndex = -1;
    pendingAttachments = [];
    renderPendingAttachments();
    promptEl.value = '';
    autoresize();

    if (/^Conversation\b/.test(chat.name)) {
      chat.name = makeChatName(text || attachments[0]?.name || 'attachment');
      renderChatList();
    }
    renderActiveChat();
  }
  saveChats(); // persist the request before any network operation or model delay

  const latestUserMessage = [...chat.messages].reverse().find(message => message?.role === 'user');
  if (!latestUserMessage) return;

  let taskText;
  let runtimeSessionId;
  if (regenerate || wasEditing) {
    runtimeSessionId = rotateRuntimeSession(chat);
    taskText = transcriptTaskText(chat);
  } else {
    runtimeSessionId = makeRuntimeSessionId(chat);
    taskText = attachmentTaskText(latestUserMessage.content, latestUserMessage.attachments || []);
  }

  if (!taskText) {
    showError('The request is empty after preparing attachments.');
    return;
  }

  const assistantBody = addTextMessage('assistant', '', {
    content: '',
    thinking: 'Preparing request',
    thinkingOpen: true,
    thinkingLive: true,
    taskStartedAtMs: Date.now(),
    thinkingLabel: 'Working',
    completionState: 'Running',
    candidateIteration: candidateRun,
    sourcePrompt: String(latestUserMessage.content || ''),
    respondingModel: retryModel,
    respondingRole: retryRole,
    selectedAddon: '',
    verifiedForLearning: false,
    executionStats: freshExecutionStats(),
    activityEvents: [],
    showRegenerate: false
  });
  const assistantMessage = assistantBody.closest('.msg');
  const assistantState = assistantMessage?.__assistantState;
  if (!assistantState) throw new Error('EngCoder could not initialize the assistant view.');
  const runRoles = ensureExecutionStats(assistantState).modelRoles;
  runRoles.foundation = String($('foundationModel')?.value || '');
  runRoles.small = String($('simpleResponseModel')?.value || '');
  runRoles.middle = String($('reviewModel')?.value || '');
  // Install the message before inference. Every update replaces the same snapshot,
  // making refresh recovery possible even if the tab disappears during streaming.
  const storedMessage = assistantMessageSnapshot(assistantState, latestUserMessage.content || '');
  chat.messages.push(storedMessage);
  saveChats();
  const storeAssistant = (fallbackContent = '') => {
    Object.assign(storedMessage, assistantMessageSnapshot(assistantState, latestUserMessage.content || '', fallbackContent));
    scheduleChatSave();
  };

  const timing = { startedAt: assistantState.taskStartedAtMs, firstContentAt: 0 };
  controller = new AbortController();
  setBusy(true);
  setStatus('Sending', 'busy');

  try {
    const submission = await submitEngCoderTask(runtimeSessionId, taskText, retryModel, retryRole, candidateRun);
    activeEngCoderTaskId = submission.task_id;
    activeEngCoderSessionId = submission.session_id;
    assistantState.runtimeTaskId = submission.task_id;
    assistantState.runtimeSessionId = submission.session_id;
    storeAssistant(); // task id must survive a page refresh
    appendActivity(assistantState, 'Queued in EngCoder runtime');
    updateAssistantMessage(assistantBody, assistantState);

    const terminal = await pollEngCoderTask(submission, assistantBody, assistantState, timing, storeAssistant);
    const modelName = $('foundationModel')?.value || 'KeyMaker';

    if (terminal.kind === 'completed') {
      if (!assistantState.content && terminal.result) assistantState.content = cleanAssistantContent(terminal.result);
      finalizeAssistantState(assistantState, timing, 'engcoder-task', modelName);
      updateAssistantMessage(assistantBody, assistantState);

      storeAssistant(terminal.result);
      setStatus('Ready', 'idle');
      handleAssistantVoiceReply(cleanAssistantContent(assistantState.content || terminal.result));
    } else if (terminal.kind === 'cancelled') {
      finalizeAssistantState(assistantState, timing, 'engcoder-cancelled', modelName);
      appendActivity(assistantState, 'Stopped');
      updateAssistantMessage(assistantBody, assistantState);
      storeAssistant();
      setStatus('Stopped', 'idle');
    } else {
      throw new Error(terminal.error || 'EngCoder task failed.');
    }
  } catch (error) {
    if (error?.name === 'AbortError' || stopRequested) {
      finalizeAssistantState(assistantState, timing, 'engcoder-cancelled', $('foundationModel')?.value || 'KeyMaker');
      appendActivity(assistantState, 'Stopped');
      updateAssistantMessage(assistantBody, assistantState);
      storeAssistant();
      setStatus('Stopped', 'idle');
    } else {
      finalizeAssistantState(assistantState, timing, 'engcoder-error', $('foundationModel')?.value || 'KeyMaker');
      if (!String(assistantState.thinking || '').includes('Failed ·')) appendActivity(assistantState, `Client error · ${conciseRuntimeError(error?.message || error)}`);
      updateAssistantMessage(assistantBody, assistantState);
      storeAssistant();
      setStatus('Error', 'error');
      showError(String(error?.message || error), { retry: !isCandidateReply(assistantState) });
    }
  } finally {
    activeEngCoderTaskId = null;
    activeEngCoderSessionId = null;
    controller = null;
    streamReader = null;
    setBusy(false);
    maybeScroll(true);
    saveAll();
  }
}

async function stopStreaming() {
  stopRequested = true;
  const taskId = activeEngCoderTaskId;
  if (taskId) {
    try {
      await fetch(`${engcoderBase()}/api/tasks/${encodeURIComponent(taskId)}/cancel`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' }
      });
    } catch {}
  }

  const activeController = controller;
  if (activeController) activeController.abort();
  setStatus('Stopped', 'idle');
}

// Host-wide Qwen output observer. Requests through /api/tasks and through the
// OpenAI-compatible /v1 endpoints share the native model provider. This feed is
// deliberately independent from the current message's saved raw output.
function applyQwenLiveEvents(state, events) {
  let started = false;
  for (const event of events || []) {
    const id = String(event.request_id || '');
    if (!id) continue;
    if (event.kind === 'start') {
      state.requestId = id;
      state.model = String(event.model || 'Qwen Next Flash');
      // A request start is not generated text. Retain the previous answer
      // until an actual provider fragment replaces it.
      state.awaitingNewOutput = true;
      state.live = true;
      started = true;
      continue;
    }
    if (id !== state.requestId) continue;
    if (event.kind === 'finish') {
      state.live = false;
      continue;
    }
    if (event.kind === 'text' || event.kind === 'reasoning' || event.kind === 'arguments') {
      const fragment = String(event.delta || '');
      if (!fragment) continue;
      if (state.awaitingNewOutput) {
        state.rawOutput = '';
        state.awaitingNewOutput = false;
      }
      state.rawOutput = (state.rawOutput + fragment).slice(-RAW_OUTPUT_LIMIT);
    }
  }
  return started;
}

// Server snapshot is authoritative after a request finishes and when a slow
// client misses start/delta events due to the bounded event journal. Never
// discard a completed output simply because the generator became idle.
function reconcileQwenLiveSnapshot(state, latest) {
  if (!latest || !latest.request_id) return false;
  const changed = state.requestId !== String(latest.request_id);
  state.requestId = String(latest.request_id);
  state.model = String(latest.model || 'Qwen Next Flash');
  state.rawOutput = String(latest.output || '').slice(-RAW_OUTPUT_LIMIT);
  state.live = latest.generating === true;
  state.awaitingNewOutput = state.live && latest.has_current_output === false;
  return changed;
}

async function pollQwenLiveOutput() {
  if (qwenLivePolling || document.hidden) return;
  qwenLivePolling = true;
  try {
    const response = await fetch(`${engcoderBase()}/api/engcoder/qwen-output?after=${qwenLiveCursor}`, {cache: 'no-store'});
    if (!response.ok) return;
    const payload = await response.json();
    const events = Array.isArray(payload.events) ? payload.events : [];
    const previous = qwenLiveState.requestId;
    const wasInitialized = qwenLiveInitialized;
    const startedFromEvents = applyQwenLiveEvents(qwenLiveState, events);
    const startedFromSnapshot = reconcileQwenLiveSnapshot(qwenLiveState, payload.latest);
    const started = startedFromEvents || (startedFromSnapshot && qwenLiveState.live);
    // A native server restart resets its process-local journal cursor.
    const serverCursor = Number(payload.cursor) || 0;
    qwenLiveCursor = serverCursor < qwenLiveCursor ? serverCursor : Math.max(qwenLiveCursor, serverCursor);
    qwenLiveInitialized = true;
    // When an external tool/MCP client starts Qwen, switch to the dedicated
    // monitor. Do not steal focus from an in-progress EngCoder chat task.
    if (started && (wasInitialized || qwenLiveState.live) && !activeEngCoderTaskId && !pipelineBusy) {
      rightPanelView = 'qwen';
      if (!thinkingDrawer.classList.contains('open')) {
        thinkingDrawer.classList.add('open');
        syncShellLayout();
        scheduleRightDrawerRender({ layout: true });
      }
    }
    // A live feed must not force expensive synchronous layout every 500 ms.
    // Coalesce multiple provider fragments to one DOM update per browser frame.
    if (rightPanelView === 'qwen' && thinkingDrawer.classList.contains('open') &&
        (events.length || previous !== qwenLiveState.requestId || startedFromSnapshot)) {
      scheduleRightDrawerRender();
    }
  } catch (_) {
    // An optional status monitor must never interrupt the chat or inference.
  } finally {
    qwenLivePolling = false;
  }
}
