function voiceLangValue() {
  return ($('voiceLang')?.value || navigator.language || 'en-US').trim() || 'en-US';
}

function hasPromptPayload() {
  return !!((promptEl.value || '').trim() || pendingAttachments.length);
}

function isVoiceActive() {
  return voiceModeEnabled || recognitionMode !== 'idle' || !!speakingUtterance;
}

function setPrimaryAction(kind) {
  sendBtn.dataset.action = kind;
  sendBtn.classList.toggle('is-stop', kind === 'stop');
  sendBtn.classList.toggle('is-talk', kind === 'talk');
  sendBtn.classList.toggle('active', kind === 'talk' && voiceModeEnabled);
  sendBtn.classList.toggle('listening', kind === 'talk' && recognitionMode === 'talk');

  if (kind === 'stop') {
    sendBtn.innerHTML = ICON_STOP;
    sendBtn.disabled = false;
    sendBtn.setAttribute('aria-label', controller ? 'Stop generating' : 'Stop voice');
    sendBtn.setAttribute('title', controller ? 'Stop generating' : 'Stop voice');
    return;
  }

  if (kind === 'talk') {
    sendBtn.innerHTML = ICON_TALK;
    sendBtn.disabled = !speechRecognitionSupported;
    sendBtn.setAttribute('aria-label', speechRecognitionSupported ? 'Talk mode' : 'Speech recognition unsupported');
    sendBtn.setAttribute('title', speechRecognitionSupported ? 'Talk mode' : 'Speech recognition is not supported in this browser');
    return;
  }

  sendBtn.innerHTML = ICON_SEND;
  sendBtn.disabled = false;
  sendBtn.setAttribute('aria-label', 'Send');
  sendBtn.setAttribute('title', 'Send');
}

function updatePrimaryAction() {
  if (controller || isVoiceActive()) {
    setPrimaryAction('stop');
    return;
  }

  if (hasPromptPayload()) setPrimaryAction('send');
  else setPrimaryAction('talk');
}

function updateVoiceButtons() {
  if (!micBtn) return;

  micBtn.classList.toggle('active', recognitionMode === 'dictation');
  micBtn.classList.toggle('listening', recognitionMode === 'dictation');
  micBtn.setAttribute('aria-pressed', recognitionMode === 'dictation' ? 'true' : 'false');

  if (!speechRecognitionSupported) {
    micBtn.disabled = true;
    micBtn.title = 'Speech recognition is not supported in this browser';
    updatePrimaryAction();
    return;
  }

  micBtn.disabled = !!controller || voiceModeEnabled || recognitionMode === 'talk';
  micBtn.title = recognitionMode === 'dictation' ? 'Stop dictation' : 'Dictate';
  updatePrimaryAction();
}

function stopVoiceRestartTimer() {
  if (voiceRestartTimer) {
    clearTimeout(voiceRestartTimer);
    voiceRestartTimer = null;
  }
}

function stopSpeechOutput() {
  if (!speechSynthesisSupported) return;
  try { window.speechSynthesis.cancel(); } catch {}
  speakingUtterance = null;
}

function showVoiceMeter(show) {
  if (!voiceMeter) return;
  voiceMeter.hidden = !show;
  voiceMeter.classList.toggle('active', !!show);
  voiceMeter.classList.toggle('silent', true);
  if (!show) {
    for (const bar of voiceMeter.querySelectorAll('.voice-meter-bar')) {
      bar.style.height = '8px';
    }
  }
}

function startMeter() {
  if (meterFrame) cancelAnimationFrame(meterFrame);

  const tick = () => {
    if (recognitionMode === 'idle') return;

    const now = performance.now();
    const elapsed = now - voiceStartedAt;
    const capturedText = `${finalTranscript} ${interimTranscript}`.trim();

    /*
      Important:
      Dictate and Talk both use SpeechRecognition. This timer is only a
      post-text silence finisher and a safety timeout. It must not decide
      "no audio" before SpeechRecognition has had a fair chance to emit text.

      The previous build killed Talk after ~6.5s with "No audio detected."
      even though Dictate worked. Chrome can delay Talk results until it has
      enough speech/silence context, so this gate must wait for recognition.
    */

    // G4: respect silence-finish timeout setting.
    const silenceFinishMs = Math.max(300, parseInt($('silenceFinishMs')?.value ?? '1350', 10) || 1350);
    if (capturedText && lastVoiceTextAt && now - lastVoiceTextAt > silenceFinishMs) {
      stopSpeechInput();
      return;
    }

    if (!capturedText && elapsed > 30000) {
      voiceNoSpeech = true;
      showError('No audio detected.');
      stopSpeechInput();
      return;
    }

    if (elapsed > 60000) {
      stopSpeechInput();
      return;
    }

    meterFrame = requestAnimationFrame(tick);
  };

  meterFrame = requestAnimationFrame(tick);
}

function stopMeter() {
  if (meterFrame) {
    cancelAnimationFrame(meterFrame);
    meterFrame = 0;
  }
  showVoiceMeter(false);
}

