/* ============================================================
   DevPulse – dashboard.js
   ============================================================ */

let _user  = null;
let _stats = null;
let _ws    = null;

// ── DOM refs ──────────────────────────────────────────────────
const $ = id => document.getElementById(id);



// ── Load /api/me ──────────────────────────────────────────────
async function loadMe() {
  const data = await apiFetch('/api/me');
  _user  = data.user;
  _stats = data.stats;

  renderHeader(_user);
  renderGreeting(_user);
  renderStats(_stats);
  renderActivity(data.activity || []);
  renderLanguages(data.languages || []);
  renderShare(_user);
}

// ── Render: header ────────────────────────────────────────────
function renderHeader(u) {
  const av = $('dash-avatar');
  if (av) av.src = u.avatar_url || '';
  const ul = $('dash-uname-link');
  if (ul) { ul.href = `/u/${u.username}`; ul.textContent = '@' + u.username; }
}

// ── Render: greeting ──────────────────────────────────────────
function renderGreeting(u) {
  const hour = new Date().getHours();
  const greet = hour < 12 ? 'Good morning' : hour < 18 ? 'Good afternoon' : 'Good evening';
  $('greeting-name').textContent = `${greet}, ${u.display_name || u.username} 👋`;
  $('greeting-sub').textContent  = 'Here is your coding activity at a glance.';
}

// ── Render: stat cards ────────────────────────────────────────
function renderStats(s) {
  if (!s) return;
  setStatCard('commits', fmtNum(s.total_commits), 'All time');
  setStatCard('streak',  s.streak_days + 'd',     'Best: ' + s.best_streak + 'd');
  setStatCard('repos',   fmtNum(s.repos_count),   s.repos_this_month + ' this month');
  setStatCard('hours',   fmtHours(s.hours_coded), s.hours_this_week?.toFixed(1) + 'h this week');
}

function setStatCard(id, value, sub) {
  const el = $(id + '-val');
  if (el) el.textContent = value;
  const sl = $(id + '-sub');
  if (sl) sl.textContent = sub;
}

// ── Render: activity ──────────────────────────────────────────
function renderActivity(acts) {
  const el = $('activity-feed');
  if (!el) return;
  if (!acts.length) { el.innerHTML = '<p class="text-2 text-sm" style="padding:.5rem 0">No activity yet — push some commits!</p>'; return; }
  el.innerHTML = acts.map(a => buildActivityItem(a)).join('');
}

// ── Render: languages ─────────────────────────────────────────
function renderLanguages(langs) {
  const el = $('lang-bars');
  if (el) el.innerHTML = buildLangBars(langs);
}

// ── Render: share section ─────────────────────────────────────
function renderShare(u) {
  const host  = location.origin;
  const uname = u.username;

  const fields = {
    'share-profile':  `${host}/u/${uname}`,
    'share-badge':    `${host}/badge/${uname}`,
    'share-embed':    `${host}/embed/${uname}`,
    'share-webhook':  `${host}/webhook/${uname}/github`,
    'share-readme':   `[![DevPulse](${host}/badge/${uname})](${host}/u/${uname})`,
  };

  Object.entries(fields).forEach(([id, val]) => {
    const inp = $(id);
    if (inp) inp.value = val;
  });

  document.querySelectorAll('[data-copy]').forEach(btn => {
    btn.addEventListener('click', () => {
      const target = $(btn.dataset.copy);
      if (target) copyText(target.value, btn);
    });
  });
}

