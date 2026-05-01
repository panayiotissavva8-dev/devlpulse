/* ============================================================
   DevPulse – embed.js  (lightweight, iframe-safe)
   ============================================================ */

document.addEventListener('DOMContentLoaded', async () => {
  const username = location.pathname.split('/').pop();
  if (!username) return;

  try {
    const data  = await fetch(`/api/profile/${username}`).then(r => r.json());
    const stats = await fetch(`/api/stats/${username}`).then(r => r.json());
    renderEmbed(data, stats, username);
  } catch {
    document.getElementById('embed-card').innerHTML =
      '<p style="color:#8b949e;font-size:.8rem;padding:1rem">Profile unavailable.</p>';
  }
});

function renderEmbed(data, stats, username) {
  const u = data.user || data;
  const s = stats;

  const avatar   = document.getElementById('em-avatar');
  const name     = document.getElementById('em-name');
  const bio      = document.getElementById('em-bio');
  const commits  = document.getElementById('em-commits');
  const streak   = document.getElementById('em-streak');
  const repos    = document.getElementById('em-repos');
  const url      = document.getElementById('em-url');

  if (avatar)  avatar.src = u.avatar_url || '';
  if (name)    name.textContent   = u.display_name || u.username || username;
  if (bio)     bio.textContent    = (u.bio || '').slice(0, 60);
  if (commits) commits.textContent = s.total_commits?.toLocaleString() || '0';
  if (streak)  streak.textContent  = (s.streak_days || 0) + 'd';
  if (repos)   repos.textContent   = s.repos_count || 0;
  if (url)     url.href            = `${location.origin}/u/${username}`;
}