function stopSpeechInput() {
  stopVoiceRestartTimer();
  voiceStopRequested = true;

  if (!recognition) {
    recognitionMode = 'idle';
    stopMeter();
    updateVoiceButtons();
    return;
  }

  try { recognition.stop(); } catch {}
}

function createSpeechRecognition(mode) {
  const recog = new SpeechRecognitionCtor();
  recog.continuous = false;
  recog.interimResults = true;
  recog.maxAlternatives = 1;
  recog.lang = voiceLangValue();

  recog.onstart = () => {
    micPermissionGranted = true;
    voiceStartedAt = performance.now();
    lastVoiceTextAt = 0;
    voiceNoSpeech = false;
    voiceStopRequested = false;
    setStatus(mode === 'talk' ? 'Listening' : 'Dictating', 'busy');
    startMeter();
    updateVoiceButtons();
  };

  recog.onresult = event => {
    let interim = '';
    let freshFinal = '';

    for (let i = event.resultIndex; i < event.results.length; ++i) {
      const result = event.results[i];
      const part = result?.[0]?.transcript || '';
      if (result.isFinal) freshFinal += part;
      else interim += part;
    }

    if (freshFinal) {
      finalTranscript = joinSpeech(finalTranscript, freshFinal);
      interimTranscript = '';
    } else {
      interimTranscript = interim;
    }

    const spoken = joinSpeech(finalTranscript, interimTranscript);
    if (spoken.trim()) {
      lastVoiceTextAt = performance.now();
      const combined = mode === 'dictation'
        ? joinSpeech(speechBaseText, spoken)
        : spoken;
      promptEl.value = combined.trimStart();
      showError('');
      setStatus(mode === 'talk' ? 'Heard speech' : 'Dictating', 'busy');
      autoresize();
      updatePrimaryAction();
    }
  };

  recog.onerror = event => {
    const error = event?.error || 'unknown';
    if (error === 'no-speech') {
      voiceNoSpeech = true;
      showError('No audio detected.');
      return;
    }

    if (error === 'aborted' && voiceStopRequested) return;

    if (error === 'not-allowed' || error === 'service-not-allowed') {
      micPermissionGranted = false;
      voiceModeEnabled = false;
      voiceNoSpeech = true;
      showError('Microphone permission is blocked. Enable microphone access for this site and reload.');
      setStatus('Mic blocked', 'error');
      return;
    }

    showError(`Speech recognition error: ${error}`);
    setStatus('Error', 'error');
  };

  recog.onend = () => {
    const endedMode = recognitionMode;
    const capturedText = `${finalTranscript} ${interimTranscript}`.trim();
    const promptText = (promptEl.value || '').trim();
    const hasUsableText = !!(capturedText || promptText);

    recognition = null;
    recognitionMode = 'idle';
    stopMeter();

    if (!hasUsableText || voiceNoSpeech) {
      if (endedMode === 'talk') voiceModeEnabled = false;
      if (!controller) setStatus('No audio detected.', 'idle');
      showError('No audio detected.');
      updateVoiceButtons();
      return;
    }

    showError('');
    updateVoiceButtons();

    if (endedMode === 'talk') {
      if (voiceModeEnabled && promptText && !controller) {
        sendMessage({ regenerate: false });
        return;
      }
      if (voiceModeEnabled && micPermissionGranted && !controller && !speakingUtterance) scheduleVoiceListen();
      else if (!controller) setStatus('Ready', 'idle');
      return;
    }

    if (!controller) setStatus('Ready', 'idle');
  };

  return recog;
}

function joinSpeech(left, right) {
  const a = String(left || '').trim();
  const b = String(right || '').trim();

  if (!a) return b;
  if (!b) return a;

  return `${a}${/\s$/.test(left) ? '' : ' '}${b}`;
}

// G2: file:// mic-permission workaround.
// Chrome does NOT persist microphone permission for file:// origins, so the
// "Use your microphones" prompt re-appears on every activation. On http(s)
// origins (localhost) the permission persists and this is a no-op.
function showFileOriginMicNotice() {
  const banner = $('fileOriginMicBanner');
  if (!banner) return;
  if (location.protocol !== 'file:') return;
  banner.hidden = false;
  const dismiss = $('dismissFileOriginMicBanner');
  if (dismiss) dismiss.addEventListener('click', () => { banner.hidden = true; }, { once: true });
}

// One-time pre-permission: acquire the mic with getUserMedia during the user
// gesture (mic button click) and immediately release it. The in-session grant
// then lets recognition.start() proceed without re-prompting on file://.
// Returns true when it is safe to call recognition.start().
async function ensureMicPrePermission() {
  if (location.protocol !== 'file:') return true;
  if (micPrePermissionDone) return true;
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) return true;

  try {
    const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    for (const track of stream.getTracks()) track.stop();
    micPrePermissionDone = true;
    return true;
  } catch (error) {
    const name = error?.name || '';
    if (name === 'NotAllowedError' || name === 'SecurityError') {
      showError('Microphone permission is blocked. Allow microphone access and reload. (On file:// Chrome re-asks every time — serve over http://localhost for a one-click mic.)');
      setStatus('Mic blocked', 'error');
    } else {
      showError(String(error?.message || error));
      setStatus('Error', 'error');
    }
    return false;
  }
}