// ── Settings: theme ───────────────────────────────────────────
function wireSettings() {
  const themeToggle = $('theme-toggle');
  if (themeToggle) {
    const isDark = (_user?.theme || 'dark') === 'dark';
    if (isDark) themeToggle.classList.add('on');
    themeToggle.addEventListener('click', async () => {
      const nowDark = themeToggle.classList.toggle('on');
      const theme = nowDark ? 'dark' : 'light';
      try {
        await apiFetch('/api/me/theme', { method: 'PATCH', body: JSON.stringify({ theme }) });
        showToast(`Theme: ${theme}`, 'success', 2000);
        document.documentElement.style.setProperty('--bg', theme === 'light' ? '#f6f8fa' : '#0d1117');
        document.documentElement.style.setProperty('--text', theme === 'light' ? '#1f2328' : '#e6edf3');
      } catch { showToast('Failed to update theme', 'error'); }
    });
  }

  const pubToggle = $('public-toggle');
  if (pubToggle) {
    if (_user?.public) pubToggle.classList.add('on');
    pubToggle.addEventListener('click', async () => {
      const isPublic = pubToggle.classList.toggle('on');
      try {
        await apiFetch('/api/me/public', { method: 'PATCH', body: JSON.stringify({ public: isPublic }) });
        showToast(`Profile is now ${isPublic ? 'public' : 'private'}`, 'success', 2500);
      } catch { showToast('Failed to update visibility', 'error'); pubToggle.classList.toggle('on'); }
    });
  }
}

// ── Logout ────────────────────────────────────────────────────
function wireLogout() {
  $('logout-btn')?.addEventListener('click', async () => {
    try { await apiFetch('/auth/logout', { method: 'POST' }); } catch {}
    location.href = '/';
  });
}

// ── WebSocket ─────────────────────────────────────────────────
function connectWS() {
  if (!_user) return;

  const wsStatus = $('ws-status');
  const setStatus = (live) => {
    if (!wsStatus) return;
    wsStatus.innerHTML = live
      ? `<span class="live-dot"></span> Live`
      : `<span style="color:var(--text-3)">● Offline</span>`;
  };

  _ws = createWS(_user.username, {
    onopen: () => setStatus(true),
    onclose: () => setStatus(false),
    onmessage: (msg) => handleWsMessage(msg),
  });
}

function handleWsMessage(msg) {
  switch (msg.type) {
    case 'snapshot':
      if (msg.stats)    updateStatValues(msg.stats);
      if (msg.activity) prependActivity(msg.activity.slice(0, 3), false);
      break;

    case 'stats_update':
      updateStatValues(msg);
      break;

    case 'commit':
      prependActivity([{
        repo:       msg.repo,
        message:    msg.message,
        language:   msg.language,
        pushed_at:  Math.floor(Date.now() / 1000)
      }], true);
      showToast(`🚀 New push to ${msg.repo}`, 'info', 4000);
      if (msg.total_commits != null) updateStatValues({ total_commits: msg.total_commits });
      break;
  }
}

function updateStatValues(s) {
  if (!_stats) _stats = {};

  if (s.total_commits != null) {
    const el = $('commits-val');
    if (el) { animateCounter(el, _stats.total_commits || 0, s.total_commits); }
    _stats.total_commits = s.total_commits;
  }
  if (s.streak_days != null) {
    const el = $('streak-val');
    if (el) { el.classList.add('bump'); el.textContent = s.streak_days + 'd'; setTimeout(() => el.classList.remove('bump'), 400); }
    _stats.streak_days = s.streak_days;
  }
  if (s.repos_count != null) {
    const el = $('repos-val');
    if (el) { animateCounter(el, _stats.repos_count || 0, s.repos_count); }
    _stats.repos_count = s.repos_count;
  }
}

function prependActivity(acts, isNew) {
  const el = $('activity-feed');
  if (!el || !acts.length) return;
  const html = acts.map(a => buildActivityItem(a, isNew)).join('');
  el.insertAdjacentHTML('afterbegin', html);
  const items = el.querySelectorAll('.activity-item');
  items.forEach((item, i) => { if (i >= 10) item.remove(); });
}

// ── Bootstrap ─────────────────────────────────────────────────
(async () => {
  try {
    await loadMe();
    wireSettings();
    wireLogout();
    connectWS();
  } catch (err) {
    console.error('Dashboard error:', err);
    if (err && err.status === 401) location.href = '/';
  }
})();