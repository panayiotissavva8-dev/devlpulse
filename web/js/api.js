/* ============================================================
   DevPulse – api.js  (shared utilities)
   ============================================================ */

// ── CSRF ──────────────────────────────────────────────────────
function getCsrfToken() {
  const m = document.cookie.match(/(?:^|;\s*)dp_csrf=([^;]+)/);
  return m ? m[1] : '';
}

// ── Fetch wrapper ─────────────────────────────────────────────
async function apiFetch(url, opts = {}) {
  const method = (opts.method || 'GET').toUpperCase();
  const headers = { 'Content-Type': 'application/json', ...(opts.headers || {}) };
  if (['POST','PATCH','PUT','DELETE'].includes(method)) {
    headers['X-CSRF-Token'] = getCsrfToken();
  }
  const res = await fetch(url, { ...opts, headers });
  const ct = res.headers.get('content-type') || '';
  const data = ct.includes('application/json') ? await res.json() : await res.text();
  if (!res.ok) throw Object.assign(new Error(data?.error || 'Request failed'), { status: res.status, data });
  return data;
}

// ── Toast notifications ───────────────────────────────────────
function ensureToastContainer() {
  let c = document.getElementById('toast-container');
  if (!c) { c = document.createElement('div'); c.id = 'toast-container'; document.body.appendChild(c); }
  return c;
}

function showToast(msg, type = 'info', duration = 3500) {
  const c = ensureToastContainer();
  const t = document.createElement('div');
  t.className = `toast toast-${type}`;
  t.textContent = msg;
  c.appendChild(t);
  setTimeout(() => {
    t.style.animation = 'slide-out .25s ease forwards';
    setTimeout(() => t.remove(), 260);
  }, duration);
}

// ── Number formatting ─────────────────────────────────────────
function fmtNum(n) {
  if (n == null) return '0';
  if (n >= 1_000_000) return (n / 1_000_000).toFixed(1) + 'M';
  if (n >= 10_000)    return (n / 1_000).toFixed(1) + 'k';
  return n.toLocaleString();
}

function fmtHours(h) {
  if (h == null) return '0h';
  return h >= 1000 ? (h / 1000).toFixed(1) + 'k h' : h.toFixed(1) + 'h';
}

// ── Relative time ─────────────────────────────────────────────
function timeAgo(ts) {
  if (!ts) return '';
  const d   = typeof ts === 'number' ? ts * 1000 : new Date(ts).getTime();
  const sec = Math.floor((Date.now() - d) / 1000);
  if (sec <   60) return 'just now';
  if (sec < 3600) return Math.floor(sec / 60) + 'm ago';
  if (sec < 86400)return Math.floor(sec / 3600) + 'h ago';
  return Math.floor(sec / 86400) + 'd ago';
}

// ── Language colors ───────────────────────────────────────────
const LANG_COLORS = {
  JavaScript:  '#f1e05a', TypeScript: '#2b7489', Python:     '#3572A5',
  Rust:        '#dea584', Go:         '#00ADD8', 'C++':      '#f34b7d',
  C:           '#555555', Java:       '#b07219', Ruby:       '#701516',
  Swift:       '#F05138', Kotlin:     '#A97BFF', PHP:        '#4F5D95',
  Shell:       '#89e051', HTML:       '#e34c26', CSS:        '#563d7c',
  Dart:        '#00B4AB', Scala:      '#c22d40', Haskell:    '#5e5086',
  Elixir:      '#6e4a7e', Lua:        '#000080', 'C#':       '#178600',
  Vue:         '#41b883', Svelte:     '#ff3e00', Nix:        '#7e7eff',
};
function langColor(name) {
  return LANG_COLORS[name] || '#6e7681';
}

// ── Build activity item HTML ───────────────────────────────────
function buildActivityItem(a, isNew = false) {
  const repoUrl = `https://github.com/${a.repo}`;
  const sha     = a.commit_sha ? a.commit_sha.slice(0, 7) : '';
  const cls     = isNew ? 'activity-item new-commit' : 'activity-item';
  return `<div class="${cls}">
    <div class="activity-dot" style="background:${langColor(a.language || '')}"></div>
    <div class="activity-body">
      <div class="activity-repo">
        <a href="${repoUrl}" target="_blank" rel="noopener">${a.repo || '—'}</a>
        ${sha ? `<span class="mono text-3"> ${sha}</span>` : ''}
      </div>
      <div class="activity-msg truncate">${escHtml(a.message || 'No message')}</div>
      ${a.language ? `<div class="activity-meta">${escHtml(a.language)}</div>` : ''}
    </div>
    <div class="activity-time">${timeAgo(a.pushed_at)}</div>
  </div>`;
}

