/* ============================================================
   DevPulse – profile.js
   ============================================================ */

const $ = id => document.getElementById(id);
let _ws = null;
let _username = null;

document.addEventListener('DOMContentLoaded', async () => {
  _username = location.pathname.split('/').pop();
  if (!_username) { location.href = '/'; return; }

  hideLoader();

  try {
    const data = await apiFetch(`/api/profile/${_username}`);
    renderProfile(data);
    connectWS(_username);
    loadViewers(_username);
    // Refresh viewer count every 30s
    setInterval(() => loadViewers(_username), 30000);
  } catch (err) {
    if (err.status === 404) showNotFound();
    else showError('Failed to load profile: ' + (err.message || err));
  }
});

function hideLoader() {
  const l = $('page-loader');
  if (l) { l.style.opacity = '0'; setTimeout(() => l.remove(), 300); }
}

function showNotFound() {
  $('profile-content')?.classList.add('hidden');
  $('profile-404')?.classList.remove('hidden');
}

function showError(msg) {
  const el = $('profile-error');
  if (el) { el.textContent = msg; el.classList.remove('hidden'); }
}

// ── Render full profile ───────────────────────────────────────
function renderProfile(data) {
  const u  = data.user;
  const s  = data.stats;
  const el = $('profile-content');
  if (el) el.classList.remove('hidden');

  document.title = `${u.display_name || u.username} – DevPulse`;

  // Header
  $('pf-avatar').src              = u.avatar_url || '';
  $('pf-name').textContent        = u.display_name || u.username;
  $('pf-username').textContent    = '@' + u.username;
  $('pf-username').href           = `https://github.com/${u.username}`;
  $('pf-bio').textContent         = u.bio || '';
  if (!u.bio) $('pf-bio').classList.add('hidden');
  if (u.location) { $('pf-location').textContent = '📍 ' + u.location; }
  else            { $('pf-location').classList.add('hidden'); }
  if (u.role)    { $('pf-role').textContent = u.role; }
  else           { $('pf-role').classList.add('hidden'); }

  // Stats
  if (s) {
    $('ps-commits').textContent = fmtNum(s.total_commits);
    $('ps-streak').textContent  = s.streak_days + 'd';
    $('ps-repos').textContent   = fmtNum(s.repos_count);
    $('ps-hours').textContent   = fmtHours(s.hours_coded);
    $('ps-streak-sub').textContent = 'Best: ' + s.best_streak + 'd';
    $('ps-repos-sub').textContent  = s.repos_this_month + ' this month';
    $('ps-hours-sub').textContent  = (s.hours_this_week || 0).toFixed(1) + 'h this week';
    if (s.top_language) {
      $('ps-top-lang')?.textContent && ($('ps-top-lang').textContent = s.top_language);
    }
  }

  // Activity
  const acts = data.activity || [];
  const actEl = $('pf-activity');
  if (actEl) {
    actEl.innerHTML = acts.length
      ? acts.map(a => buildActivityItem(a)).join('')
      : '<p class="text-2 text-sm" style="padding:.5rem 0">No recent activity.</p>';
  }

  // Languages
  const langEl = $('pf-langs');
  if (langEl) langEl.innerHTML = buildLangBars(data.languages || []);

  // Share links
  const host  = location.origin;
  const badge = `${host}/badge/${u.username}`;
  const profile = `${host}/u/${u.username}`;
  const readmeMd = `[![DevPulse](${badge})](${profile})`;

  const si = $('share-badge-url');   if (si) si.value = badge;
  const sb = $('share-readme-md');   if (sb) sb.value = readmeMd;

  document.querySelectorAll('[data-copy]').forEach(btn => {
    btn.addEventListener('click', () => {
      const target = $(btn.dataset.copy);
      if (target) copyText(target.value, btn);
    });
  });
}

// ── Viewer count ──────────────────────────────────────────────
async function loadViewers(username) {
  try {
    const d  = await apiFetch(`/api/viewers/${username}`);
    const el = $('viewer-count');
    if (el) {
      const n = d.viewers || 0;
      el.textContent = n + (n === 1 ? ' viewer' : ' viewers');
    }
  } catch {}
}

// ── WebSocket ─────────────────────────────────────────────────
function connectWS(username) {
  _ws = createWS(username, {
    onmessage: (msg) => handleMsg(msg),
  });
}

function handleMsg(msg) {
  switch (msg.type) {
    case 'stats_update':
      updateStats(msg); break;
    case 'commit':
      prependCommit(msg); break;
    case 'snapshot':
      if (msg.stats) updateStats(msg.stats);
      break;
  }
}

function updateStats(s) {
  if (s.total_commits != null) {
    const el = $('ps-commits');
    if (el) { animateCounter(el, parseInt(el.textContent.replace(/\D/g,'')) || 0, s.total_commits); }
  }
  if (s.streak_days != null) {
    const el = $('ps-streak');
    if (el) { el.textContent = s.streak_days + 'd'; el.classList.add('bump'); setTimeout(() => el.classList.remove('bump'), 400); }
  }
}

function prependCommit(msg) {
  const actEl = $('pf-activity');
  if (!actEl) return;
  const html = buildActivityItem({
    repo:      msg.repo,
    message:   msg.message,
    language:  msg.language,
    pushed_at: Math.floor(Date.now() / 1000)
  }, true);
  actEl.insertAdjacentHTML('afterbegin', html);
  // Keep max 10
  actEl.querySelectorAll('.activity-item').forEach((el, i) => { if (i >= 10) el.remove(); });
}
