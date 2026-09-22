"use strict";

const loginView = document.querySelector("#login-view");
const chatView = document.querySelector("#chat-view");
const loginForm = document.querySelector("#login-form");
const nicknameInput = document.querySelector("#nickname-input");
const loginButton = document.querySelector("#login-button");
const loginError = document.querySelector("#login-error");
const currentUser = document.querySelector("#current-user");
const onlineCount = document.querySelector("#online-count");
const connectionDot = document.querySelector("#connection-dot");
const connectionBanner = document.querySelector("#connection-banner");
const connectionMessage = document.querySelector("#connection-message");
const retryButton = document.querySelector("#retry-button");
const messages = document.querySelector("#messages");
const currentChannel = document.querySelector("#current-channel");
const globalChannelButton = document.querySelector("#global-channel-button");
const groupToggleButton = document.querySelector("#group-toggle-button");
const groupCount = document.querySelector("#group-count");
const groupPanel = document.querySelector("#group-panel");
const groupForm = document.querySelector("#group-form");
const groupNameInput = document.querySelector("#group-name-input");
const joinGroupButton = document.querySelector("#join-group-button");
const groupFeedback = document.querySelector("#group-feedback");
const joinedGroups = document.querySelector("#joined-groups");
const userListContainer = document.querySelector("#user-list-container");
const globalUserItem = document.querySelector("#user-item-global");
const messageForm = document.querySelector("#message-form");
const messageInput = document.querySelector("#message-input");
const sendButton = document.querySelector("#send-button");
const voiceButton = document.querySelector("#voice-button");
const voiceFeedback = document.querySelector("#voice-feedback");
const stickerButton = document.querySelector("#sticker-button");
const stickerPanel = document.querySelector("#sticker-panel");
const themeToggle = document.querySelector("#theme-toggle");
const themeColorMeta = document.querySelector('meta[name="theme-color"]');

// 升级新增 DOM 节点
const pwaInstallButton = document.querySelector("#pwa-install-button");
const notificationToggle = document.querySelector("#notification-toggle");
const soundToggle = document.querySelector("#sound-toggle");
const replyBanner = document.querySelector("#reply-banner");
const replyToUser = document.querySelector("#reply-to-user");
const replyToText = document.querySelector("#reply-to-text");
const cancelReplyButton = document.querySelector("#cancel-reply-button");
const imageButton = document.querySelector("#image-button");
const imageFileInput = document.querySelector("#image-file-input");
const typingIndicator = document.querySelector("#typing-indicator");
const typingText = document.querySelector("#typing-text");
const imageLightbox = document.querySelector("#image-lightbox");
const lightboxImg = document.querySelector("#lightbox-img");
const lightboxDownload = document.querySelector("#lightbox-download");
const lightboxClose = document.querySelector("#lightbox-close");

// 认证与会话管理 DOM 节点
const tabLogin = document.querySelector("#tab-login");
const tabRegister = document.querySelector("#tab-register");
const tabGuest = document.querySelector("#tab-guest");
const passwordGroup = document.querySelector("#password-group");
const passwordInput = document.querySelector("#password-input");
const loginButtonText = document.querySelector("#login-button-text");
const nicknameLabel = document.querySelector("#nickname-label");
const logoutButton = document.querySelector("#logout-button");
const forgotPasswordLink = document.querySelector("#forgot-password-link");

// 密保恢复码与重置密码模态弹窗 DOM
const recoveryKeyModal = document.querySelector("#recovery-key-modal");
const displayRecoveryKey = document.querySelector("#display-recovery-key");
const copyRecoveryKeyBtn = document.querySelector("#copy-recovery-key-btn");
const copyRecoveryFeedback = document.querySelector("#copy-recovery-feedback");
const closeRecoveryModalBtn = document.querySelector("#close-recovery-modal-btn");

const resetPasswordModal = document.querySelector("#reset-password-modal");
const resetPasswordForm = document.querySelector("#reset-password-form");
const resetUsernameInput = document.querySelector("#reset-username-input");
const resetKeyInput = document.querySelector("#reset-key-input");
const resetNewpassInput = document.querySelector("#reset-newpass-input");
const resetPasswordError = document.querySelector("#reset-password-error");
const resetPasswordSuccess = document.querySelector("#reset-password-success");
const cancelResetBtn = document.querySelector("#cancel-reset-btn");
const submitResetBtn = document.querySelector("#submit-reset-btn");

let currentAuthMode = "login"; // "login" | "register" | "guest" | "token"
let currentPassword = "";
let sessionToken = localStorage.getItem("chatroom_token") || "";
const pendingAckTimers = new Map();

let desktopNotificationEnabled = localStorage.getItem("chatroom_notify") === "enabled";
let deferredInstallPrompt = null;

const MAX_RETRIES = 5;
const STATUS_INTERVAL_MS = 10_000;
const HISTORY_LIMIT = 50;
const MAX_TEXTAREA_HEIGHT = 132;
const MIN_RECORDING_MS = 500;
const MAX_RECORDING_MS = 60_000;
const FALLBACK_STICKERS = [
  { key: "happy", emoji: "😄" },
  { key: "sad", emoji: "😢" },
  { key: "like", emoji: "👍" },
  { key: "angry", emoji: "😠" },
  { key: "wow", emoji: "😮" },
];
const ALLOWED_STICKER_KEYS = new Set(FALLBACK_STICKERS.map(({ key }) => key));

let socket = null;
let nickname = "";
let loginPending = false;
let hasJoined = false;
let intentionalClose = false;
let retryCount = 0;
let reconnectTimer = null;
let statusTimer = null;
let historyLoading = false;
let historyLoaded = false;
let renderFrame = null;
let viewportFrame = null;
let composerEnabled = false;
let voiceBusy = false;
let voiceRequestId = 0;
let recordingSession = null;
let voiceFeedbackTimer = null;
let activeGroup = null;
let activeDm = null;
let activeReplyTo = null; // { id, from, text }
let soundEnabled = localStorage.getItem("chatroom_sound") !== "muted";
let typingSendTimer = 0;
let typingHideTimer = null;
let onlineUsersList = [];
const unreadDmCounts = new Map(); // username -> count
let allContactsList = []; // [{ nickname, online }]
let stickerMap = new Map(FALLBACK_STICKERS.map(({ key, emoji }) => [key, emoji]));
const messageReactions = new Map(); // msg_id -> Map<emoji, Set<username>>
const joinedGroupNames = new Set();
const pendingGroupJoins = new Set();
const messageNodesByChannel = new Map([["global", []]]);
const pendingMessageNodes = [];
const pendingRealtimePayloads = [];
const historyMessageKeys = new Set();
const defaultDocumentTitle = document.title;
const darkModeQuery = window.matchMedia("(prefers-color-scheme: dark)");
let mentionAudioContext = null;
let mentionTitleTimer = null;
let mentionTitleVisible = false;

// ==========================================
// 1. 主题与个性化色彩
// ==========================================

function storedTheme() {
  try {
    const theme = localStorage.getItem("theme");
    return theme === "dark" || theme === "light" ? theme : null;
  } catch {
    return null;
  }
}

function applyTheme(theme, remember = false) {
  const dark = theme === "dark";
  document.documentElement.classList.toggle("dark", dark);
  if (themeToggle) {
    themeToggle.querySelector("span").textContent = dark ? "☀️" : "🌙";
    const label = dark ? "切换到亮色主题" : "切换到暗黑主题";
    themeToggle.setAttribute("aria-label", label);
    themeToggle.title = label;
  }
  themeColorMeta?.setAttribute("content", dark ? "#11131a" : "#f3f5fa");

  if (remember) {
    try {
      localStorage.setItem("theme", theme);
    } catch {}
  }
}

function initializeTheme() {
  applyTheme(storedTheme() || (darkModeQuery.matches ? "dark" : "light"));
}

function getAvatarColor(name) {
  let hash = 0;
  const str = String(name || "用户");
  for (let i = 0; i < str.length; i++) {
    hash = str.charCodeAt(i) + ((hash << 5) - hash);
  }
  const hue = Math.abs(hash) % 360;
  return `linear-gradient(135deg, hsl(${hue}, 70%, 50%), hsl(${(hue + 45) % 360}, 75%, 40%))`;
}

function createAvatarElement(name, size = "sm") {
  const el = document.createElement("div");
  el.className = `user-avatar avatar-${size}`;
  el.style.background = getAvatarColor(name);
  const text = (name || "?").trim();
  el.textContent = text.slice(0, 1).toUpperCase();
  el.title = name;
  return el;
}

// ==========================================
// 2. Web Audio 音效系统
// ==========================================

function updateSoundButton() {
  if (!soundToggle) return;
  soundToggle.classList.toggle("muted", !soundEnabled);
  soundToggle.querySelector("span").textContent = soundEnabled ? "🔊" : "🔇";
  soundToggle.title = soundEnabled ? "提示音：已开启（点击静音）" : "提示音：已静音（点击开启）";
}

function toggleSound() {
  soundEnabled = !soundEnabled;
  try {
    localStorage.setItem("chatroom_sound", soundEnabled ? "enabled" : "muted");
  } catch {}
  updateSoundButton();
  if (soundEnabled) playSendSound();
}

function getAudioContext() {
  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  if (!AudioContextClass) return null;
  mentionAudioContext ||= new AudioContextClass();
  if (mentionAudioContext.state === "suspended") {
    void mentionAudioContext.resume().catch(() => {});
  }
  return mentionAudioContext;
}

function playSendSound() {
  if (!soundEnabled) return;
  try {
    const ctx = getAudioContext();
    if (!ctx) return;
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    const t = ctx.currentTime;
    osc.type = "sine";
    osc.frequency.setValueAtTime(440, t);
    osc.frequency.exponentialRampToValueAtTime(880, t + 0.08);
    gain.gain.setValueAtTime(0.08, t);
    gain.gain.exponentialRampToValueAtTime(0.001, t + 0.08);
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start(t);
    osc.stop(t + 0.09);
  } catch {}
}

function playReceiveSound() {
  if (!soundEnabled) return;
  try {
    const ctx = getAudioContext();
    if (!ctx) return;
    const osc1 = ctx.createOscillator();
    const osc2 = ctx.createOscillator();
    const gain = ctx.createGain();
    const t = ctx.currentTime;
    osc1.type = "triangle";
    osc2.type = "sine";
    osc1.frequency.setValueAtTime(523.25, t); // C5
    osc2.frequency.setValueAtTime(659.25, t + 0.06); // E5
    gain.gain.setValueAtTime(0.1, t);
    gain.gain.exponentialRampToValueAtTime(0.001, t + 0.22);
    osc1.connect(gain);
    osc2.connect(gain);
    gain.connect(ctx.destination);
    osc1.start(t);
    osc1.stop(t + 0.08);
    osc2.start(t + 0.06);
    osc2.stop(t + 0.22);
  } catch {}
}

function playMentionTone() {
  if (!soundEnabled) return;
  try {
    const ctx = getAudioContext();
    if (!ctx) return;
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    const t = ctx.currentTime;
    osc.type = "sine";
    osc.frequency.setValueAtTime(740, t);
    osc.frequency.setValueAtTime(880, t + 0.08);
    gain.gain.setValueAtTime(0.0001, t);
    gain.gain.exponentialRampToValueAtTime(0.12, t + 0.015);
    gain.gain.exponentialRampToValueAtTime(0.0001, t + 0.25);
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start(t);
    osc.stop(t + 0.26);
  } catch {}
}

// ==========================================
// 3. 时间与标题栏提醒
// ==========================================

function formatMessageTime(date) {
  const now = new Date();
  const sameDay =
    date.getFullYear() === now.getFullYear() &&
    date.getMonth() === now.getMonth() &&
    date.getDate() === now.getDate();
  const hours = String(date.getHours()).padStart(2, "0");
  const minutes = String(date.getMinutes()).padStart(2, "0");
  if (sameDay) return `${hours}:${minutes}`;
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const day = String(date.getDate()).padStart(2, "0");
  return `${month}-${day} ${hours}:${minutes}`;
}