// ── Build language bars HTML ───────────────────────────────────
function buildLangBars(langs) {
  if (!langs || !langs.length) return '<p class="text-2 text-sm">No language data yet.</p>';
  // Normalize: accept [{name,percentage}, {language,percentage}, {name,bytes}...]
  const items = langs.map(l => ({
    name: l.name || l.language || 'Unknown',
    pct:  l.percentage != null ? l.percentage : null,
    bytes:l.bytes || 0
  }));
  // If no percentage, compute from bytes
  if (items[0].pct == null) {
    const total = items.reduce((s, i) => s + i.bytes, 0);
    items.forEach(i => { i.pct = total ? (i.bytes / total * 100) : 0; });
  }
  const sorted = items.slice(0, 8).sort((a, b) => b.pct - a.pct);
  return `<div class="lang-list">` +
    sorted.map(l => `<div class="lang-item">
      <div class="lang-header">
        <span class="lang-name">
          <span class="lang-dot" style="background:${langColor(l.name)}"></span>
          ${escHtml(l.name)}
        </span>
        <span class="lang-pct">${l.pct.toFixed(1)}%</span>
      </div>
      <div class="lang-track">
        <div class="lang-fill" style="width:${Math.min(100,l.pct)}%;background:${langColor(l.name)}"></div>
      </div>
    </div>`).join('') +
  `</div>`;
}

// ── HTML escape ───────────────────────────────────────────────
function escHtml(s) {
  return String(s)
    .replace(/&/g,'&amp;').replace(/</g,'&lt;')
    .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

// ── Copy to clipboard ─────────────────────────────────────────
async function copyText(text, btn) {
  try {
    await navigator.clipboard.writeText(text);
    const orig = btn.textContent;
    btn.textContent = 'Copied!';
    setTimeout(() => { btn.textContent = orig; }, 1500);
    showToast('Copied to clipboard', 'success', 2000);
  } catch { showToast('Copy failed', 'error', 2000); }
}

// ── Animate counter ───────────────────────────────────────────
function animateCounter(el, from, to, duration = 600) {
  if (!el || from === to) return;
  el.classList.add('bump');
  const start = performance.now();
  const step = (now) => {
    const t = Math.min((now - start) / duration, 1);
    const ease = 1 - Math.pow(1 - t, 3);
    el.textContent = fmtNum(Math.round(from + (to - from) * ease));
    if (t < 1) requestAnimationFrame(step);
    else { el.textContent = fmtNum(to); setTimeout(() => el.classList.remove('bump'), 100); }
  };
  requestAnimationFrame(step);
}

// ── WebSocket helper ──────────────────────────────────────────
function createWS(username, handlers) {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const url   = `${proto}://${location.host}/ws/${username}`;
  let ws, pingInterval, reconnectTimeout;
  let dead = false;

  function connect() {
    ws = new WebSocket(url);

    ws.onopen = () => {
      pingInterval = setInterval(() => {
        if (ws.readyState === WebSocket.OPEN) ws.send('ping');
      }, 25000);
      handlers.onopen && handlers.onopen();
    };

    ws.onmessage = (e) => {
      if (e.data === 'pong') return;
      try {
        const msg = JSON.parse(e.data);
        handlers.onmessage && handlers.onmessage(msg);
      } catch {}
    };

    ws.onclose = () => {
      clearInterval(pingInterval);
      handlers.onclose && handlers.onclose();
      if (!dead) reconnectTimeout = setTimeout(connect, 5000);
    };

    ws.onerror = () => ws.close();
  }

  connect();

  return {
    send: (d) => ws && ws.readyState === WebSocket.OPEN && ws.send(d),
    close: () => { dead = true; clearTimeout(reconnectTimeout); clearInterval(pingInterval); ws && ws.close(); }
  };
}
