"use strict";

const REQUEST_TIMEOUT_MS = 15_000;

const currentTime = document.querySelector("#current-time");
const refreshState = document.querySelector("#refresh-state");
const refreshButton = document.querySelector("#refresh-button");
const refreshIntervalSelect = document.querySelector("#refresh-interval-select");
const lastRefresh = document.querySelector("#last-refresh");
const nextRefresh = document.querySelector("#next-refresh");
const refreshCount = document.querySelector("#refresh-count");
const errorBanner = document.querySelector("#error-banner");
const onlineStat = document.querySelector("#online-stat");
const messageStat = document.querySelector("#message-stat");
const uptimeStat = document.querySelector("#uptime-stat");
const userCount = document.querySelector("#user-count");
const usersBody = document.querySelector("#users-body");
const accountCountPill = document.querySelector("#account-count-pill");
const accountsBody = document.querySelector("#accounts-body");
const nicknameFilter = document.querySelector("#nickname-filter");
const clearFilter = document.querySelector("#clear-filter");
const messageCount = document.querySelector("#message-count");
const adminMessages = document.querySelector("#admin-messages");
const clusterModeBadge = document.querySelector("#cluster-mode-badge");
const clusterNodesBody = document.querySelector("#cluster-nodes-body");

const broadcastForm = document.querySelector("#broadcast-form");
const broadcastInput = document.querySelector("#broadcast-input");
const broadcastButton = document.querySelector("#broadcast-button");
const broadcastFeedback = document.querySelector("#broadcast-feedback");
const exportJsonBtn = document.querySelector("#export-json-btn");
const exportCsvBtn = document.querySelector("#export-csv-btn");

let refreshIntervalMs = 30 * 60 * 1000;
let stats = null;
let loadedMessages = [];
let completedRefreshes = 0;
let nextRefreshAt = Date.now() + refreshIntervalMs;
let autoRefreshTimer = null;
let clockTimer = null;
let isRefreshing = false;

function formatClock(date) {
  return date.toLocaleTimeString("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  });
}

function normalizeTimestamp(value) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric) || numeric <= 0) return null;
  const timestamp = numeric < 1_000_000_000_000 ? numeric * 1000 : numeric;
  return Number.isNaN(new Date(timestamp).getTime()) ? null : timestamp;
}

function formatDateTime(value) {
  const timestamp = normalizeTimestamp(value);
  if (!timestamp) return "未知";
  const date = new Date(timestamp);
  return Number.isNaN(date.getTime()) ? "未知" : date.toLocaleString("zh-CN", { hour12: false });
}

function formatUptime(value) {
  let seconds = Math.max(0, Math.floor(Number(value) / 1000));
  if (!Number.isFinite(seconds)) return "--";

  const days = Math.floor(seconds / 86_400);
  seconds %= 86_400;
  const hours = Math.floor(seconds / 3600);
  seconds %= 3600;
  const minutes = Math.floor(seconds / 60);
  seconds %= 60;

  const parts = [];
  if (days) parts.push(`${days}天`);
  if (days || hours) parts.push(`${hours}时`);
  if (days || hours || minutes) parts.push(`${minutes}分`);
  parts.push(`${seconds}秒`);
  return parts.join(" ");
}