function setMessageTime(time, ts) {
  const date = Number.isFinite(ts) && ts > 0 ? new Date(ts) : new Date();
  time.dateTime = date.toISOString();
  time.textContent = formatMessageTime(date);
  time.title = date.toLocaleString("zh-CN", { hour12: false });
}

function stopMentionTitleAlert() {
  clearInterval(mentionTitleTimer);
  mentionTitleTimer = null;
  mentionTitleVisible = false;
  document.title = defaultDocumentTitle;
}

function startMentionTitleAlert() {
  if (!activeGroup && document.hasFocus()) return;
  if (mentionTitleTimer !== null) return;
  mentionTitleVisible = true;
  document.title = "🔔 @你";
  mentionTitleTimer = setInterval(() => {
    mentionTitleVisible = !mentionTitleVisible;
    document.title = mentionTitleVisible ? "🔔 @你" : defaultDocumentTitle;
  }, 900);
}

// ==========================================
// 3.1 Windows 原生系统桌面通知
// ==========================================

function updateNotificationButton() {
  if (!notificationToggle) return;
  if (!("Notification" in window)) {
    notificationToggle.hidden = true;
    return;
  }
  if (Notification.permission === "denied") {
    notificationToggle.classList.add("denied");
    notificationToggle.title = "系统桌面通知已被浏览器权限拦截（可在地址栏网站设置中允许）";
    notificationToggle.classList.remove("active");
  } else if (Notification.permission === "granted" && desktopNotificationEnabled) {
    notificationToggle.classList.add("active");
    notificationToggle.title = "系统桌面通知：已开启（后台/最小化时接收系统 Toast 弹窗）";
  } else {
    notificationToggle.classList.remove("active");
    notificationToggle.title = "系统桌面通知：未开启（点击开启桌面通知）";
  }
}

async function toggleDesktopNotification() {
  if (!("Notification" in window)) return;
  if (Notification.permission === "default") {
    const perm = await Notification.requestPermission();
    if (perm === "granted") {
      desktopNotificationEnabled = true;
      try {
        localStorage.setItem("chatroom_notify", "enabled");
      } catch {}
      sendDesktopNotification("Chatroom 极客聊天室", "🎉 系统桌面通知已开启！新消息在后台时会自动弹出。");
    }
  } else if (Notification.permission === "granted") {
    desktopNotificationEnabled = !desktopNotificationEnabled;
    try {
      localStorage.setItem("chatroom_notify", desktopNotificationEnabled ? "enabled" : "disabled");
    } catch {}
  }
  updateNotificationButton();
}

function sendDesktopNotification(title, body) {
  if (!("Notification" in window) || Notification.permission !== "granted" || !desktopNotificationEnabled) {
    return;
  }
  if (!document.hidden && document.hasFocus() && !activeGroup && !activeDm) {
    return;
  }
  try {
    const notif = new Notification(title, {
      body: String(body || "").slice(0, 100),
      icon: "/icons/icon-192.png",
      badge: "/icons/icon-192.png",
      tag: "chatroom-msg",
    });
    notif.onclick = () => {
      window.focus();
      notif.close();
    };
  } catch {}
}

// ==========================================
// 4. Markdown & 代码块解析器
// ==========================================

function renderMarkdownContent(text, container) {
  const codeBlockRegex = /```([a-zA-Z0-9_-]*)\n([\s\S]*?)```/g;
  let lastIndex = 0;
  let match;

  while ((match = codeBlockRegex.exec(text)) !== null) {
    if (match.index > lastIndex) {
      const part = text.slice(lastIndex, match.index);
      renderInlineMarkdown(part, container);
    }
    const lang = (match[1] || "code").toLowerCase();
    const code = match[2];

    const block = document.createElement("div");
    block.className = "code-block";

    const header = document.createElement("div");
    header.className = "code-header";
    const langSpan = document.createElement("span");
    langSpan.className = "code-lang";
    langSpan.textContent = lang;

    const copyBtn = document.createElement("button");
    copyBtn.className = "copy-code-button";
    copyBtn.type = "button";
    copyBtn.textContent = "📋 复制";
    copyBtn.addEventListener("click", () => {
      navigator.clipboard?.writeText(code).then(() => {
        copyBtn.textContent = "✓ 已复制!";
        setTimeout(() => { copyBtn.textContent = "📋 复制"; }, 1600);
      });
    });

    header.append(langSpan, copyBtn);
    const pre = document.createElement("pre");
    pre.className = "code-pre";
    const codeEl = document.createElement("code");
    codeEl.textContent = code;
    pre.append(codeEl);
    block.append(header, pre);
    container.append(block);

    lastIndex = match.index + match[0].length;
  }

  if (lastIndex < text.length) {
    const part = text.slice(lastIndex);
    renderInlineMarkdown(part, container);
  }
}

function renderInlineMarkdown(text, container) {
  const lines = text.split("\n");
  lines.forEach((line, index) => {
    if (index > 0) container.append(document.createElement("br"));

    if (line.startsWith("> ")) {
      const bq = document.createElement("blockquote");
      bq.className = "markdown-quote";
      renderFormattedSpans(line.slice(2), bq);
      container.append(bq);
      return;
    }

    renderFormattedSpans(line, container);
  });
}

function renderFormattedSpans(text, container) {
  const tokenRegex = /(`[^`]+`|\*\*[^*]+\*\*|\*[^*]+\*|~~[^~]+~~|https?:\/\/[^\s]+)/g;
  let lastIndex = 0;
  let match;

  while ((match = tokenRegex.exec(text)) !== null) {
    if (match.index > lastIndex) {
      container.append(document.createTextNode(text.slice(lastIndex, match.index)));
    }
    const token = match[0];
    if (token.startsWith("`") && token.endsWith("`")) {
      const codeSpan = document.createElement("code");
      codeSpan.className = "markdown-inline-code";
      codeSpan.textContent = token.slice(1, -1);
      container.append(codeSpan);
    } else if (token.startsWith("**") && token.endsWith("**")) {
      const boldSpan = document.createElement("strong");
      boldSpan.className = "markdown-bold";
      boldSpan.textContent = token.slice(2, -2);
      container.append(boldSpan);
    } else if (token.startsWith("*") && token.endsWith("*")) {
      const italicSpan = document.createElement("em");
      italicSpan.className = "markdown-italic";
      italicSpan.textContent = token.slice(1, -1);
      container.append(italicSpan);
    } else if (token.startsWith("~~") && token.endsWith("~~")) {
      const strikeSpan = document.createElement("del");
      strikeSpan.className = "markdown-strike";
      strikeSpan.textContent = token.slice(2, -2);
      container.append(strikeSpan);
    } else if (token.startsWith("http")) {
      const link = document.createElement("a");
      link.className = "chat-link";
      link.href = token;
      link.target = "_blank";
      link.rel = "noopener noreferrer";
      link.textContent = token;
      container.append(link);
    }
    lastIndex = match.index + token.length;
  }

  if (lastIndex < text.length) {
    container.append(document.createTextNode(text.slice(lastIndex)));
  }
}

// ==========================================
// 5. 引用回复与消息 Reaction
// ==========================================

function setReplyTo(id, user, text) {
  activeReplyTo = { id, from: user, text: (text || "富媒体消息").slice(0, 70) };
  if (replyBanner && replyToUser && replyToText) {
    replyToUser.textContent = user;
    replyToText.textContent = activeReplyTo.text;
    replyBanner.hidden = false;
  }
  messageInput?.focus();
}

function clearReplyTo() {
  activeReplyTo = null;
  if (replyBanner) replyBanner.hidden = true;
}

function renderQuoteBubble(reply_to, container) {
  if (!reply_to || typeof reply_to !== "object") return;
  const quote = document.createElement("div");
  quote.className = "message-quote";
  const author = document.createElement("span");
  author.className = "quote-author";
  author.textContent = `↩ 回复 ${reply_to.from || "用户"}:`;
  const snippet = document.createElement("span");
  snippet.className = "quote-text";
  snippet.textContent = reply_to.text || "";
  quote.append(author, snippet);

  if (reply_to.id) {
    quote.addEventListener("click", () => {
      const target = document.querySelector(`[data-message-id="${reply_to.id}"]`) ||
                     document.getElementById(`msg-${reply_to.id}`);
      if (target) {
        target.scrollIntoView({ behavior: "smooth", block: "center" });
        target.classList.remove("highlight-pulse");
        void target.offsetWidth;
        target.classList.add("highlight-pulse");
      }
    });
  }
  container.append(quote);
}

function createReactionBar(msg_id, authorName, textSnippet) {
  const bar = document.createElement("div");
  bar.className = "reaction-bar";
  const emojis = ["👍", "❤️", "😂", "🔥", "🎉", "🚀"];

  emojis.forEach((emoji) => {
    const btn = document.createElement("button");
    btn.className = "reaction-quick-btn";
    btn.type = "button";
    btn.textContent = emoji;
    btn.title = `回应 ${emoji}`;
    btn.addEventListener("click", (e) => {
      e.stopPropagation();
      sendReaction(msg_id, emoji);
    });
    bar.append(btn);
  });

  const replyBtn = document.createElement("button");
  replyBtn.className = "reaction-quick-btn";
  replyBtn.type = "button";
  replyBtn.textContent = "↩";
  replyBtn.title = "引用回复";
  replyBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    setReplyTo(msg_id, authorName, textSnippet);
  });
  bar.append(replyBtn);

  if (authorName === nickname || nickname === "admin") {
    const recallBtn = document.createElement("button");
    recallBtn.className = "reaction-quick-btn recall-btn";
    recallBtn.type = "button";
    recallBtn.textContent = "🗑️";
    recallBtn.title = "撤回 (2分钟内)";
    recallBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      if (confirm("确定要撤回这条消息吗？")) {
        sendRecall(msg_id);
      }
    });
    bar.append(recallBtn);
  }

  return bar;
}

function sendRecall(msg_id) {
  if (!socket || socket.readyState !== WebSocket.OPEN) return;
  const payload = {
    type: "recall",
    msg_id,
  };
  if (activeGroup) payload.group = activeGroup;
  if (activeDm) payload.to = activeDm;
  socket.send(JSON.stringify(payload));
}

function handleMessageRecall(msg_id, from) {
  const isMine = from === nickname;
  const tipText = `${isMine ? "你" : from} 撤回了一条消息`;

  const rows = document.querySelectorAll(`[data-message-id="${CSS.escape(msg_id)}"], #msg-${CSS.escape(msg_id)}`);
  for (const row of rows) {
    row.className = "system-message recalled-message-notice";
    row.innerHTML = `<span class="recalled-text">${tipText}</span>`;
  }

  for (const [chKey, nodes] of messageNodesByChannel.entries()) {
    for (let i = 0; i < nodes.length; ++i) {
      if (nodes[i] && (nodes[i].dataset?.messageId === msg_id || nodes[i].id === `msg-${msg_id}`)) {
        const notice = document.createElement("div");
        notice.className = "system-message recalled-message-notice";
        notice.innerHTML = `<span class="recalled-text">${tipText}</span>`;
        nodes[i] = notice;
      }
    }
  }
}

function sendReaction(msg_id, emoji) {
  if (!socket || socket.readyState !== WebSocket.OPEN) return;
  const payload = {
    type: "reaction",
    msg_id,
    emoji,
  };
  if (activeGroup) payload.group = activeGroup;
  if (activeDm) payload.to = activeDm;
  socket.send(JSON.stringify(payload));
}

function renderReactionBadges(msg_id) {
  const container = document.getElementById(`reactions-${msg_id}`);
  if (!container) return;
  container.replaceChildren();

  const msgMap = messageReactions.get(msg_id);
  if (!msgMap || msgMap.size === 0) return;

  for (const [emoji, users] of msgMap.entries()) {
    if (users.size === 0) continue;
    const badge = document.createElement("button");
    badge.className = "reaction-badge";
    const hasMine = users.has(nickname);
    badge.classList.toggle("reacted", hasMine);
    badge.type = "button";
    badge.title = `${[...users].join(", ")} 回应了 ${emoji}`;
    badge.innerHTML = `<span class="badge-emoji">${emoji}</span> <span class="badge-count">${users.size}</span>`;
    badge.addEventListener("click", () => sendReaction(msg_id, emoji));
    container.append(badge);
  }
}

