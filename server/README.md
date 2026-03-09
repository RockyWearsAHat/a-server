# YouTube Server

This service keeps YouTube API credentials and OAuth client secrets out of the Qt application.

## What It Does

- Starts Google device-code sign-in for the TV app
- Polls Google for approval and stores refresh tokens on the server side
- Proxies allow-listed YouTube Data API requests
- Refreshes expired access tokens before private API calls
- Returns only an opaque `sessionId` to the C++ app

## Setup

1. Copy `.env.example` to `.env`
2. Fill in `YOUTUBE_API_KEY`, `YOUTUBE_OAUTH_CLIENT_ID`, and `YOUTUBE_OAUTH_CLIENT_SECRET`
3. Install dependencies with `npm install`
4. Run `npm run dev` for development or `npm run build && npm start` for production

## Logs

The server now writes a persistent log file at `server/debug.log` by default.

You can override that path with:

- `SERVER_LOG_FILE=./debug.log`

This log captures:

- upstream Google device-auth failures
- local 404 route misses
- request failures with 4xx/5xx responses
- unhandled exceptions and rejected promises

## Google Cloud Setup

Create OAuth credentials in Google Cloud Console:

1. Open Google Cloud Console
2. Enable the YouTube Data API v3
3. Create an OAuth client for TV and limited-input devices
4. Copy the client id and client secret into `server/.env`
5. Create or reuse a YouTube Data API key and put it in `server/.env`

## App Integration

The Qt app should use:

- `AIO_YOUTUBE_SERVER_URL=http://127.0.0.1:8916`
- `AIO_YOUTUBE_SERVER_AUTOBOOT=1` to auto-launch the local server on app startup

The app no longer needs the Google OAuth client id or secret in its own `.env` when this server is configured.
