# YouTube & Streaming Services Architecture

> **Last audited**: 2025-07-18

## YouTube

### Architecture

Two-layer system: Node.js backend server + Qt WebEngine frontend.

**Server** (`server/`):

- Express + TypeScript, port 8916
- YouTube Data API v3 proxy (search, trending, video details)
- OAuth 2.0 device flow for user authentication
- Server owns API keys: `YOUTUBE_API_KEY`, `YOUTUBE_OAUTH_CLIENT_ID`, `YOUTUBE_OAUTH_CLIENT_SECRET`
- Client stores only `youtube/server/sessionId` in QSettings

**Qt Frontend**:

- `YouTubeBrowsePage` — Tile grid for search/browse, signals `videoRequested(videoId)`
- `YouTubePlayerPage` — QWebEngineView loading `youtube-nocookie.com/embed/{videoId}` for native streaming playback
- Replaced earlier yt-dlp + QMediaPlayer pipeline with web player embed

### Server Startup

- `AIO_YOUTUBE_SERVER_AUTOBOOT=1` → MainWindow autoboots from `server/dist/index.js`
- `AIO_YOUTUBE_SERVER_URL` override for custom server location
- Default: `http://127.0.0.1:8916`

### Server Key Files

| File                   | Purpose                                  |
| ---------------------- | ---------------------------------------- |
| `server/src/index.ts`  | Entry point, Express app, route mounting |
| `server/src/routes/`   | API route handlers                       |
| `server/package.json`  | Dependencies (Express, googleapis, etc.) |
| `server/tsconfig.json` | TypeScript configuration                 |

## Streaming Services

### Architecture

WebEngine-based wrappers via `StreamingWebViewPage`:

- Netflix, Disney+, Hulu, etc. loaded in QWebEngineView
- `StreamingHubWidget` — Selection page for available services
- Launched via `HomeScreen::streamingAppRequested` signal

### Current State

- Functional for content viewing
- TV UX needs polish: D-pad navigation within web views, profile/cookie handling
- Each service uses its standard web login flow

### QSS

- `assets/qss/youtube.qss` — Hero section, player chrome, sidebar styling
- `assets/qss/tv.qss` — Base theme applied to all pages including streaming