// ==========================================
// 6. 图片灯箱全屏预览
// ==========================================

function openLightbox(url) {
  if (!imageLightbox || !lightboxImg) return;
  lightboxImg.src = url;
  if (lightboxDownload) lightboxDownload.href = url;
  imageLightbox.hidden = false;
}

function closeLightbox() {
  if (!imageLightbox) return;
  imageLightbox.hidden = true;
  if (lightboxImg) lightboxImg.src = "";
}

// ==========================================
// 7. 图片上传与发送
function getMimeTypeForImage(file) {
  if (file.type && file.type.startsWith("image/")) {
    return file.type;
  }
  const name = (file.name || "").toLowerCase();
  if (name.endsWith(".png")) return "image/png";
  if (name.endsWith(".jpg") || name.endsWith(".jpeg")) return "image/jpeg";
  if (name.endsWith(".gif")) return "image/gif";
  if (name.endsWith(".webp")) return "image/webp";
  if (name.endsWith(".svg")) return "image/svg+xml";
  if (name.endsWith(".bmp")) return "image/bmp";
  return "image/png";
}

async function uploadAndSendImage(file) {
  if (!file) {
    showVoiceFeedback("未检测到有效图片", true);
    return;
  }
  const mimeType = getMimeTypeForImage(file);
  if (!mimeType.startsWith("image/")) {
    showVoiceFeedback("请选择 PNG / JPG / GIF / WebP / SVG / BMP 图片", true);
    return;
  }
  if (file.size > 10 * 1024 * 1024) {
    showVoiceFeedback("图片大小不能超过 10MB", true);
    return;
  }
  if (!hasJoined || socket?.readyState !== WebSocket.OPEN) {
    showVoiceFeedback("未连接到聊天室，图片发送失败", true);
    return;
  }

  showVoiceFeedback("正在上传图片…");
  try {
    const res = await fetch("/api/upload/image", {
      method: "POST",
      headers: { "Content-Type": mimeType },
      body: file,
    });
    if (!res.ok) {
      let errText = `上传失败 (${res.status})`;
      try {
        const errData = await res.json();
        if (errData.error) errText = errData.error;
      } catch {}
      throw new Error(errText);
    }
    const data = await res.json();
    if (!data.url) throw new Error("服务器未返回图片地址");

    const payload = {
      type: "image_send",
      url: data.url,
      msg_id: `${Date.now()}_${nickname}`,
    };
    if (activeGroup) payload.group = activeGroup;
    if (activeDm) payload.to = activeDm;
    if (activeReplyTo) {
      payload.reply_to = activeReplyTo;
      clearReplyTo();
    }

    socket.send(JSON.stringify(payload));
    showVoiceFeedback("图片已发送");
    playSendSound();
  } catch (err) {
    showVoiceFeedback(`图片发送失败: ${err.message || "网络异常"}`, true);
  }
}

// ==========================================
// 8. 正在输入提示 (Typing Indicator)
// ==========================================

function notifyTyping() {
  const now = performance.now();
  if (now - typingSendTimer > 2500) {
    typingSendTimer = now;
    if (socket?.readyState === WebSocket.OPEN && hasJoined) {
      const payload = {
        type: "typing",
        channel: activeGroup ? "group" : (activeDm ? "dm" : "global"),
      };
      if (activeGroup) payload.group = activeGroup;
      if (activeDm) payload.to = activeDm;
      socket.send(JSON.stringify(payload));
    }
  }
}

function showTypingIndicator(fromUser) {
  if (!typingIndicator || !typingText) return;
  clearTimeout(typingHideTimer);
  typingText.textContent = `${fromUser} 正在输入...`;
  typingIndicator.hidden = false;
  typingHideTimer = setTimeout(() => {
    typingIndicator.hidden = true;
  }, 3500);
}

// ==========================================
// 9. 斜杠快捷指令 (Slash Commands)
// ==========================================

function executeSlashCommand(text) {
  const trimmed = text.trim();
  if (trimmed === "/help") {
    appendSystemMessage(
      "📖 聊天室快捷指令手册：\n" +
      "• /roll [最大值] — 掷骰子，如 /roll 100\n" +
      "• /clear — 清空当前频道的屏幕消息\n" +
      "• /shrug — 发送 ¯\\_(ツ)_/¯\n" +
      "• /flip — 发送 (╯°□°)╯︵ ┻━┻\n" +
      "• @昵称 — 提及提醒该用户\n" +
      "• 支持 Markdown 代码块语法（```cpp ... ```）\n" +
      "• 支持直接 Ctrl+V 粘贴截图发送图片"
    );
    return true;
  }
  if (trimmed === "/clear") {
    const key = getChannelKey();
    const list = messageNodesByChannel.get(key);
    if (list) list.length = 0;
    messages.replaceChildren(createEmptyState());
    appendSystemMessage("已清空当前屏幕消息");
    return true;
  }
  if (trimmed.startsWith("/roll")) {
    const parts = trimmed.split(/\s+/);
    let max = 100;
    if (parts[1] && !isNaN(parseInt(parts[1], 10))) {
      max = Math.max(1, parseInt(parts[1], 10));
    }
    const val = Math.floor(Math.random() * max) + 1;
    const msg = `🎲 掷出了 ${val} 点 (1-${max})`;
    sendRawChatMessage(msg);
    return true;
  }
  if (trimmed === "/shrug") {
    sendRawChatMessage("¯\\_(ツ)_/¯");
    return true;
  }
  if (trimmed === "/flip") {
    sendRawChatMessage("(╯°□°)╯︵ ┻━┻");
    return true;
  }
  return false;
}

function sendRawChatMessage(text) {
  if (!text || socket?.readyState !== WebSocket.OPEN || !hasJoined) return;
  const payload = {
    text,
    msg_id: `${Date.now()}_${nickname}`,
  };
  if (activeReplyTo) {
    payload.reply_to = activeReplyTo;
    clearReplyTo();
  }

  if (activeDm) {
    payload.type = "dm";
    payload.to = activeDm;
  } else if (activeGroup) {
    payload.type = "group_chat";
    payload.group = activeGroup;
  } else {
    payload.type = "chat";
  }

  socket.send(JSON.stringify(payload));
  playSendSound();
}

// ==========================================
// 10. 视图切换与通道路由
// ==========================================

function setLoginError(message) {
  loginError.textContent = message;
  nicknameInput.setAttribute("aria-invalid", message ? "true" : "false");
}

function setLoginLoading(loading) {
  loginButton.disabled = loading;
  nicknameInput.disabled = loading;
  loginButton.querySelector("span:first-child").textContent = loading
    ? "正在连接…"
    : "进入聊天室";
}

function showChatView() {
  loginView.hidden = true;
  chatView.hidden = false;
  currentUser.textContent = nickname;
  currentUser.title = `当前昵称：${nickname}`;
  setComposerEnabled(true);
  messageInput.focus();
}

function showLoginView(errorMessage = "") {
  stopStatusPolling();
  hasJoined = false;
  loginPending = false;
  chatView.hidden = true;
  loginView.hidden = false;
  setLoginLoading(false);
  setLoginError(errorMessage);
  nicknameInput.focus();
  nicknameInput.select();
}

function getChannelKey() {
  if (activeDm) return `dm:${activeDm}`;
  return activeGroup ? `group:${activeGroup}` : "global";
}

function createEmptyState() {
  const state = document.createElement("div");
  state.className = "empty-state";

  const icon = document.createElement("div");
  icon.className = "empty-icon";
  icon.setAttribute("aria-hidden", "true");
  icon.textContent = activeDm ? "🔒" : activeGroup ? "群" : "✦";

  const title = document.createElement("p");
  title.textContent = activeDm
    ? `与 ${activeDm} 的私聊`
    : activeGroup
      ? `已进入群 ${activeGroup}`
      : "已进入聊天室";

  const copy = document.createElement("span");
  copy.textContent = activeDm
    ? "私聊消息仅实时传输，不会保存到历史记录"
    : activeGroup
      ? "这是一条独立的群聊频道"
      : "说声你好，开启今天的对话吧";

  state.append(icon, title, copy);
  return state;
}

function renderCurrentChannel() {
  const nodes = messageNodesByChannel.get(getChannelKey()) || [];
  messages.replaceChildren(...(nodes.length ? nodes : [createEmptyState()]));
  messages.scrollTop = messages.scrollHeight;
}

function updateChannelUI() {
  const inGroup = Boolean(activeGroup);
  const inDm = Boolean(activeDm);
  currentChannel.textContent = inDm ? `私聊：${activeDm}` : inGroup ? `群 ${activeGroup}` : "全局聊天";
  currentChannel.classList.toggle("group-channel", inGroup);
  currentChannel.classList.toggle("dm-channel", inDm);
  globalChannelButton.hidden = !inGroup && !inDm;
  groupToggleButton.hidden = inDm;
  if (inDm) {
    groupPanel.hidden = true;
    groupToggleButton.setAttribute("aria-expanded", "false");
  }
  messageInput.placeholder = inDm
    ? `私聊 ${activeDm}… (支持 Markdown / 粘贴图片)`
    : inGroup
      ? `发消息到群 ${activeGroup}… (支持 Markdown / 粘贴图片)`
      : "输入消息… (支持 Markdown / 代码块 / 粘贴图片 / /help 指令)";
  stickerButton.parentElement.hidden = inDm;
  voiceButton.hidden = inDm;
  stickerButton.disabled = !composerEnabled || inGroup || inDm;
  stickerButton.title = inGroup ? "表情包目前仅支持全局聊天" : "选择表情包";
  voiceButton.disabled = !composerEnabled || voiceBusy || inGroup || inDm;
  voiceButton.title = inGroup ? "语音消息目前仅支持全局聊天" : "";
  chatView.classList.toggle("dm-mode", inDm);
  closeStickerPanel();

  globalUserItem.classList.toggle("active", !inGroup && !inDm);
  for (const button of userListContainer.querySelectorAll("button[data-user]")) {
    button.classList.toggle("active", button.dataset.user === activeDm);
  }

  for (const button of joinedGroups.querySelectorAll("button[data-group]")) {
    button.classList.toggle("active", button.dataset.group === activeGroup);
  }
}

function switchChannel(group = null) {
  if (group && !joinedGroupNames.has(group)) return;
  if (group && recordingSession) cancelVoiceRecording();
  activeDm = null;
  activeGroup = group;
  clearReplyTo();
  const channelKey = group ? `group:${group}` : "global";
  if (!messageNodesByChannel.has(channelKey)) {
    messageNodesByChannel.set(channelKey, []);
  }
  updateChannelUI();
  renderCurrentChannel();
  if (!group && document.hasFocus()) stopMentionTitleAlert();
  messageInput.focus();
}

function switchDm(target) {
  if (!target || target === nickname) return;
  if (recordingSession) cancelVoiceRecording();
  activeGroup = null;
  activeDm = target;
  clearReplyTo();

  if (unreadDmCounts.has(target)) {
    unreadDmCounts.delete(target);
    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify({ type: "mark_read", from: target }));
    }
    renderOnlineUsers(allContactsList.length ? allContactsList : onlineUsersList);
  }

  const channelKey = `dm:${target}`;
  if (!messageNodesByChannel.has(channelKey)) messageNodesByChannel.set(channelKey, []);
  updateChannelUI();
  renderCurrentChannel();
  messageInput.focus();
}

function renderJoinedGroups() {
  joinedGroups.replaceChildren();
  groupCount.textContent = String(joinedGroupNames.size);

  if (!joinedGroupNames.size) {
    const empty = document.createElement("span");
    empty.className = "no-groups";
    empty.textContent = "尚未加入群组";
    joinedGroups.append(empty);
    return;
  }

  for (const group of joinedGroupNames) {
    const button = document.createElement("button");
    button.type = "button";
    button.dataset.group = group;
    button.textContent = group;
    button.title = `进入群 ${group}`;
    button.classList.toggle("active", group === activeGroup);
    button.addEventListener("click", () => switchChannel(group));
    joinedGroups.append(button);
  }
}

