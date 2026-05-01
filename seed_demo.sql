INSERT OR IGNORE INTO users (
  github_id, username, display_name, avatar_url, bio,
  location, role, github_url, public, theme, webhook_secret, created_at
) VALUES (
  'demo-hackclub-0000', 'hackclub-demo', 'Hack Club',
  'https://avatars.githubusercontent.com/u/39990001?v=4',
  'A nonprofit network of high school coding clubs. Building cool things.',
  'San Francisco, CA', 'student', 'https://github.com/hackclub',
  1, 'dark', 'demo-secret-not-used', strftime('%s','now')
);
INSERT OR REPLACE INTO stats_cache (user_id, total_commits, streak_days, best_streak, repos_count, hours_coded, commits_today, repos_this_month, hours_this_week, top_language, languages_json, last_updated)
SELECT user_id, 1847, 42, 67, 38, 312.5, 7, 5, 18.3, 'JavaScript',
'[{"name":"JavaScript","percent":41.2},{"name":"Python","percent":28.7},{"name":"Rust","percent":15.1},{"name":"TypeScript","percent":9.4},{"name":"Go","percent":5.6}]',
strftime('%s','now') FROM users WHERE username = 'hackclub-demo';
DELETE FROM activity_feed WHERE user_id = (SELECT user_id FROM users WHERE username = 'hackclub-demo');
INSERT INTO activity_feed (user_id, repo, message, language, commit_sha, pushed_at) SELECT user_id, 'hackclub/sprig', 'feat: add multiplayer support to game engine', 'JavaScript', 'a1b2c3d', strftime('%s','now') - 900 FROM users WHERE username = 'hackclub-demo';
INSERT INTO activity_feed (user_id, repo, message, language, commit_sha, pushed_at) SELECT user_id, 'hackclub/site', 'fix: mobile nav overflow on small screens', 'JavaScript', 'b2c3d4e', strftime('%s','now') - 3600 FROM users WHERE username = 'hackclub-demo';
INSERT INTO activity_feed (user_id, repo, message, language, commit_sha, pushed_at) SELECT user_id, 'hackclub/hcb', 'feat: stripe webhook handler for donations', 'TypeScript', 'f6a7b8c', strftime('%s','now') - 43200 FROM users WHERE username = 'hackclub-demo';
INSERT INTO activity_feed (user_id, repo, message, language, commit_sha, pushed_at) SELECT user_id, 'hackclub/blot', 'feat: SVG path optimizer, 40% faster renders', 'Rust', 'd4e5f6a', strftime('%s','now') - 86400 FROM users WHERE username = 'hackclub-demo';
INSERT INTO activity_feed (user_id, repo, message, language, commit_sha, pushed_at) SELECT user_id, 'hackclub/dns', 'add: hackclub.app wildcard CNAME support', 'Go', 'a7b8c9d', strftime('%s','now') - 172800 FROM users WHERE username = 'hackclub-demo';
