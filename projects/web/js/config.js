const APP_VERSION = '0.7.4'; // EngCoder product version.
const $ = id => document.getElementById(id);

const chatEl = $('chat');
const promptEl = $('prompt');
const sendBtn = $('send');
const stopBtn = $('stop');
const statusEl = $('status');
const statusDot = $('statusDot');
const errEl = $('error');
const errorTextEl = $('errorText');
const retryLastBtn = $('retryLast');
const settingsDialog = $('settingsDialog');
const labDialog = $('labDialog');
const drawer = $('drawer');
const thinkingDrawer = $('thinkingDrawer');
const thinkingDrawerBody = $('thinkingDrawerBody');
const thinkingStateEl = $('thinkingState');
const generatedOutputTab = $('generatedOutputTab');
const qwenLiveTab = $('qwenLiveTab');
const runInfoTab = $('runInfoTab');
let rightPanelView = 'output';
const closeThinkingDrawerBtn = $('closeThinkingDrawer');
const AUTO_CLOSE_CHAT_WIDTH = 1220;
const MIN_APP_WIDTH = 560;
const toggleDrawerBtn = $('toggleDrawer');
const toggleThinkingDrawerBtn = $('toggleThinkingDrawer');
const chatListEl = $('chatList');
const newChatBtn = $('newChat');
const ctxMenu = $('ctxMenu');
const attachBtn = $('attach');
const micBtn = $('mic');
const voiceMeter = $('voiceMeter');
const imgInput = $('imgInput');
const folderInput = $('folderInput');
const imgPreview = $('imgPreview');
const attachMenu = $('attachMenu');
const composerQuickBar = $('composerQuickBar');
const quickModeBtn = $('quickMode');
const quickModeValue = $('quickModeValue');
const quickModeMenu = $('quickModeMenu');
const quickModelBtn = $('quickModel');
const quickModelValue = $('quickModelValue');
const quickHistoryBtn = $('quickHistory');
const quickHistoryValue = $('quickHistoryValue');
const quickRuntimeBtn = $('quickRuntime');
const quickControlsBtn = $('quickControls');
const quickAttachmentCount = $('quickAttachmentCount');
const SpeechRecognitionCtor = window.SpeechRecognition || window.webkitSpeechRecognition || null;
const speechRecognitionSupported = !!SpeechRecognitionCtor;
const speechSynthesisSupported = 'speechSynthesis' in window && typeof window.SpeechSynthesisUtterance === 'function';

const STORAGE_KEYS = {
  endpoint: 'mylms_oa_endpoint',
  model: 'mylms_oa_model',
  apiKey: 'mylms_oa_api_key',
  system: 'mylms_oa_system',
  temp: 'mylms_oa_temp',
  maxTokens: 'mylms_oa_max_tokens',
  stream: 'mylms_oa_stream',
  voiceLang: 'mylms_oa_voice_lang',
  speakReplies: 'mylms_oa_speak_replies',
  chats: 'mylms_oa_chats',
  currentChat: 'mylms_oa_current_chat',
  modelRoles: 'mylms_oa_model_roles',
  // G4: context / history
  includeHistory: 'mylms_oa_include_history',
  maxHistoryTurns: 'mylms_oa_max_history_turns',
  // G4: voice
  autoRelisten: 'mylms_oa_auto_relisten',
  silenceFinishMs: 'mylms_oa_silence_finish_ms',
  voiceRate: 'mylms_oa_voice_rate',
  voicePitch: 'mylms_oa_voice_pitch',
  // G4: thinking / activity
  thinkingExpandDefault: 'mylms_oa_thinking_expand_default',
  thinkingShowEmpty: 'mylms_oa_thinking_show_empty',
  // G4: appearance
  theme: 'mylms_oa_theme',
  messageStyle: 'mylms_oa_message_style',
  fontSize: 'mylms_oa_font_size',
  foundationModel: 'engcoder_foundation_model',
  controllerModel: 'engcoder_controller_model',
  simpleResponseModel: 'engcoder_simple_response_model',
  reviewModel: 'engcoder_review_model',
  reviewMode: 'engcoder_review_mode',
  thinkingMode: 'engcoder_thinking_mode',
  addonCppModel: 'engcoder_addon_cpp_model',
  addonDocsModel: 'engcoder_addon_docs_model',
  addonPlannerModel: 'engcoder_addon_planner_model',
  addonToolsModel: 'engcoder_addon_tools_model',
  addonRerankerModel: 'engcoder_addon_reranker_model',
  forcedAddon: 'engcoder_forced_addon',
  autoRouting: 'engcoder_auto_routing',
  enableAddons: 'engcoder_enable_addons',
  showActivityDetails: 'engcoder_show_activity_details',
  trainingProfile: 'engcoder_training_profile',
  trainingScope: 'engcoder_training_scope',
  trainingSteps: 'engcoder_training_steps',
  checkpointSteps: 'engcoder_checkpoint_steps'
};