function setGroupFeedback(message, isError = false) {
  groupFeedback.textContent = message;
  groupFeedback.classList.toggle("error", isError);
}

function confirmGroupJoin(group) {
  if (!group) return;
  pendingGroupJoins.delete(group);
  joinedGroupNames.add(group);
  const channelKey = `group:${group}`;
  messageNodesByChannel.set(channelKey, messageNodesByChannel.get(channelKey) || []);
  renderJoinedGroups();
  setGroupFeedback(`已加入群 ${group}`);
  groupNameInput.value = "";
  switchChannel(group);
}

function resetGroupsAfterDisconnect() {
  const hadGroups = joinedGroupNames.size > 0 || pendingGroupJoins.size > 0;
  joinedGroupNames.clear();
  pendingGroupJoins.clear();
  for (const key of [...messageNodesByChannel.keys()]) {
    if (key.startsWith("group:")) messageNodesByChannel.delete(key);
  }
  activeGroup = null;
  renderJoinedGroups();
  setGroupFeedback(hadGroups ? "群组状态已清空，重连后请重新加入" : "");
  updateChannelUI();
  renderCurrentChannel();
  if (hadGroups) appendSystemMessage("连接已断开，群组成员状态已清空；重连后请重新加入群组");
}

function closeStickerPanel() {
  stickerPanel.hidden = true;
  stickerButton.setAttribute("aria-expanded", "false");
}

function renderStickerPanel(stickers) {
  stickerPanel.replaceChildren();
  for (const { key, emoji } of stickers) {
    const button = document.createElement("button");
    button.type = "button";
    button.setAttribute("role", "menuitem");
    button.dataset.sticker = key;
    button.title = `发送 ${emoji}`;
    button.setAttribute("aria-label", `发送表情 ${emoji}`);
    button.textContent = emoji;
    button.addEventListener("click", () => sendSticker(key));
    stickerPanel.append(button);
  }
}

async function loadStickers() {
  let stickers = FALLBACK_STICKERS;
  try {
    const response = await fetch("/api/stickers", {
      method: "GET",
      headers: { Accept: "application/json" },
      cache: "no-store",
    });
    if (!response.ok) throw new Error("sticker request failed");
    const result = await response.json();
    const valid = Array.isArray(result.stickers)
      ? result.stickers.filter(
          ({ key, emoji }) =>
            ALLOWED_STICKER_KEYS.has(key) && typeof emoji === "string" && emoji.trim(),
        )
      : [];
    if (!valid.length) throw new Error("invalid sticker response");
    stickers = valid;
  } catch {}

  stickerMap = new Map(stickers.map(({ key, emoji }) => [key, emoji]));
  renderStickerPanel(stickers);
}

function sendSticker(key) {
  if (
    activeGroup ||
    !stickerMap.has(key) ||
    socket?.readyState !== WebSocket.OPEN ||
    !hasJoined
  ) {
    return;
  }
  const payload = {
    type: "chat",
    text: "",
    sticker: key,
    msg_id: `${Date.now()}_${nickname}`,
  };
  if (activeReplyTo) {
    payload.reply_to = activeReplyTo;
    clearReplyTo();
  }
  socket.send(JSON.stringify(payload));
  closeStickerPanel();
  messageInput.focus();
  playSendSound();
}

function setComposerEnabled(enabled) {
  composerEnabled = enabled;
  messageInput.disabled = !enabled;
  groupNameInput.disabled = !enabled;
  joinGroupButton.disabled = !enabled;
  voiceButton.disabled = !enabled || voiceBusy || Boolean(activeGroup);
  stickerButton.disabled = !enabled || Boolean(activeGroup);
  if (imageButton) imageButton.disabled = !enabled;

  if (!enabled) cancelVoiceRecording();
  updateChannelUI();
  updateSendButtonState();
}

function updateSendButtonState() {
  sendButton.disabled =
    !composerEnabled ||
    !hasJoined ||
    socket?.readyState !== WebSocket.OPEN ||
    !messageInput.value.trim();
}

function hideConnectionBanner() {
  connectionBanner.hidden = true;
  connectionBanner.classList.remove("error");
  retryButton.hidden = true;
  connectionDot.classList.remove("offline");
}

function showConnectionBanner(message, isError = false, allowRetry = false) {
  connectionMessage.textContent = message;
  connectionBanner.hidden = false;
  connectionBanner.classList.toggle("error", isError);
  connectionBanner.querySelector(".banner-spinner").hidden = isError;
  retryButton.hidden = !allowRetry;
  connectionDot.classList.add("offline");
}

function isCurrentSocket(event) {
  return event.currentTarget === socket;
}

// ==========================================
// 11. WebSocket 连接与消息调度
// ==========================================

function connect(isReconnect = false) {
  clearTimeout(reconnectTimer);
  intentionalClose = false;
  loginPending = true;

  if (!isReconnect) {
    setLoginLoading(true);
    setLoginError("");
  } else {
    showConnectionBanner("连接已断开，正在重新连接…");
    setComposerEnabled(false);
  }

  const protocol = location.protocol === "https:" ? "wss:" : "ws:";
  const ws = new WebSocket(`${protocol}//${location.host}/ws`);
  socket = ws;

  ws.addEventListener("open", (event) => {
    if (!isCurrentSocket(event)) return;
    if (sessionToken && (isReconnect || currentAuthMode === "token")) {
      ws.send(JSON.stringify({ type: "auth_token", token: sessionToken }));
    } else if (currentAuthMode === "register") {
      ws.send(JSON.stringify({ type: "register", username: nickname, password: currentPassword }));
    } else if (currentPassword) {
      ws.send(JSON.stringify({ type: "login", nickname, password: currentPassword }));
    } else {
      ws.send(JSON.stringify({ type: "login", nickname }));
    }
  });

  ws.addEventListener("message", handleMessage);
  ws.addEventListener("error", (event) => {
    if (!isCurrentSocket(event)) return;
  });
  ws.addEventListener("close", handleClose);
}

function handleMessage(event) {
  if (!isCurrentSocket(event)) return;

  let payload;
  try {
    payload = JSON.parse(event.data);
  } catch {
    appendSystemMessage("收到了一条无法识别的服务器消息", true);
    return;
  }

  if (!payload || typeof payload.type !== "string") return;

  if (
    historyLoading &&
    hasJoined &&
    ["chat", "mention", "group_chat", "voice", "image", "dm"].includes(payload.type)
  ) {
    pendingRealtimePayloads.push(payload);
    return;
  }

  processPayload(payload);
}

function updateClusterNodeBadge(nodeId, port) {
  const badge = document.querySelector("#cluster-node-badge");
  if (!badge) return;
  if (nodeId) {
    badge.textContent = `🌐 ${nodeId}`;
    badge.title = `当前连接的分布式集群节点: ${nodeId}${port ? ` (端口 ${port})` : ""}\n点击查看节点详情`;
    badge.hidden = false;
    badge.onclick = () => {
      alert(`【分布式集群节点】\n• 当前节点 ID: ${nodeId}\n• 端口: ${port || "默认"}\n• 架构: 分布式网格 (Distributed Mesh)\n\n所有节点间公聊、私聊与撤回均已全网实时打通！`);
    };
  } else {
    badge.hidden = true;
  }
}