async function startSpeechInput(mode) {
  showError('');
  stopVoiceRestartTimer();

  if (!speechRecognitionSupported) {
    showError('Speech recognition is not supported in this browser. Use Chrome or Edge.');
    voiceModeEnabled = false;
    updateVoiceButtons();
    return;
  }

  if (recognitionMode !== 'idle') {
    stopSpeechInput();
    return;
  }

  // G2: on file:// origins, pre-acquire the mic permission once (within this
  // user gesture) so recognition.start() below does not re-prompt.
  if (!(await ensureMicPrePermission())) {
    voiceModeEnabled = false;
    updateVoiceButtons();
    return;
  }

  try {
    recognitionMode = mode;
    speechBaseText = mode === 'dictation' ? (promptEl.value || '') : '';
    finalTranscript = '';
    interimTranscript = '';
    voiceNoSpeech = false;
    voiceStopRequested = false;
    voiceStartedAt = performance.now();
    lastVoiceTextAt = 0;

    if (mode === 'talk') {
      promptEl.value = '';
      autoresize();
    }

    recognition = createSpeechRecognition(mode);
    setStatus(mode === 'talk' ? 'Listening' : 'Dictating', 'busy');
    updateVoiceButtons();
    recognition.start();
  } catch (error) {
    recognition = null;
    recognitionMode = 'idle';
    voiceModeEnabled = false;
    stopMeter();

    const name = error?.name || '';
    if (name === 'NotAllowedError' || name === 'SecurityError') {
      showError('Microphone permission is blocked. Enable microphone access for this site and reload.');
      setStatus('Mic blocked', 'error');
    } else {
      showError(String(error?.message || error));
      setStatus('Error', 'error');
    }

    updateVoiceButtons();
  }
}

function scheduleVoiceListen() {
  stopVoiceRestartTimer();
  if (!voiceModeEnabled || controller || speakingUtterance) return;
  // G4: respect auto-relisten setting.
  if ($('autoRelisten') && !$('autoRelisten').checked) return;
  if (!micPermissionGranted) {
    if (!controller) setStatus('Mic permission needed', 'idle');
    return;
  }
  voiceRestartTimer = setTimeout(() => {
    voiceRestartTimer = null;
    if (voiceModeEnabled && !controller && !speakingUtterance && recognitionMode === 'idle') {
      startSpeechInput('talk');
    }
  }, 600);
}

function toggleDictation() {
  showError('');
  if (recognitionMode === 'dictation') {
    stopSpeechInput();
    return;
  }
  if (voiceModeEnabled) toggleTalkMode(false);
  startSpeechInput('dictation');
}

function toggleTalkMode(force) {
  showError('');
  const next = typeof force === 'boolean' ? force : !voiceModeEnabled;
  voiceModeEnabled = next;
  stopVoiceRestartTimer();

  if (!next) {
    stopSpeechInput();
    stopSpeechOutput();
    recognitionMode = 'idle';
    setStatus('Ready', 'idle');
    updateVoiceButtons();
    return;
  }

  if (!speechRecognitionSupported) {
    voiceModeEnabled = false;
    showError('Speech recognition is not supported in this browser. Use Chrome or Edge.');
    updateVoiceButtons();
    return;
  }

  stopSpeechOutput();
  startSpeechInput('talk');
}

function speechSafeText(text) {
  return String(text || '')
    .replace(/```[\s\S]*?```/g, ' code block omitted ')
    .replace(/<think>[\s\S]*?<\/think>/gi, ' ')
    .replace(/[\r\n]+/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

function handleAssistantVoiceReply(text) {
  const shouldSpeak = voiceModeEnabled || !!$('speakReplies')?.checked;
  const safeText = speechSafeText(text);
  if (!safeText) {
    if (voiceModeEnabled) scheduleVoiceListen();
    return;
  }

  if (!shouldSpeak || !speechSynthesisSupported) {
    if (voiceModeEnabled) scheduleVoiceListen();
    return;
  }

  stopSpeechOutput();
  const utterance = new SpeechSynthesisUtterance(safeText);
  utterance.lang = voiceLangValue();
  // G4: respect voice rate/pitch settings.
  utterance.rate = clamp(parseFloat($('voiceRate')?.value ?? '1') || 1, 0.5, 2);
  utterance.pitch = clamp(parseFloat($('voicePitch')?.value ?? '1') || 1, 0, 2);
  utterance.onstart = () => {
    speakingUtterance = utterance;
    setStatus('Speaking', 'busy');
    updateVoiceButtons();
  };
  utterance.onend = () => {
    if (speakingUtterance === utterance) speakingUtterance = null;
    if (voiceModeEnabled) scheduleVoiceListen();
    else setStatus('Ready', 'idle');
    updateVoiceButtons();
  };
  utterance.onerror = () => {
    if (speakingUtterance === utterance) speakingUtterance = null;
    if (voiceModeEnabled) scheduleVoiceListen();
    else setStatus('Ready', 'idle');
    updateVoiceButtons();
  };
  window.speechSynthesis.speak(utterance);
}