let controller = null;
let streamReader = null;
let pendingAttachments = [];
let lastUser = '';
let chats = [];
let currentChatId = null;
let activeThinkingState = null;
// Host-level output is separate from chat transcript state, so tool clients
// using /v1 also appear without changing or saving the current conversation.
let qwenLiveState = { rawOutput: '', rawOutputFormat: 'generated-v1', requestId: '', model: '', live: false, awaitingNewOutput: false };
let qwenLiveCursor = 0;
let qwenLivePolling = false;
let qwenLiveInitialized = false;
let editingMessageIndex = -1;

// Runtime architecture view. The native EngCoder process owns orchestration;
// the browser only renders discovered controller/foundation/addon state.
let models = [];
let manualModelRoles = {}; // legacy 0.2.x compatibility only
let pipelineBusy = false;
let runtimeArchitecture = null;
let modelDiscovery = null;
let modelRegistry = null;
let residencyState = null;
let trainingState = null;
let nativeModelLibrary = null;
let nativeModelLibraryPollTimer = null;

const ICON_SEND = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 19V5"></path><path d="M6 11l6-6 6 6"></path></svg>';
const ICON_STOP = '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="6" y="6" width="12" height="12" rx="0" fill="currentColor"></rect></svg>';
const ICON_TALK = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 12a8 8 0 0 1 16 0"></path><path d="M4 12v3a2 2 0 0 0 2 2h1v-7H6a2 2 0 0 0-2 2z"></path><path d="M20 12v3a2 2 0 0 1-2 2h-1v-7h1a2 2 0 0 1 2 2z"></path><path d="M9 19c1 .7 2 .9 3 .9s2-.2 3-.9"></path></svg>';

let recognition = null;
let recognitionMode = 'idle';
let voiceModeEnabled = false;
let speechBaseText = '';
let finalTranscript = '';
let interimTranscript = '';
let speakingUtterance = null;
let voiceRestartTimer = null;
let meterFrame = 0;
let voiceStartedAt = 0;
let lastVoiceTextAt = 0;
let voiceNoSpeech = false;
let voiceStopRequested = false;
let micPermissionGranted = false;
let micPrePermissionDone = false;

let shouldAutoScroll = true;



// Remote clients receive a one-time secret prompt; no token or network logic is
// imposed on local users. Session storage survives refresh but not browser close.
// The server never returns the secret and rejects remote state-changing calls
// without it. Intercept only same-origin EngCoder API requests.
{
  const baseFetch = window.fetch.bind(window);
  const localHosts = new Set(['localhost', '127.0.0.1', '::1', '[::1]']);
  const remoteBrowser = !localHosts.has(window.location.hostname);
  let remoteToken = remoteBrowser ? (sessionStorage.getItem('engcoder_remote_api_token') || '') : '';
  window.fetch = async (resource, init = {}) => {
    if (!remoteBrowser) return baseFetch(resource, init);
    const url = new URL(typeof resource === 'string' ? resource : resource.url, location.href);
    if (url.origin !== location.origin || !(url.pathname.startsWith('/api/') || url.pathname.startsWith('/v1/'))) {
      return baseFetch(resource, init);
    }
    const call = token => {
      const headers = new Headers(init.headers || {});
      if (token) headers.set('Authorization', `Bearer ${token}`);
      return baseFetch(resource, { ...init, headers });
    };
    let response = await call(remoteToken);
    if (response.status === 401) {
      const entered = window.prompt('EngCoder remote API token (configured on the host PC):', '');
      if (entered && entered.trim()) {
        remoteToken = entered.trim();
        sessionStorage.setItem('engcoder_remote_api_token', remoteToken);
        response = await call(remoteToken);
      }
    }
    return response;
  };
}