function processPayload(payload) {
  if (payload.type === "login_success") {
    if (payload.token) {
      sessionToken = payload.token;
      localStorage.setItem("chatroom_token", sessionToken);
    }
    if (payload.nickname) {
      nickname = payload.nickname;
      localStorage.setItem("chatroom_nickname", nickname);
    }
    if (payload.node_id) {
      updateClusterNodeBadge(payload.node_id, payload.port);
    }
    loginPending = false;
    hasJoined = true;
    retryCount = 0;
    setLoginLoading(false);
    historyLoading = !historyLoaded;
    showChatView();
    hideConnectionBanner();
    startStatusPolling();
    if (historyLoading) void loadHistory();
    return;
  }

  if (payload.type === "register_success") {
    setLoginLoading(false);
    if (payload.token) {
      sessionToken = payload.token;
      localStorage.setItem("chatroom_token", sessionToken);
    }
    if (payload.username) {
      nickname = payload.username;
      localStorage.setItem("chatroom_nickname", nickname);
    }
    loginPending = false;
    hasJoined = true;
    retryCount = 0;
    showChatView();
    hideConnectionBanner();
    startStatusPolling();
    appendSystemMessage(`🎉 欢迎新注册用户 ${nickname}！`);
    if (!historyLoaded) void loadHistory();

    if (payload.recovery_key) {
      showRecoveryKeyModal(payload.recovery_key);
    }
    return;
  }

  if (payload.type === "ack") {
    const ackId = payload.msg_id;
    if (ackId) {
      if (pendingAckTimers.has(ackId)) {
        clearTimeout(pendingAckTimers.get(ackId));
        pendingAckTimers.delete(ackId);
      }
      const el = document.querySelector(`.msg-ack-status[data-ack-id="${CSS.escape(ackId)}"]`);
      if (el) {
        if (payload.status === "offline_queued") {
          el.className = "msg-ack-status delivered ack-offline-queued";
          el.textContent = "📬";
          el.title = payload.info || "对方当前离线，已保存为离线留言";
        } else {
          el.className = "msg-ack-status delivered";
          el.textContent = "✔️";
          el.title = "已送达";
        }
      }
    }
    return;
  }

  if (payload.type === "unread_sync") {
    if (payload.counts && typeof payload.counts === "object") {
      for (const [sender, count] of Object.entries(payload.counts)) {
        unreadDmCounts.set(sender, Number(count));
      }
    }
    if (Array.isArray(payload.messages)) {
      for (const msg of payload.messages) {
        if (msg && msg.from) {
          const chKey = `dm:${msg.from}`;
          if (!messageNodesByChannel.has(chKey)) messageNodesByChannel.set(chKey, []);
          const node = createMessageElement(msg, false);
          if (node) messageNodesByChannel.get(chKey).push(node);
        }
      }
    }
    renderOnlineUsers(allContactsList.length ? allContactsList : onlineUsersList);
    if (payload.total > 0) {
      playReceiveSound();
      appendSystemMessage(`📬 您有 ${payload.total} 条未读离线留言`);
    }
    return;
  }

  if (payload.type === "mark_read_ack") {
    if (payload.from) {
      unreadDmCounts.delete(payload.from);
      renderOnlineUsers(allContactsList.length ? allContactsList : onlineUsersList);
    }
    return;
  }

  if (payload.type === "error") {
    const errorMsg = typeof payload.msg === "string" ? payload.msg : "系统提示错误";
    if (loginPending) {
      setLoginLoading(false);
      setLoginError(errorMsg);
      if (errorMsg.includes("凭据") || errorMsg.includes("过期") || errorMsg.includes("密码")) {
        localStorage.removeItem("chatroom_token");
        sessionToken = "";
      }
      return;
    }
    appendSystemMessage(`⚠️ ${errorMsg}`);
    if (errorMsg.includes("移出") || errorMsg.includes("管理员")) {
      intentionalClose = true;
      socket?.close();
      showLoginView(errorMsg);
      alert(errorMsg);
    }
    return;
  }

  if (payload.type === "system") {
    const message = typeof payload.msg === "string" ? payload.msg : "系统通知";
    const isOwnLogin = loginPending && message === `${nickname} 上线了`;
    const ownGroupPrefix = "你已加入群 ";

    if (isOwnLogin) {
      loginPending = false;
      hasJoined = true;
      retryCount = 0;
      setLoginLoading(false);
      historyLoading = !historyLoaded;
      showChatView();
      hideConnectionBanner();
      startStatusPolling();
      if (historyLoading) void loadHistory();
    }

    if (message.startsWith(ownGroupPrefix)) {
      const group = message.slice(ownGroupPrefix.length).trim();
      if (group && (pendingGroupJoins.has(group) || !joinedGroupNames.has(group))) {
        confirmGroupJoin(group);
      }
    }

    appendSystemMessage(message);
    void fetchStatus();
    return;
  }

  if (payload.type === "chat") {
    if (!hasJoined) return;
    const from = typeof payload.from === "string" ? payload.from : "匿名用户";
    const text = typeof payload.text === "string" ? payload.text : "";
    const sticker = typeof payload.sticker === "string" ? payload.sticker : "";
    const ts = Number(payload.ts);
    const msg_id = payload.msg_id || `${ts}_${from}`;
    const reply_to = payload.reply_to;
    const messageKey = `${from}\u0000${text}\u0000${sticker}\u0000${ts}`;
    if (historyMessageKeys.delete(messageKey)) return;

    appendChatMessage({
      from,
      text,
      ts,
      sticker,
      msg_id,
      reply_to,
    });
    if (from !== nickname) {
      playReceiveSound();
      sendDesktopNotification(from, text || (sticker ? `[表情]` : "发来消息"));
    }
    return;
  }

  if (payload.type === "image") {
    if (!hasJoined || typeof payload.url !== "string" || !payload.url) return;
    const from = typeof payload.from === "string" ? payload.from : "匿名用户";
    const group = payload.group || null;
    const dm = payload.to ? (from === nickname ? payload.to : from) : null;
    const msg_id = payload.msg_id || `${payload.ts}_${from}`;
    appendImageMessage({
      from,
      url: payload.url,
      ts: Number(payload.ts),
      msg_id,
      reply_to: payload.reply_to,
      group,
      dm,
    });
    if (from !== nickname) {
      playReceiveSound();
      sendDesktopNotification(from, "[图片]");
    }
    return;
  }

  if (payload.type === "reaction") {
    const { from, msg_id, emoji } = payload;
    if (!msg_id || !emoji) return;

    if (!messageReactions.has(msg_id)) {
      messageReactions.set(msg_id, new Map());
    }
    const msgMap = messageReactions.get(msg_id);
    if (!msgMap.has(emoji)) {
      msgMap.set(emoji, new Set());
    }
    const userSet = msgMap.get(emoji);
    if (userSet.has(from)) {
      userSet.delete(from);
      if (userSet.size === 0) msgMap.delete(emoji);
    } else {
      userSet.add(from);
    }

    renderReactionBadges(msg_id);
    return;
  }

  if (payload.type === "recall") {
    const { msg_id, from } = payload;
    if (msg_id) {
      handleMessageRecall(msg_id, from || "某人");
    }
    return;
  }

  if (payload.type === "typing") {
    if (!hasJoined || payload.from === nickname) return;
    showTypingIndicator(payload.from);
    return;
  }

  if (payload.type === "mention") {
    if (!hasJoined) return;
    const from = typeof payload.from === "string" ? payload.from : "匿名用户";
    const text = typeof payload.text === "string" ? payload.text : "";
    highlightMention({ from, text, ts: Number(payload.ts) });
    playMentionTone();
    startMentionTitleAlert();
    sendDesktopNotification(`🔔 ${from} @了你`, text);
    return;
  }

  if (payload.type === "dm") {
    if (!hasJoined) return;
    const from = typeof payload.from === "string" ? payload.from : "";
    const to = typeof payload.to === "string" ? payload.to : "";
    const peer = from === nickname ? to : from;
    if (!from || !to || !peer || peer === nickname) return;

    appendChatMessage({
      from,
      text: typeof payload.text === "string" ? payload.text : "",
      ts: Number(payload.ts),
      msg_id: payload.msg_id,
      reply_to: payload.reply_to,
      dm: peer,
    });
    if (from !== nickname) {
      playReceiveSound();
      sendDesktopNotification(`🔒 私聊 · ${from}`, typeof payload.text === "string" ? payload.text : "[私聊消息]");
      if (activeDm !== peer) {
        const prev = unreadDmCounts.get(peer) || 0;
        unreadDmCounts.set(peer, prev + 1);
        renderOnlineUsers(allContactsList.length ? allContactsList : onlineUsersList);
        appendSystemMessage(`🔒 来自 ${peer} 的私聊消息，点击查看`, false, () => switchDm(peer));
      } else {
        if (socket && socket.readyState === WebSocket.OPEN) {
          socket.send(JSON.stringify({ type: "mark_read", from: peer }));
        }
      }
    }
    return;
  }

  if (payload.type === "group_chat") {
    if (!hasJoined || typeof payload.group !== "string" || !payload.group.trim()) return;
    const group = payload.group.trim();
    if (!joinedGroupNames.has(group)) {
      joinedGroupNames.add(group);
      renderJoinedGroups();
    }
    const from = typeof payload.from === "string" ? payload.from : "匿名用户";
    appendChatMessage({
      from,
      text: typeof payload.text === "string" ? payload.text : "",
      ts: Number(payload.ts),
      msg_id: payload.msg_id,
      reply_to: payload.reply_to,
      group,
    });
    if (from !== nickname) {
      playReceiveSound();
      sendDesktopNotification(`群 [${group}] · ${from}`, typeof payload.text === "string" ? payload.text : "[群消息]");
      if (activeGroup !== group) {
        appendSystemMessage(`群 ${group} 有新消息，点击查看`, false, () => switchChannel(group));
      }
    }
    return;
  }

  if (payload.type === "voice") {
    if (!hasJoined || typeof payload.url !== "string" || !payload.url) return;
    appendVoiceMessage({
      from: typeof payload.from === "string" ? payload.from : "匿名用户",
      url: payload.url,
      ts: Number(payload.ts),
      group: payload.group || null,
    });
    if (payload.from !== nickname) playReceiveSound();
    return;
  }

  if (payload.type === "error") {
    const message = typeof payload.msg === "string" ? payload.msg : "服务器拒绝了本次操作";

    if (loginPending) {
      intentionalClose = true;
      socket?.close();
      showLoginView(message);
    } else {
      if (pendingGroupJoins.size) {
        pendingGroupJoins.clear();
        setGroupFeedback(message, true);
      }
      appendSystemMessage(message, true);
    }
  }
}

function handleClose(event) {
  if (!isCurrentSocket(event)) return;
  socket = null;

  if (intentionalClose) {
    intentionalClose = false;
    return;
  }

  if (!hasJoined) {
    loginPending = false;
    setLoginLoading(false);
    setLoginError("无法连接聊天室，请确认服务器已启动后重试");
    return;
  }

  stopStatusPolling();
  resetGroupsAfterDisconnect();
  scheduleReconnect();
}

function scheduleReconnect() {
  setComposerEnabled(false);

  if (retryCount >= MAX_RETRIES) {
    showConnectionBanner("重连失败，请检查网络后手动重试", true, true);
    return;
  }

  const delay = Math.min(1000 * 2 ** retryCount, 16_000);
  retryCount += 1;
  showConnectionBanner(`连接已断开，${Math.round(delay / 1000)} 秒后重连…`);
  reconnectTimer = setTimeout(() => connect(true), delay);
}

// ==========================================
// 12. 消息节点构建与渲染
// ==========================================

function appendChatMessage({ from, text, ts, sticker = "", group = null, dm = null, msg_id = null, reply_to = null, history = false }) {
  const emoji = stickerMap.get(sticker) || "";
  if (!text && !emoji) return;
  const effectiveId = msg_id || `${ts}_${from}`;
  const row = document.createElement("article");
  const mine = from === nickname;
  row.className = `message-row${mine ? " mine" : ""}${group ? " group-message" : ""}${dm ? " dm-message" : ""}`;
  row.dataset.messageFrom = from;
  row.dataset.messageText = text;
  row.dataset.messageId = effectiveId;
  row.id = `msg-${effectiveId}`;
  if (Number.isFinite(ts)) row.dataset.messageTs = String(ts);

  const meta = document.createElement("div");
  meta.className = "message-meta";

  // 头像
  const avatar = createAvatarElement(from, "sm");
  meta.append(avatar);

  const author = document.createElement("span");
  author.className = "message-author";
  author.textContent = mine ? "我" : from;

  const time = document.createElement("time");
  setMessageTime(time, ts);

  const bubble = document.createElement("div");
  bubble.className = "message-bubble";
  bubble.classList.toggle("sticker-only", !text && Boolean(emoji));

  // 引用回复
  if (reply_to) {
    renderQuoteBubble(reply_to, bubble);
  }

  if (text) {
    const textNode = document.createElement("div");
    textNode.className = "message-text";
    renderMarkdownContent(text, textNode);
    bubble.append(textNode);
  }

  if (emoji) {
    const stickerNode = document.createElement("span");
    stickerNode.className = text ? "message-sticker inline" : "message-sticker large";
    stickerNode.setAttribute("aria-label", `表情 ${emoji}`);
    stickerNode.textContent = emoji;
    bubble.append(stickerNode);
  }

  if (group) {
    const groupBadge = document.createElement("span");
    groupBadge.className = "group-message-badge";
    groupBadge.textContent = `群 ${group}`;
    meta.prepend(groupBadge);
  }

  if (dm) {
    const dmBadge = document.createElement("span");
    dmBadge.className = "dm-message-badge";
    dmBadge.textContent = "🔒 私聊";
    meta.prepend(dmBadge);
  }

  meta.append(author, time);

  if (mine) {
    const ackStatus = document.createElement("span");
    ackStatus.className = `msg-ack-status ${history ? "delivered" : "pending"}`;
    ackStatus.dataset.ackId = effectiveId;
    ackStatus.textContent = history ? "✔️" : "⏳";
    ackStatus.title = history ? "已送达" : "发送中";
    meta.append(ackStatus);

    if (!history) {
      const timer = setTimeout(() => {
        if (ackStatus.classList.contains("pending")) {
          ackStatus.className = "msg-ack-status failed";
          ackStatus.textContent = "⚠️";
          ackStatus.title = "发送未确认，点击重试";
        }
      }, 5000);
      pendingAckTimers.set(effectiveId, timer);
    }
  }

  // 快捷反应浮层
  const reactionBar = createReactionBar(effectiveId, from, text || emoji);
  row.append(reactionBar);

  // 聚合反应徽章容器
  const badgesContainer = document.createElement("div");
  badgesContainer.className = "reaction-badges";
  badgesContainer.id = `reactions-${effectiveId}`;

  row.append(meta, bubble, badgesContainer);

  const channelKey = dm ? `dm:${dm}` : group ? `group:${group}` : "global";
  if (history) {
    const channelNodes = messageNodesByChannel.get(channelKey) || [];
    channelNodes.unshift(row);
    messageNodesByChannel.set(channelKey, channelNodes);
    return;
  }
  enqueueMessageNode(row, channelKey);
}

function appendImageMessage({ from, url, ts, msg_id = null, reply_to = null, group = null, dm = null, history = false }) {
  const effectiveId = msg_id || `${ts}_${from}`;
  const row = document.createElement("article");
  const mine = from === nickname;
  row.className = `message-row${mine ? " mine" : ""}${group ? " group-message" : ""}${dm ? " dm-message" : ""}`;
  row.dataset.messageFrom = from;
  row.dataset.messageId = effectiveId;
  row.id = `msg-${effectiveId}`;
  if (Number.isFinite(ts)) row.dataset.messageTs = String(ts);

  const meta = document.createElement("div");
  meta.className = "message-meta";

  const avatar = createAvatarElement(from, "sm");
  meta.append(avatar);

  const author = document.createElement("span");
  author.className = "message-author";
  author.textContent = mine ? "我" : from;

  const time = document.createElement("time");
  setMessageTime(time, ts);

  const bubble = document.createElement("div");
  bubble.className = "message-bubble";

  if (reply_to) {
    renderQuoteBubble(reply_to, bubble);
  }

  const imgWrap = document.createElement("div");
  imgWrap.className = "message-image-wrap";
  const img = document.createElement("img");
  img.className = "message-image";
  img.src = url;
  img.alt = "图片消息";
  img.loading = "lazy";
  img.addEventListener("click", () => openLightbox(url));
  imgWrap.append(img);
  bubble.append(imgWrap);

  if (group) {
    const groupBadge = document.createElement("span");
    groupBadge.className = "group-message-badge";
    groupBadge.textContent = `群 ${group}`;
    meta.prepend(groupBadge);
  }

  if (dm) {
    const dmBadge = document.createElement("span");
    dmBadge.className = "dm-message-badge";
    dmBadge.textContent = "🔒 私聊";
    meta.prepend(dmBadge);
  }

  meta.append(author, time);

  const reactionBar = createReactionBar(effectiveId, from, "[图片]");
  row.append(reactionBar);

  const badgesContainer = document.createElement("div");
  badgesContainer.className = "reaction-badges";
  badgesContainer.id = `reactions-${effectiveId}`;

  row.append(meta, bubble, badgesContainer);

  const channelKey = dm ? `dm:${dm}` : group ? `group:${group}` : "global";
  if (history) {
    const channelNodes = messageNodesByChannel.get(channelKey) || [];
    channelNodes.unshift(row);
    messageNodesByChannel.set(channelKey, channelNodes);
    return;
  }
  enqueueMessageNode(row, channelKey);
}