function updateClockAndCountdown() {
  const now = new Date();
  currentTime.textContent = now.toLocaleString("zh-CN", { hour12: false });
  currentTime.dateTime = now.toISOString();

  if (refreshIntervalMs <= 0) {
    nextRefresh.textContent = "手动";
    return;
  }

  const remaining = Math.max(0, nextRefreshAt - Date.now());
  const totalSeconds = Math.ceil(remaining / 1000);
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  nextRefresh.textContent = `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

function setRefreshState(message, stateClass = "") {
  refreshState.textContent = message;
  refreshState.className = `refresh-state${stateClass ? ` ${stateClass}` : ""}`;
}

function showErrors(errors) {
  if (!errors.length) {
    errorBanner.hidden = true;
    errorBanner.textContent = "";
    return;
  }
  errorBanner.textContent = errors.join("；");
  errorBanner.hidden = false;
}

async function fetchJson(url, options = {}) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  try {
    const response = await fetch(url, {
      ...options,
      headers: {
        Accept: "application/json",
        ...(options.headers || {}),
      },
      cache: "no-store",
      signal: controller.signal,
    });
    if (!response.ok) {
      let errText = `HTTP ${response.status}`;
      try {
        const errJson = await response.json();
        if (errJson.error) errText = errJson.error;
      } catch {}
      throw new Error(errText);
    }
    return await response.json();
  } finally {
    clearTimeout(timer);
  }
}

function renderStats() {
  onlineStat.textContent = String(stats?.users?.length ?? "--");
  messageStat.textContent = Number.isFinite(Number(stats?.message_count))
    ? Number(stats.message_count).toLocaleString("zh-CN")
    : "--";
  uptimeStat.textContent = formatUptime(stats?.server_uptime_ms);
  userCount.textContent = `${stats?.users?.length ?? 0} 位用户`;
  renderUsers(Array.isArray(stats?.users) ? stats.users : []);
}

function renderUsers(users) {
  const fragment = document.createDocumentFragment();

  if (!users.length) {
    const row = document.createElement("tr");
    row.className = "placeholder-row";
    const cell = document.createElement("td");
    cell.colSpan = 5;
    cell.textContent = "暂无在线用户";
    row.append(cell);
    fragment.append(row);
  } else {
    for (const user of users) {
      const row = document.createElement("tr");
      const name = document.createElement("td");
      const address = document.createElement("td");
      const connected = document.createElement("td");
      const status = document.createElement("td");
      const action = document.createElement("td");
      const badge = document.createElement("span");

      const nick = typeof user?.nickname === "string" ? user.nickname : "匿名用户";
      name.className = "user-name";
      address.className = "user-address";
      name.textContent = nick;
      address.textContent = typeof user?.address === "string" ? user.address : "--";
      connected.textContent = formatDateTime(user?.connect_ts);
      badge.className = `status-badge${user?.online === true ? "" : " offline"}`;
      badge.textContent = user?.online === true ? "在线" : "离线";
      status.append(badge);

      if (user?.online === true) {
        const kickBtn = document.createElement("button");
        kickBtn.className = "kick-btn";
        kickBtn.type = "button";
        kickBtn.textContent = "移出";
        kickBtn.title = `将 ${nick} 移出聊天室`;
        kickBtn.addEventListener("click", () => kickUser(nick));
        action.append(kickBtn);
      } else {
        action.textContent = "--";
      }

      row.append(name, address, connected, status, action);
      fragment.append(row);
    }
  }

  usersBody.replaceChildren(fragment);
}

async function kickUser(nick) {
  if (!confirm(`确定要将用户 "${nick}" 移出聊天室吗？`)) return;
  try {
    await fetchJson("/api/admin/kick", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ nickname: nick }),
    });
    alert(`已将用户 "${nick}" 移出聊天室`);
    void refreshData();
  } catch (err) {
    alert(`移出失败: ${err.message}`);
  }
}

function renderAccounts(accounts) {
  if (!accountsBody) return;
  const fragment = document.createDocumentFragment();

  if (!Array.isArray(accounts) || !accounts.length) {
    const row = document.createElement("tr");
    row.className = "placeholder-row";
    const cell = document.createElement("td");
    cell.colSpan = 6;
    cell.textContent = "暂无注册账号";
    row.append(cell);
    fragment.append(row);
  } else {
    for (const acc of accounts) {
      const row = document.createElement("tr");
      const name = document.createElement("td");
      const created = document.createElement("td");
      const lastLogin = document.createElement("td");
      const status = document.createElement("td");
      const recoveryKeyCell = document.createElement("td");
      const action = document.createElement("td");
      const badge = document.createElement("span");

      const username = typeof acc?.username === "string" ? acc.username : "未知";
      name.className = "user-name";
      name.textContent = username;
      created.textContent = formatDateTime(acc?.created_at);
      lastLogin.textContent = formatDateTime(acc?.last_login);

      const isOnline = acc?.online === true;
      badge.className = `status-badge${isOnline ? "" : " offline"}`;
      badge.textContent = isOnline ? "在线" : "离线";
      status.append(badge);

      const recKey = typeof acc?.recovery_key === "string" && acc.recovery_key ? acc.recovery_key : "--";
      const codeEl = document.createElement("code");
      codeEl.style.cssText = "font-family:monospace;font-size:0.82rem;background:#f1f5f9;padding:2px 6px;border-radius:4px;color:#475569;";
      codeEl.textContent = recKey;
      recoveryKeyCell.append(codeEl);

      const actionGroup = document.createElement("div");
      actionGroup.className = "action-btn-group";

      const resetBtn = document.createElement("button");
      resetBtn.className = "action-btn reset-btn";
      resetBtn.type = "button";
      resetBtn.textContent = "🔑 重置密码";
      resetBtn.title = `重置 ${username} 的登录密码`;
      resetBtn.addEventListener("click", () => resetPassword(username));

      const delBtn = document.createElement("button");
      delBtn.className = "action-btn del-btn";
      delBtn.type = "button";
      delBtn.textContent = "🗑 删除";
      delBtn.title = `注销 ${username} 的账号`;
      delBtn.addEventListener("click", () => deleteUser(username));

      actionGroup.append(resetBtn, delBtn);
      action.append(actionGroup);

      row.append(name, created, lastLogin, status, recoveryKeyCell, action);
      fragment.append(row);
    }
  }

  accountsBody.replaceChildren(fragment);
  if (accountCountPill) {
    accountCountPill.textContent = `${Array.isArray(accounts) ? accounts.length : 0} 个账号`;
  }
}

function renderClusterNodes(clusterData) {
  if (!clusterNodesBody) return;
  if (!clusterData || !Array.isArray(clusterData.nodes)) {
    clusterNodesBody.innerHTML = '<tr class="placeholder-row"><td colspan="6">获取集群拓扑失败</td></tr>';
    return;
  }

  const nodes = clusterData.nodes;
  const isClustered = clusterData.clustered === true || nodes.length > 1;
  const selfId = clusterData.self_node_id || "";

  if (clusterModeBadge) {
    if (isClustered) {
      clusterModeBadge.className = "count-pill cluster-mesh-active";
      clusterModeBadge.textContent = `⚡ 集群网格活跃 (${nodes.length} 节点)`;
    } else {
      clusterModeBadge.className = "count-pill";
      clusterModeBadge.textContent = "单机独立模式";
    }
  }

  const fragment = document.createDocumentFragment();
  for (const node of nodes) {
    const row = document.createElement("tr");
    const isSelf = node.node_id === selfId;

    const idCell = document.createElement("td");
    idCell.className = "user-name";
    idCell.innerHTML = `<strong>${node.node_id || "--"}</strong>${isSelf ? ' <span class="self-tag">(当前节点)</span>' : ""}`;

    const urlCell = document.createElement("td");
    urlCell.textContent = node.base_url || `${node.host}:${node.port}`;

    const statusCell = document.createElement("td");
    const badge = document.createElement("span");
    const isAlive = node.alive === true;
    badge.className = `status-badge${isAlive ? "" : " offline"}`;
    badge.textContent = isAlive ? "🟢 在线" : "🔴 离线";
    statusCell.append(badge);

    const latencyCell = document.createElement("td");
    latencyCell.textContent = isAlive ? `${node.latency_ms} ms` : "--";

    const usersCell = document.createElement("td");
    usersCell.textContent = `${node.online_users || 0} 人`;

    const heartbeatCell = document.createElement("td");
    heartbeatCell.textContent = node.last_heartbeat_ms ? formatDateTime(node.last_heartbeat_ms) : "--";

    row.append(idCell, urlCell, statusCell, latencyCell, usersCell, heartbeatCell);
    fragment.append(row);
  }

  clusterNodesBody.replaceChildren(fragment);
}

async function resetPassword(username) {
  const newPass = prompt(`请输入用户 "${username}" 的新登录密码：`, "123456");
  if (!newPass || !newPass.trim()) return;
  try {
    await fetchJson("/api/admin/reset_password", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username, new_password: newPass.trim() }),
    });
    alert(`✓ 用户 "${username}" 密码重置成功！新密码为：${newPass.trim()}`);
    void refreshData();
  } catch (err) {
    alert(`重置密码失败: ${err.message}`);
  }
}

async function deleteUser(username) {
  if (!confirm(`⚠️ 确定要彻底注销并删除账号 "${username}" 吗？此操作无法撤销。`)) return;
  try {
    await fetchJson("/api/admin/delete_user", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username }),
    });
    alert(`✓ 账号 "${username}" 已成功删除！`);
    void refreshData();
  } catch (err) {
    alert(`删除失败: ${err.message}`);
  }
}

function renderMessages() {
  const query = nicknameFilter.value.trim().toLocaleLowerCase("zh-CN");
  const filtered = query
    ? loadedMessages.filter((message) => String(message?.from ?? "").toLocaleLowerCase("zh-CN").includes(query))
    : loadedMessages;
  const fragment = document.createDocumentFragment();

  if (!filtered.length) {
    const empty = document.createElement("div");
    empty.className = "empty-list";
    empty.textContent = query ? "没有匹配该昵称的消息" : "暂无消息数据";
    fragment.append(empty);
  } else {
    for (const message of filtered) {
      const entry = document.createElement("article");
      const author = document.createElement("div");
      const content = document.createElement("div");
      const time = document.createElement("time");

      entry.className = "message-entry";
      author.className = "message-author";
      content.className = "message-text";
      time.className = "message-time";
      author.textContent = typeof message?.from === "string" ? message.from : "匿名用户";

      if (message?.sticker) {
        const sticker = document.createElement("span");
        sticker.className = "message-sticker";
        sticker.textContent = String(message.sticker);
        content.append(sticker);
      }
      if (message?.url) {
        const mediaNote = document.createElement("span");
        mediaNote.className = "message-media-tag";
        mediaNote.textContent = message.type === "voice" ? " [语音消息]" : " [图片消息]";
        content.append(mediaNote);
      }
      content.append(document.createTextNode(typeof message?.text === "string" ? message.text : ""));

      const timestamp = normalizeTimestamp(message?.ts);
      time.textContent = formatDateTime(message?.ts);
      if (timestamp) time.dateTime = new Date(timestamp).toISOString();
      entry.append(author, content, time);
      fragment.append(entry);
    }
  }

  adminMessages.replaceChildren(fragment);
  const total = Number(stats?.message_count);
  const serverTotal = Number.isFinite(total) ? `，服务器总计 ${total.toLocaleString("zh-CN")} 条` : "";
  messageCount.textContent = query
    ? `显示 ${filtered.length} / 已加载 ${loadedMessages.length} 条${serverTotal}`
    : `已加载 ${loadedMessages.length} 条${serverTotal}`;
  clearFilter.hidden = !nicknameFilter.value;
}

async function refreshData() {
  if (isRefreshing) return;
  isRefreshing = true;
  refreshButton.disabled = true;
  setRefreshState("正在刷新…", "loading");

  try {
    const [statsResult, messagesResult, accountsResult, clusterResult] = await Promise.allSettled([
      fetchJson("/api/admin/stats"),
      fetchJson("/api/admin/messages?limit=200"),
      fetchJson("/api/users"),
      fetchJson("/api/cluster/nodes"),
    ]);
    const errors = [];

    if (statsResult.status === "fulfilled" && statsResult.value && Array.isArray(statsResult.value.users)) {
      stats = statsResult.value;
      renderStats();
    } else {
      errors.push("用户与统计数据刷新失败");
    }

    if (messagesResult.status === "fulfilled" && Array.isArray(messagesResult.value)) {
      loadedMessages = messagesResult.value;
      renderMessages();
    } else {
      errors.push("消息数据刷新失败");
    }

    if (accountsResult.status === "fulfilled" && Array.isArray(accountsResult.value)) {
      renderAccounts(accountsResult.value);
    } else {
      errors.push("注册账号数据刷新失败");
    }

    if (clusterResult.status === "fulfilled" && clusterResult.value) {
      renderClusterNodes(clusterResult.value);
    }

    completedRefreshes += 1;
    refreshCount.textContent = String(completedRefreshes);
    lastRefresh.textContent = formatClock(new Date());
    showErrors(errors);
    setRefreshState(errors.length ? "部分更新失败" : "数据已更新", errors.length ? "error" : "success");
  } catch {
    showErrors(["数据刷新时发生异常，请稍后重试"]);
    setRefreshState("刷新失败", "error");
  } finally {
    refreshButton.disabled = false;
    isRefreshing = false;
  }
}

function scheduleAutoRefresh() {
  clearInterval(autoRefreshTimer);
  if (refreshIntervalMs <= 0) {
    updateClockAndCountdown();
    return;
  }
  nextRefreshAt = Date.now() + refreshIntervalMs;
  updateClockAndCountdown();
  autoRefreshTimer = setInterval(() => {
    nextRefreshAt = Date.now() + refreshIntervalMs;
    void refreshData();
  }, refreshIntervalMs);
}

// 广播表单提交
if (broadcastForm && broadcastInput) {
  broadcastForm.addEventListener("submit", async (e) => {
    e.preventDefault();
    const text = broadcastInput.value.trim();
    if (!text) return;

    broadcastButton.disabled = true;
    broadcastFeedback.hidden = true;

    try {
      await fetchJson("/api/admin/broadcast", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ message: text }),
      });
      broadcastFeedback.textContent = `✓ 系统公告已成功全员广播！内容："${text}"`;
      broadcastFeedback.className = "feedback-msg success";
      broadcastFeedback.hidden = false;
      broadcastInput.value = "";
      setTimeout(() => { broadcastFeedback.hidden = true; }, 5000);
      void refreshData();
    } catch (err) {
      broadcastFeedback.textContent = `广播失败: ${err.message}`;
      broadcastFeedback.className = "feedback-msg error";
      broadcastFeedback.hidden = false;
    } finally {
      broadcastButton.disabled = false;
    }
  });
}

// 导出 JSON
if (exportJsonBtn) {
  exportJsonBtn.addEventListener("click", () => {
    if (!loadedMessages.length) {
      alert("当前没有可导出的消息");
      return;
    }
    const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(loadedMessages, null, 2));
    const dlAnchor = document.createElement("a");
    dlAnchor.setAttribute("href", dataStr);
    dlAnchor.setAttribute("download", `chatroom_messages_${Date.now()}.json`);
    dlAnchor.click();
  });
}

// 导出 CSV
if (exportCsvBtn) {
  exportCsvBtn.addEventListener("click", () => {
    if (!loadedMessages.length) {
      alert("当前没有可导出的消息");
      return;
    }
    let csv = "\uFEFF类型,发送者,接收者/群,时间,内容\n";
    for (const m of loadedMessages) {
      const type = m.type || "chat";
      const from = `"${(m.from || "").replace(/"/g, '""')}"`;
      const target = `"${(m.group || m.to || "全局").replace(/"/g, '""')}"`;
      const time = `"${formatDateTime(m.ts)}"`;
      const text = `"${(m.text || m.sticker || m.url || "").replace(/"/g, '""')}"`;
      csv += `${type},${from},${target},${time},${text}\n`;
    }
    const dataStr = "data:text/csv;charset=utf-8," + encodeURIComponent(csv);
    const dlAnchor = document.createElement("a");
    dlAnchor.setAttribute("href", dataStr);
    dlAnchor.setAttribute("download", `chatroom_messages_${Date.now()}.csv`);
    dlAnchor.click();
  });
}

if (refreshIntervalSelect) {
  refreshIntervalSelect.addEventListener("change", (e) => {
    refreshIntervalMs = parseInt(e.target.value, 10) || 0;
    scheduleAutoRefresh();
    if (refreshIntervalMs > 0) void refreshData();
  });
}

refreshButton.addEventListener("click", () => {
  scheduleAutoRefresh();
  void refreshData();
});

nicknameFilter.addEventListener("input", renderMessages);
clearFilter.addEventListener("click", () => {
  nicknameFilter.value = "";
  renderMessages();
  nicknameFilter.focus();
});

window.addEventListener("beforeunload", () => {
  clearInterval(autoRefreshTimer);
  clearInterval(clockTimer);
});

clockTimer = setInterval(updateClockAndCountdown, 1000);
scheduleAutoRefresh();
updateClockAndCountdown();
void refreshData();
