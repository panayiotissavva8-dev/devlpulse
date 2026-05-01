<div align="center">

<img src="https://avatars.githubusercontent.com/u/39990001?v=4" width="72" height="72" style="border-radius:50%" />

# DevPulse

**Live developer stats, powered by GitHub**

[![Live Demo](https://img.shields.io/badge/demo-devpulse.fly.dev-6366f1?style=flat-square&logo=fly.io&logoColor=white)](https://devpulse.fly.dev)
[![Demo Profile](https://img.shields.io/badge/profile-hackclub--demo-3fb950?style=flat-square)](https://devpulse.fly.dev/u/hackclub-demo)
[![Built with Crow](https://img.shields.io/badge/built%20with-Crow%20C++-e3b341?style=flat-square)](https://crowcpp.org)
[![SQLite](https://img.shields.io/badge/database-SQLite-3b82f6?style=flat-square)](https://sqlite.org)

---

*Track commits, streaks, and coding hours in real time — with a shareable profile and embeddable README badge.*

[![DevPulse Badge](https://devpulse.fly.dev/badge/hackclub-demo)](https://devpulse.fly.dev/u/hackclub-demo)

</div>

---

## ✨ Features

- **⚡ Live WebSocket updates** — stats refresh the moment you push, no reload needed
- **🔗 Embeddable badge** — one Markdown line for your GitHub README
- **📊 Language breakdown** — see exactly what you've been shipping
- **🚀 GitHub webhook integration** — HMAC-verified, fires on every push
- **🌐 Public profile** — shareable `/u/username` page with full activity feed
- **🔒 Privacy controls** — toggle public/private, CSRF protection, HttpOnly cookies, rate limiting
- **🎨 Dark/light theme** — persisted per user

---

## 🚀 Quick Start

### 1. Sign in
Visit [devpulse.fly.dev](https://devpulse.fly.dev) and click **Sign in with GitHub**.

### 2. Add the webhook
In your GitHub repo → **Settings → Webhooks → Add webhook**:
- **Payload URL**: copy from your DevPulse dashboard
- **Content type**: `application/json`
- **Events**: Just the push event

### 3. Add the badge to your README
```markdown
[![DevPulse](https://devpulse.fly.dev/badge/YOUR_USERNAME)](https://devpulse.fly.dev/u/YOUR_USERNAME)
```

### 4. Push and watch it update live 🎉

---

## 🛠 Self-Hosting

### Prerequisites
- Docker & Docker Compose
- A GitHub OAuth App ([create one here](https://github.com/settings/developers))

### Setup

```bash
git clone https://github.com/YOUR_USERNAME/devpulse
cd devpulse
```

Create `secret.env`:
```env
GITHUB_CLIENT_ID=your_client_id
GITHUB_CLIENT_SECRET=your_client_secret
APP_URL=http://localhost:8080
DB_PATH=data/devpulse.sqlite
PORT=8080
```

Run:
```bash
docker compose up --build
```

Visit `http://localhost:8080` 🚀

### Deploy to Fly.io

```bash
fly launch
fly secrets set GITHUB_CLIENT_ID=xxx GITHUB_CLIENT_SECRET=xxx APP_URL=https://your-app.fly.dev
fly volumes create devpulse_data --size 1 --region ams
fly deploy
```

---

## 🏗 Architecture

```
┌─────────────┐     OAuth      ┌──────────────┐
│   Browser   │ ─────────────▶ │    GitHub    │
│             │ ◀──────────── │     API      │
└──────┬──────┘                └──────────────┘
       │ WebSocket                     ▲
       ▼                               │ Webhook
┌─────────────────────────────┐        │
│     Crow C++ HTTP Server    │────────┘
│  ┌─────────┐ ┌───────────┐  │
│  │ SQLite  │ │ WSManager │  │
│  │   DB    │ │  (live)   │  │
│  └─────────┘ └───────────┘  │
└─────────────────────────────┘
```

| Layer | Tech |
|-------|------|
| HTTP server | [Crow C++](https://crowcpp.org) |
| Database | SQLite (WAL mode) |
| HTTP client | [cpr](https://github.com/libcpr/cpr) |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) |
| Auth | GitHub OAuth 2.0 |
| Real-time | WebSockets |
| Deploy | Fly.io |

---

## 📡 API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/me` | GET | Authenticated user stats + activity |
| `/api/profile/:username` | GET | Public profile JSON |
| `/api/stats/:username` | GET | Stats only |
| `/api/activity/:username` | GET | Activity feed |
| `/api/search?q=` | GET | Search users |
| `/badge/:username` | GET | SVG badge |
| `/embed/:username` | GET | Embeddable iframe |
| `/webhook/:username/github` | POST | GitHub push webhook (HMAC) |
| `/ws/:username` | WS | Live updates |

---

## 🔒 Security

- HMAC-SHA256 verified webhooks
- HttpOnly session cookies (SameSite=Lax)
- CSRF token validation on all mutations
- Rate limiting on all public endpoints
- Input sanitization on all user data
- Content Security Policy headers

---

## 📸 Demo

**Live profile**: [devpulse.fly.dev/u/hackclub-demo](https://devpulse.fly.dev/u/hackclub-demo)

---

<div align="center">

Built with ❤️ using C++, Crow, and SQLite

[devpulse.fly.dev](https://devpulse.fly.dev)

</div>