function appendVoiceMessage({ from, url, ts, group = null }) {
  const effectiveId = `${ts}_${from}`;
  const row = document.createElement("article");
  const mine = from === nickname;
  row.className = `message-row voice-message${mine ? " mine" : ""}${group ? " group-message" : ""}`;
  row.dataset.messageId = effectiveId;
  row.id = `msg-${effectiveId}`;

  const meta = document.createElement("div");
  meta.className = "message-meta";

  const avatar = createAvatarElement(from, "sm");
  meta.append(avatar);

  const author = document.createElement("span");
  author.className = "message-author";
  author.textContent = mine ? "我" : from;

  const time = document.createElement("time");
  setMessageTime(time, ts);

  const bubble = document.createElement("div");
  bubble.className = "message-bubble voice-bubble";

  const audio = document.createElement("audio");
  audio.controls = true;
  audio.preload = "none";
  audio.src = url;
  audio.setAttribute("aria-label", `${author.textContent}发送的语音消息`);

  const duration = document.createElement("span");
  duration.className = "voice-duration";
  duration.textContent = "--:--";

  audio.addEventListener("loadedmetadata", () => {
    duration.textContent = formatDuration(audio.duration);
  });
  audio.addEventListener("error", () => {
    duration.textContent = "无法播放";
  });

  if (group) {
    const groupBadge = document.createElement("span");
    groupBadge.className = "group-message-badge";
    groupBadge.textContent = `群 ${group}`;
    meta.prepend(groupBadge);
  }

  meta.append(author, time);
  bubble.append(audio, duration);

  const reactionBar = createReactionBar(effectiveId, from, "[语音]");
  row.append(reactionBar);

  const badgesContainer = document.createElement("div");
  badgesContainer.className = "reaction-badges";
  badgesContainer.id = `reactions-${effectiveId}`;

  row.append(meta, bubble, badgesContainer);
  enqueueMessageNode(row, group ? `group:${group}` : "global");
}

function formatDuration(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return "--:--";
  const totalSeconds = Math.floor(seconds);
  const minutes = Math.floor(totalSeconds / 60);
  const remainingSeconds = totalSeconds % 60;
  return `${String(minutes).padStart(2, "0")}:${String(remainingSeconds).padStart(2, "0")}`;
}

function appendSystemMessage(message, isError = false, onClick = null) {
  if (chatView.hidden) return;

  const item = document.createElement("div");
  item.className = `system-message${isError ? " error" : ""}${onClick ? " actionable" : ""}`;
  const content = document.createElement(onClick ? "button" : "span");
  if (onClick) {
    content.type = "button";
    content.addEventListener("click", onClick);
  }
  content.textContent = message;
  item.append(content);
  enqueueMessageNode(item);
}

function enqueueMessageNode(node, channelKey = getChannelKey()) {
  const channelNodes = messageNodesByChannel.get(channelKey) || [];
  channelNodes.push(node);
  messageNodesByChannel.set(channelKey, channelNodes);
  if (channelKey !== getChannelKey()) return;

  pendingMessageNodes.push({ node, channelKey });
  if (renderFrame !== null) return;
  renderFrame = requestAnimationFrame(flushMessageQueue);
}

function flushMessageQueue() {
  renderFrame = null;
  if (!pendingMessageNodes.length) return;

  const nearBottom = messages.scrollHeight - messages.scrollTop - messages.clientHeight < 96;
  const fragment = document.createDocumentFragment();
  for (const { node, channelKey } of pendingMessageNodes.splice(0)) {
    if (channelKey === getChannelKey()) fragment.append(node);
  }
  messages.querySelector(".empty-state")?.remove();
  messages.append(fragment);

  if (nearBottom) messages.scrollTo({ top: messages.scrollHeight, behavior: "smooth" });
}

function highlightMention({ from, text, ts }) {
  const rows = messages.querySelectorAll(".message-row:not(.mine)");
  for (let index = rows.length - 1; index >= 0; index -= 1) {
    const row = rows[index];
    if (
      row.dataset.messageFrom === from &&
      row.dataset.messageText === text &&
      (!row.dataset.messageTs || row.dataset.messageTs === String(ts))
    ) {
      row.classList.add("mention-highlight");
      return;
    }
  }
}

function resizeMessageInput() {
  messageInput.style.height = "auto";
  const nextHeight = Math.min(messageInput.scrollHeight, MAX_TEXTAREA_HEIGHT);
  messageInput.style.height = `${nextHeight}px`;
  messageInput.style.overflowY = messageInput.scrollHeight > MAX_TEXTAREA_HEIGHT ? "auto" : "hidden";
}

function updateViewportHeight() {
  cancelAnimationFrame(viewportFrame);
  viewportFrame = requestAnimationFrame(() => {
    const height = window.visualViewport?.height || window.innerHeight;
    document.documentElement.style.setProperty("--viewport-height", `${Math.round(height)}px`);
  });
}

// ==========================================
// 13. API 历史与状态请求
// ==========================================

async function loadHistory() {
  try {
    const response = await fetch(`/api/history?limit=${HISTORY_LIMIT}`, {
      method: "GET",
      headers: { Accept: "application/json" },
      cache: "no-store",
    });
    if (!response.ok) throw new Error("history request failed");

    const history = await response.json();
    if (!Array.isArray(history)) throw new Error("invalid history response");
    for (let index = history.length - 1; index >= 0; index -= 1) {
      const item = history[index];
      if (!item || item.type !== "chat" || item.group) continue;
      const from = typeof item.from === "string" ? item.from : "匿名用户";
      const text = typeof item.text === "string" ? item.text : "";
      const sticker = typeof item.sticker === "string" ? item.sticker : "";
      const ts = Number(item.ts);
      const msg_id = item.msg_id || `${ts}_${from}`;
      historyMessageKeys.add(`${from}\u0000${text}\u0000${sticker}\u0000${ts}`);
      appendChatMessage({
        from,
        text,
        ts,
        sticker,
        msg_id,
        reply_to: item.reply_to,
        history: true,
      });
    }
  } catch {
    appendSystemMessage("历史消息加载失败，实时聊天不受影响", true);
  } finally {
    historyLoading = false;
    historyLoaded = true;
    renderCurrentChannel();
    for (const payload of pendingRealtimePayloads.splice(0)) processPayload(payload);
  }
}

function renderOnlineUsers(users) {
  userListContainer.replaceChildren();
  const sortedUsers = [...users].sort((left, right) => {
    const leftOnline = left.online !== false;
    const rightOnline = right.online !== false;
    if (leftOnline !== rightOnline) return leftOnline ? -1 : 1;
    const lName = left.nickname || left.username || "";
    const rName = right.nickname || right.username || "";
    return lName.localeCompare(rName, "zh-CN");
  });

  for (const user of sortedUsers) {
    const userNick = user.nickname || user.username || "";
    if (!userNick) continue;
    const isOnline = user.online !== false;
    const button = document.createElement("button");
    const isSelf = userNick === nickname;
    button.type = "button";
    button.className = "user-item";
    button.dataset.user = userNick;
    button.classList.toggle("active", userNick === activeDm);
    button.disabled = isSelf;
    button.title = isSelf
      ? `${userNick}（我）`
      : `与 ${userNick} 私聊${isOnline ? (user.address ? ` · ${user.address}` : " · 在线") : " · 离线留言"}`;

    const avatar = createAvatarElement(userNick, "sm");
    const dot = document.createElement("span");
    dot.className = `user-status-dot ${isOnline ? "online" : "offline"}`;
    dot.setAttribute("aria-hidden", "true");
    const name = document.createElement("span");
    name.className = "user-item-name";
    name.textContent = isSelf ? `${userNick}（我）` : userNick;
    button.append(avatar, dot, name);

    const unreadCount = unreadDmCounts.get(userNick) || 0;
    if (unreadCount > 0) {
      const badge = document.createElement("span");
      badge.className = "unread-badge";
      badge.textContent = unreadCount > 99 ? "99+" : String(unreadCount);
      button.append(badge);
    }

    if (!isSelf) button.addEventListener("click", () => switchDm(userNick));
    userListContainer.append(button);
  }

  if (!sortedUsers.length) {
    const empty = document.createElement("span");
    empty.className = "user-list-empty";
    empty.textContent = "暂无联系人";
    userListContainer.append(empty);
  }
}

async function fetchOnlineUsers() {
  if (!hasJoined) return;
  try {
    const response = await fetch("/api/users", {
      method: "GET",
      headers: { Accept: "application/json" },
      cache: "no-store",
    });
    if (response.ok) {
      const users = await response.json();
      if (Array.isArray(users) && users.length > 0) {
        allContactsList = users.map((u) => ({
          nickname: u.username,
          online: Boolean(u.online),
        }));
        renderOnlineUsers(allContactsList);
        return;
      }
    }
  } catch {}

  try {
    const response = await fetch("/api/online", {
      method: "GET",
      headers: { Accept: "application/json" },
      cache: "no-store",
    });
    if (!response.ok) throw new Error("online request failed");
    const result = await response.json();
    const online = Array.isArray(result) ? result : result?.oneline;
    if (!Array.isArray(online)) throw new Error("invalid online response");
    onlineUsersList = online.filter((user) => user && typeof user.nickname === "string" && user.nickname);
    renderOnlineUsers(onlineUsersList);
  } catch {}
}

async function fetchStatus() {
  if (!hasJoined) return;

  try {
    const response = await fetch("/api/status", {
      method: "GET",
      headers: { Accept: "application/json" },
      cache: "no-store",
    });
    if (!response.ok) throw new Error("status request failed");

    const status = await response.json();
    const count = Number(status.online_count);
    onlineCount.textContent = Number.isFinite(count) ? `${count} 人在线` : "已连接";
  } catch {
    onlineCount.textContent = socket?.readyState === WebSocket.OPEN ? "已连接" : "连接中断";
  }
}

function startStatusPolling() {
  stopStatusPolling();
  void fetchStatus();
  void fetchOnlineUsers();
  statusTimer = setInterval(() => {
    void fetchStatus();
    void fetchOnlineUsers();
  }, STATUS_INTERVAL_MS);
}

function stopStatusPolling() {
  clearInterval(statusTimer);
  statusTimer = null;
}

function sendCurrentMessage() {
  const rawText = messageInput.value;
  const text = rawText.trim();
  if (!text || socket?.readyState !== WebSocket.OPEN || !hasJoined) return;

  // 检查是否为 Slash 指令
  if (text.startsWith("/")) {
    if (executeSlashCommand(text)) {
      messageInput.value = "";
      resizeMessageInput();
      updateSendButtonState();
      return;
    }
  }

  sendRawChatMessage(text);
  messageInput.value = "";
  resizeMessageInput();
  updateSendButtonState();
  messageInput.focus();
}

function showVoiceFeedback(message, isError = false) {
  clearTimeout(voiceFeedbackTimer);
  voiceFeedback.textContent = message;
  voiceFeedback.classList.toggle("error", isError);
  voiceFeedback.hidden = false;
  voiceFeedbackTimer = setTimeout(() => {
    voiceFeedback.hidden = true;
  }, isError ? 5_000 : 3_000);
}

function setVoiceBusy(busy, label = "🎤 语音") {
  voiceBusy = busy;
  voiceButton.disabled = !composerEnabled || busy || Boolean(activeGroup);
  voiceButton.textContent = label;
}

function setVoiceButtonIdle() {
  voiceButton.classList.remove("recording");
  voiceButton.setAttribute("aria-pressed", "false");
  voiceButton.setAttribute("aria-label", "开始录音");
  setVoiceBusy(false);
}

function getRecordingMimeType() {
  const candidates = [
    "audio/webm;codecs=opus",
    "audio/webm",
    "audio/ogg;codecs=opus",
    "audio/ogg",
  ];
  return candidates.find((type) => MediaRecorder.isTypeSupported(type)) || "";
}

function explainMicrophoneError(error) {
  if (!error) return "无法开启麦克风，请检查录音权限";
  if (error.name === "NotAllowedError" || error.name === "PermissionDeniedError") {
    return "麦克风权限被拒绝，请在浏览器地址栏允许麦克风权限后重试";
  }
  if (error.name === "NotFoundError" || error.name === "DevicesNotFoundError") {
    return "未找到可用麦克风设备，请连接麦克风后重试";
  }
  return `无法录音：${error.message || error.name || "未知错误"}`;
}

async function startVoiceRecording() {
  if (voiceBusy || !composerEnabled || recordingSession || activeGroup) return;
  if (!navigator.mediaDevices?.getUserMedia || typeof MediaRecorder === "undefined") {
    showVoiceFeedback("当前浏览器不支持录音功能", true);
    return;
  }

  const mimeType = getRecordingMimeType();
  if (!mimeType) {
    showVoiceFeedback("当前浏览器不支持录制 WebM 或 Ogg 音频", true);
    return;
  }

  setVoiceBusy(true, "正在开启麦克风…");
  const requestId = ++voiceRequestId;

  let stream;
  try {
    stream = await navigator.mediaDevices.getUserMedia({
      audio: { echoCancellation: true, noiseSuppression: true },
    });
  } catch (error) {
    setVoiceButtonIdle();
    showVoiceFeedback(explainMicrophoneError(error), true);
    return;
  }

  if (requestId !== voiceRequestId || !composerEnabled || activeGroup) {
    stream.getTracks().forEach((track) => track.stop());
    setVoiceButtonIdle();
    return;
  }

  let recorder;
  try {
    recorder = new MediaRecorder(stream, { mimeType });
  } catch (error) {
    stream.getTracks().forEach((track) => track.stop());
    setVoiceButtonIdle();
    showVoiceFeedback(explainMicrophoneError(error), true);
    return;
  }

  const session = {
    recorder,
    stream,
    chunks: [],
    mimeType: recorder.mimeType || mimeType,
    startedAt: performance.now(),
    timer: null,
    discard: false,
  };
  recordingSession = session;

  recorder.addEventListener("dataavailable", (event) => {
    if (event.data.size > 0) session.chunks.push(event.data);
  });
  recorder.addEventListener(
    "stop",
    () => {
      void finishVoiceRecording(session);
    },
    { once: true },
  );
  recorder.addEventListener(
    "error",
    () => {
      session.discard = true;
      showVoiceFeedback("录音过程中发生错误，请重试", true);
      if (recordingSession === session && recorder.state === "recording") {
        stopVoiceRecording(true);
      }
    },
    { once: true },
  );

  recorder.start(250);
  voiceButton.classList.add("recording");
  voiceButton.setAttribute("aria-pressed", "true");
  voiceButton.setAttribute("aria-label", "停止录音");
  setVoiceBusy(false);
  updateRecordingCountdown(session);
  session.timer = setInterval(() => updateRecordingCountdown(session), 250);
}

function updateRecordingCountdown(session) {
  if (recordingSession !== session || session.recorder.state !== "recording") return;
  const remainingMs = Math.max(0, MAX_RECORDING_MS - (performance.now() - session.startedAt));
  const remainingSeconds = Math.ceil(remainingMs / 1000);
  voiceButton.textContent = `录音中… ${formatDuration(remainingSeconds)}`;
  if (remainingMs <= 0) stopVoiceRecording();
}

function stopVoiceRecording(discard = false) {
  const session = recordingSession;
  if (!session) return;
  session.discard ||= discard;
  clearInterval(session.timer);
  voiceButton.classList.remove("recording");
  voiceButton.setAttribute("aria-pressed", "false");
  setVoiceBusy(true, discard ? "正在取消…" : "正在处理…");
  if (session.recorder.state !== "inactive") session.recorder.stop();
}

function cancelVoiceRecording() {
  voiceRequestId += 1;
  if (recordingSession) {
    stopVoiceRecording(true);
  } else {
    setVoiceButtonIdle();
  }
}

async function finishVoiceRecording(session) {
  clearInterval(session.timer);
  session.stream.getTracks().forEach((track) => track.stop());
  if (recordingSession === session) recordingSession = null;

  const elapsedMs = performance.now() - session.startedAt;
  if (session.discard) {
    setVoiceButtonIdle();
    return;
  }
  if (elapsedMs < MIN_RECORDING_MS) {
    setVoiceButtonIdle();
    showVoiceFeedback("录音太短，请至少录制 0.5 秒", true);
    return;
  }

  const contentType = session.mimeType.split(";", 1)[0];
  const audioBlob = new Blob(session.chunks, { type: session.mimeType });
  if (!audioBlob.size) {
    setVoiceButtonIdle();
    showVoiceFeedback("没有录到声音数据，请重试", true);
    return;
  }

  setVoiceBusy(true, "正在上传…");
  try {
    const response = await fetch("/api/upload/voice", {
      method: "POST",
      headers: { "Content-Type": contentType },
      body: audioBlob,
    });
    if (!response.ok) throw new Error(`upload failed: ${response.status}`);

    const result = await response.json();
    if (typeof result.url !== "string" || !result.url) throw new Error("missing voice url");
    if (!hasJoined || socket?.readyState !== WebSocket.OPEN) {
      showVoiceFeedback("连接已断开，语音未发送，请重新录制", true);
      return;
    }

    socket.send(JSON.stringify({ type: "voice_send", url: result.url }));
    showVoiceFeedback("语音已发送");
    playSendSound();
  } catch {
    showVoiceFeedback("语音上传失败，未发送，请稍后重试", true);
  } finally {
    setVoiceButtonIdle();
  }
}

// ==========================================
// 14. 事件监听与绑定
// ==========================================

function setAuthMode(mode) {
  currentAuthMode = mode;
  tabLogin?.classList.toggle("active", mode === "login");
  tabRegister?.classList.toggle("active", mode === "register");
  tabGuest?.classList.toggle("active", mode === "guest");

  if (mode === "guest") {
    passwordGroup?.classList.add("hidden");
    if (nicknameLabel) nicknameLabel.textContent = "游客昵称";
    if (loginButtonText) loginButtonText.textContent = "游客体验进入";
    nicknameInput.placeholder = "例如：体验者007";
  } else if (mode === "register") {
    passwordGroup?.classList.remove("hidden");
    if (nicknameLabel) nicknameLabel.textContent = "注册用户名";
    if (loginButtonText) loginButtonText.textContent = "立即注册账号";
    nicknameInput.placeholder = "例如：极客小明";
  } else {
    passwordGroup?.classList.remove("hidden");
    if (nicknameLabel) nicknameLabel.textContent = "用户名 / 账号";
    if (loginButtonText) loginButtonText.textContent = "安全登录进入";
    nicknameInput.placeholder = "例如：张三";
  }
  setLoginError("");
}

tabLogin?.addEventListener("click", () => setAuthMode("login"));
tabRegister?.addEventListener("click", () => setAuthMode("register"));
tabGuest?.addEventListener("click", () => setAuthMode("guest"));

loginForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const value = nicknameInput.value.trim();

  if (!value) {
    setLoginError("请输入用户名后再进入聊天室");
    nicknameInput.focus();
    return;
  }

  const pass = passwordInput ? passwordInput.value : "";
  if (currentAuthMode === "register") {
    if (!pass || pass.length < 3) {
      setLoginError("注册密码至少需要 3 个字符");
      passwordInput?.focus();
      return;
    }
  } else if (currentAuthMode === "login") {
    if (!pass) {
      setLoginError("请输入密码（游客请点击上方「游客体验」）");
      passwordInput?.focus();
      return;
    }
  }

  nickname = value;
  currentPassword = pass;
  retryCount = 0;
  connect(false);
});

nicknameInput.addEventListener("input", () => {
  if (loginError.textContent) setLoginError("");
});
if (passwordInput) {
  passwordInput.addEventListener("input", () => {
    if (loginError.textContent) setLoginError("");
  });
}

if (logoutButton) {
  logoutButton.addEventListener("click", () => {
    localStorage.removeItem("chatroom_token");
    sessionToken = "";
    intentionalClose = true;
    socket?.close();
    showLoginView("已安全退出登录");
  });
}

themeToggle.addEventListener("click", () => {
  const nextTheme = document.documentElement.classList.contains("dark") ? "light" : "dark";
  applyTheme(nextTheme, true);
});

if (soundToggle) {
  soundToggle.addEventListener("click", toggleSound);
}

if (cancelReplyButton) {
  cancelReplyButton.addEventListener("click", clearReplyTo);
}

if (imageButton && imageFileInput) {
  imageButton.addEventListener("click", () => {
    if (imageButton.disabled) return;
    imageFileInput.click();
  });
  imageFileInput.addEventListener("change", (e) => {
    const file = e.target.files?.[0];
    if (file) {
      void uploadAndSendImage(file);
      imageFileInput.value = "";
    }
  });
}

if (lightboxClose) {
  lightboxClose.addEventListener("click", closeLightbox);
}
if (imageLightbox) {
  imageLightbox.querySelector(".lightbox-backdrop")?.addEventListener("click", closeLightbox);
}

// 粘贴剪贴板图片支持 (Ctrl+V)
window.addEventListener("paste", (event) => {
  if (!hasJoined || socket?.readyState !== WebSocket.OPEN) return;
  const items = event.clipboardData?.items;
  if (!items) return;

  for (let i = 0; i < items.length; i++) {
    const item = items[i];
    if (item.type.startsWith("image/")) {
      const file = item.getAsFile();
      if (file) {
        event.preventDefault();
        void uploadAndSendImage(file);
        break;
      }
    }
  }
});

// 拖拽图片支持
window.addEventListener("dragover", (e) => {
  e.preventDefault();
});
window.addEventListener("drop", (e) => {
  e.preventDefault();
  if (!hasJoined || socket?.readyState !== WebSocket.OPEN) return;
  const file = e.dataTransfer?.files?.[0];
  if (file) {
    const mime = getMimeTypeForImage(file);
    if (mime.startsWith("image/")) {
      void uploadAndSendImage(file);
    }
  }
});

darkModeQuery.addEventListener?.("change", (event) => {
  if (!storedTheme()) applyTheme(event.matches ? "dark" : "light");
});

groupToggleButton.addEventListener("click", () => {
  const willOpen = groupPanel.hidden;
  groupPanel.hidden = !willOpen;
  groupToggleButton.setAttribute("aria-expanded", String(willOpen));
  if (willOpen && composerEnabled) groupNameInput.focus();
});

globalChannelButton.addEventListener("click", () => switchChannel());
globalUserItem.addEventListener("click", () => switchChannel());

groupForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const group = groupNameInput.value.trim();
  if (!group) {
    setGroupFeedback("请输入群名后再加入", true);
    groupNameInput.focus();
    return;
  }
  if (socket?.readyState !== WebSocket.OPEN || !hasJoined) {
    setGroupFeedback("连接尚未就绪，请稍后重试", true);
    return;
  }
  if (joinedGroupNames.has(group)) {
    setGroupFeedback(`你已经在群 ${group} 中`);
    switchChannel(group);
    return;
  }

  pendingGroupJoins.add(group);
  socket.send(JSON.stringify({ type: "join_group", name: group }));
  setGroupFeedback(`正在加入群 ${group}…`);
});

groupNameInput.addEventListener("input", () => {
  if (groupFeedback.textContent) setGroupFeedback("");
});

stickerButton.addEventListener("click", () => {
  if (stickerButton.disabled) return;
  const willOpen = stickerPanel.hidden;
  stickerPanel.hidden = !willOpen;
  stickerButton.setAttribute("aria-expanded", String(willOpen));
  if (willOpen) stickerPanel.querySelector("button")?.focus();
});

document.addEventListener("click", (event) => {
  if (!event.target.closest(".sticker-wrap")) closeStickerPanel();
});

document.addEventListener("keydown", (event) => {
  if (event.key === "Escape") {
    if (!stickerPanel.hidden) {
      closeStickerPanel();
      stickerButton.focus();
    }
    if (imageLightbox && !imageLightbox.hidden) {
      closeLightbox();
    }
  }
});

messageForm.addEventListener("submit", (event) => {
  event.preventDefault();
  sendCurrentMessage();
});

messageInput.addEventListener("input", () => {
  resizeMessageInput();
  updateSendButtonState();
  notifyTyping();
});

messageInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
    event.preventDefault();
    sendCurrentMessage();
  }
});

voiceButton.addEventListener("click", () => {
  if (recordingSession?.recorder.state === "recording") {
    stopVoiceRecording();
  } else {
    void startVoiceRecording();
  }
});

retryButton.addEventListener("click", () => {
  retryCount = 0;
  connect(true);
});

window.visualViewport?.addEventListener("resize", updateViewportHeight);
window.visualViewport?.addEventListener("scroll", updateViewportHeight);
window.addEventListener("resize", updateViewportHeight);
window.addEventListener("focus", () => {
  if (!activeGroup) stopMentionTitleAlert();
});

if (notificationToggle) {
  notificationToggle.addEventListener("click", () => {
    void toggleDesktopNotification();
  });
}

window.addEventListener("beforeinstallprompt", (e) => {
  e.preventDefault();
  deferredInstallPrompt = e;
  if (pwaInstallButton) {
    pwaInstallButton.hidden = false;
  }
});

if (pwaInstallButton) {
  pwaInstallButton.addEventListener("click", async () => {
    if (deferredInstallPrompt) {
      deferredInstallPrompt.prompt();
      const choice = await deferredInstallPrompt.userChoice;
      if (choice.outcome === "accepted") {
        pwaInstallButton.hidden = true;
      }
      deferredInstallPrompt = null;
    }
  });
}

window.addEventListener("appinstalled", () => {
  if (pwaInstallButton) pwaInstallButton.hidden = true;
  deferredInstallPrompt = null;
  appendSystemMessage("🎉 桌面应用安装成功！已添加至系统开始菜单与桌面");
});

if ("serviceWorker" in navigator) {
  window.addEventListener("load", () => {
    navigator.serviceWorker.register("/sw.js").catch(() => {});
  });
}

initializeTheme();
updateSoundButton();
updateNotificationButton();
resizeMessageInput();
updateSendButtonState();
updateViewportHeight();
renderJoinedGroups();
renderCurrentChannel();
renderStickerPanel(FALLBACK_STICKERS);
void loadStickers();

// 如果本地保存有已认证的 Token 与昵称，尝试自动恢复会话
if (sessionToken && localStorage.getItem("chatroom_nickname")) {
  nickname = localStorage.getItem("chatroom_nickname");
  currentAuthMode = "token";
  connect(false);
}

// ==========================================
// Phase 3: 全文搜索逻辑与快捷键绑定
// ==========================================
function escapeSearchHtml(str) {
  return String(str || "").replace(/[&<>"']/g, (m) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;"
  }[m]));
}

const searchToggle = document.getElementById("search-toggle");
const searchDrawer = document.getElementById("search-drawer");
const closeSearchDrawer = document.getElementById("close-search-drawer");
const searchInput = document.getElementById("search-input");
const searchActionBtn = document.getElementById("search-action-btn");
const searchResultsList = document.getElementById("search-results-list");

function toggleSearchDrawer(open) {
  if (!searchDrawer) return;
  const shouldOpen = open !== undefined ? open : searchDrawer.hidden;
  searchDrawer.hidden = !shouldOpen;
  if (shouldOpen) {
    searchInput?.focus();
    searchInput?.select();
  }
}

async function performSearch() {
  if (!searchInput || !searchResultsList) return;
  const q = searchInput.value.trim();
  if (!q) {
    searchResultsList.innerHTML = '<div class="search-placeholder">请输入搜索关键词</div>';
    return;
  }
  searchResultsList.innerHTML = '<div class="search-placeholder">正在检索全量消息库...</div>';
  try {
    const res = await fetch(`/api/search?q=${encodeURIComponent(q)}&limit=50`);
    if (!res.ok) throw new Error("搜索服务响应失败");
    const data = await res.json();
    const results = data.results || [];
    if (results.length === 0) {
      searchResultsList.innerHTML = `<div class="search-placeholder">未找到包含 "${escapeSearchHtml(q)}" 的历史消息</div>`;
      return;
    }
    searchResultsList.replaceChildren();
    for (const item of results) {
      const card = document.createElement("div");
      card.className = "search-result-card";

      const header = document.createElement("div");
      header.className = "search-result-header";

      const user = document.createElement("span");
      user.className = "search-result-user";
      user.textContent = item.from || "用户";

      const chBadge = document.createElement("span");
      chBadge.className = "search-result-channel";
      chBadge.textContent = item.group ? `群 ${item.group}` : (item.to ? "私聊" : "全局");

      const time = document.createElement("span");
      time.className = "search-result-time";
      time.textContent = item.ts ? new Date(item.ts).toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit" }) : "";

      header.append(user, chBadge, time);

      const text = document.createElement("div");
      text.className = "search-result-text";
      const rawText = item.text || (item.url ? "[富媒体图片/语音]" : "");

      // 关键词高亮
      try {
        const regex = new RegExp(`(${q.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")})`, "gi");
        text.innerHTML = escapeSearchHtml(rawText).replace(regex, '<span class="search-highlight">$1</span>');
      } catch {
        text.textContent = rawText;
      }

      card.append(header, text);

      card.addEventListener("click", () => {
        if (item.group) {
          switchChannel(item.group);
        } else if (item.to) {
          const peer = item.from === nickname ? item.to : item.from;
          switchDm(peer);
        } else {
          switchChannel(null);
        }
        toggleSearchDrawer(false);
      });

      searchResultsList.append(card);
    }
  } catch (err) {
    searchResultsList.innerHTML = `<div class="search-placeholder" style="color:var(--danger, #ef4444)">检索出错: ${escapeSearchHtml(err.message)}</div>`;
  }
}

if (searchToggle) {
  searchToggle.addEventListener("click", () => toggleSearchDrawer());
}
if (closeSearchDrawer) {
  closeSearchDrawer.addEventListener("click", () => toggleSearchDrawer(false));
}
if (searchActionBtn) {
  searchActionBtn.addEventListener("click", performSearch);
}
if (searchInput) {
  searchInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      performSearch();
    }
  });
}

// 快捷键 Ctrl+F / Cmd+F 切换搜索抽屉，Esc 关闭抽屉
window.addEventListener("keydown", (e) => {
  if ((e.ctrlKey || e.metaKey) && (e.key === "f" || e.key === "F")) {
    e.preventDefault();
    toggleSearchDrawer(true);
  } else if (e.key === "Escape") {
    if (searchDrawer && !searchDrawer.hidden) toggleSearchDrawer(false);
    if (resetPasswordModal && !resetPasswordModal.hidden) resetPasswordModal.hidden = true;
    if (recoveryKeyModal && !recoveryKeyModal.hidden) recoveryKeyModal.hidden = true;
  }
});

// ==========================================
// 密保恢复码与自主重置密码逻辑
// ==========================================

function showRecoveryKeyModal(key) {
  if (!recoveryKeyModal || !displayRecoveryKey) return;
  displayRecoveryKey.textContent = key;
  if (copyRecoveryFeedback) copyRecoveryFeedback.hidden = true;
  recoveryKeyModal.hidden = false;
}

if (copyRecoveryKeyBtn && displayRecoveryKey) {
  copyRecoveryKeyBtn.addEventListener("click", async () => {
    const text = displayRecoveryKey.textContent;
    if (!text) return;
    try {
      await navigator.clipboard.writeText(text);
      if (copyRecoveryFeedback) copyRecoveryFeedback.hidden = false;
      copyRecoveryKeyBtn.textContent = "✓ 已复制";
      setTimeout(() => {
        if (copyRecoveryKeyBtn) copyRecoveryKeyBtn.textContent = "📋 复制恢复码";
      }, 2500);
    } catch {
      // Fallback
      prompt("请手动复制您的安全恢复码：", text);
    }
  });
}

if (closeRecoveryModalBtn) {
  closeRecoveryModalBtn.addEventListener("click", () => {
    if (recoveryKeyModal) recoveryKeyModal.hidden = true;
  });
}

if (forgotPasswordLink) {
  forgotPasswordLink.addEventListener("click", () => {
    if (!resetPasswordModal) return;
    if (resetPasswordError) resetPasswordError.hidden = true;
    if (resetPasswordSuccess) resetPasswordSuccess.hidden = true;
    if (resetKeyInput) resetKeyInput.value = "";
    if (resetNewpassInput) resetNewpassInput.value = "";

    const currentNick = nicknameInput ? nicknameInput.value.trim() : "";
    if (resetUsernameInput) {
      resetUsernameInput.value = currentNick;
      if (currentNick && resetKeyInput) {
        setTimeout(() => resetKeyInput.focus(), 100);
      } else {
        setTimeout(() => resetUsernameInput.focus(), 100);
      }
    }
    resetPasswordModal.hidden = false;
  });
}

if (cancelResetBtn && resetPasswordModal) {
  cancelResetBtn.addEventListener("click", () => {
    resetPasswordModal.hidden = true;
  });
}

if (resetPasswordForm) {
  resetPasswordForm.addEventListener("submit", async (e) => {
    e.preventDefault();
    const username = resetUsernameInput ? resetUsernameInput.value.trim() : "";
    const recoveryKey = resetKeyInput ? resetKeyInput.value.trim() : "";
    const newPassword = resetNewpassInput ? resetNewpassInput.value : "";

    if (resetPasswordError) resetPasswordError.hidden = true;
    if (resetPasswordSuccess) resetPasswordSuccess.hidden = true;

    if (!username) {
      if (resetPasswordError) {
        resetPasswordError.textContent = "请输入要重置的用户名";
        resetPasswordError.hidden = false;
      }
      resetUsernameInput?.focus();
      return;
    }
    if (!recoveryKey) {
      if (resetPasswordError) {
        resetPasswordError.textContent = "请输入您的密保恢复码";
        resetPasswordError.hidden = false;
      }
      resetKeyInput?.focus();
      return;
    }
    if (!newPassword || newPassword.length < 3) {
      if (resetPasswordError) {
        resetPasswordError.textContent = "新密码长度至少需要 3 个字符";
        resetPasswordError.hidden = false;
      }
      resetNewpassInput?.focus();
      return;
    }

    if (submitResetBtn) submitResetBtn.disabled = true;

    try {
      const res = await fetch("/api/auth/reset_password", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          username,
          recovery_key: recoveryKey,
          new_password: newPassword,
        }),
      });
      const data = await res.json();
      if (!res.ok || data.error) {
        throw new Error(data.error || `HTTP ${res.status}`);
      }

      if (resetPasswordSuccess) {
        resetPasswordSuccess.textContent = "✓ 密码重置成功！正在返回登录界面…";
        resetPasswordSuccess.hidden = false;
      }

      setTimeout(() => {
        if (resetPasswordModal) resetPasswordModal.hidden = true;
        setAuthMode("login");
        if (nicknameInput) nicknameInput.value = username;
        if (passwordInput) {
          passwordInput.value = "";
          passwordInput.focus();
        }
        setLoginError("密码已成功重置，请输入新密码登录");
        loginError.style.color = "var(--success, #22c55e)";
        setTimeout(() => {
          loginError.style.color = "";
        }, 5000);
      }, 1400);
    } catch (err) {
      if (resetPasswordError) {
        resetPasswordError.textContent = `重置失败: ${err.message}`;
        resetPasswordError.hidden = false;
      }
    } finally {
      if (submitResetBtn) submitResetBtn.disabled = false;
    }
  });
}



