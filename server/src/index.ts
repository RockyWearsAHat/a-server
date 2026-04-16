import cors from "cors";
import dotenv from "dotenv";
import express, { type Request, type Response } from "express";
import fs from "node:fs";
import path from "node:path";
import { randomUUID } from "node:crypto";
import { fileURLToPath } from "node:url";

const serverModuleDir = path.dirname(fileURLToPath(import.meta.url));
const serverRootDir = path.resolve(serverModuleDir, "..");
const serverEnvFile = path.resolve(serverRootDir, ".env");

dotenv.config({ path: serverEnvFile });

const port = Number.parseInt(process.env.PORT ?? "8916", 10);
const youtubeApiKey = process.env.YOUTUBE_API_KEY ?? "";
const youtubeClientId = process.env.YOUTUBE_OAUTH_CLIENT_ID ?? "";
const youtubeClientSecret = process.env.YOUTUBE_OAUTH_CLIENT_SECRET ?? "";
const youtubeRegion = process.env.YOUTUBE_REGION ?? "US";
const serverLogFile = path.resolve(
  serverRootDir,
  process.env.SERVER_LOG_FILE ?? "./debug.log",
);
const sessionStoreFile = path.resolve(
  serverRootDir,
  process.env.SESSION_STORE_FILE ?? "./data/youtube-sessions.json",
);
const publicProxyCacheFile = path.resolve(
  serverRootDir,
  process.env.YOUTUBE_PUBLIC_CACHE_FILE ?? "./data/youtube-public-cache.json",
);
const youtubeReadonlyScope = "https://www.googleapis.com/auth/youtube.readonly";
const allowedProxyEndpoints = new Set([
  "channels",
  "playlistItems",
  "search",
  "subscriptions",
  "videos",
]);
const publicProxyCacheTtlMs = Number.parseInt(
  process.env.YOUTUBE_PROXY_CACHE_TTL_MS ?? "21600000",
  10,
);
const quotaErrorCacheTtlMs = Number.parseInt(
  process.env.YOUTUBE_PROXY_QUOTA_ERROR_TTL_MS ?? "60000",
  10,
);
const publicProxyStaleTtlMs = Number.parseInt(
  process.env.YOUTUBE_PROXY_STALE_TTL_MS ?? "604800000",
  10,
);

type StoredSession = {
  sessionId: string;
  active: boolean;
  authenticated: boolean;
  deviceCode?: string;
  userCode?: string;
  verificationUrl?: string;
  verificationUrlComplete?: string;
  statusMessage?: string;
  pollIntervalSeconds: number;
  expiresInSeconds: number;
  createdAtSeconds: number;
  accessToken?: string;
  refreshToken?: string;
  accountDisplayName?: string;
  accountAvatarUrl?: string;
};

type ProxyResponse = {
  status: number;
  body: string;
};

type ProxyCacheEntry = ProxyResponse & {
  expiresAtMs: number;
  staleAtMs: number;
};

type YouTubeErrorPayload = {
  error?: {
    code?: number;
    message?: string;
    errors?: Array<{
      domain?: string;
      reason?: string;
      message?: string;
    }>;
  };
};

const app = express();
const inflightPublicProxyRequests = new Map<string, Promise<ProxyResponse>>();

app.use(cors());
app.use(express.json({ limit: "64kb" }));
app.use((req, res, next) => {
  const startedAt = Date.now();
  res.on("finish", () => {
    if (res.statusCode >= 400) {
      logServer("WARN", "HTTP request completed with error status", {
        method: req.method,
        path: req.originalUrl,
        statusCode: res.statusCode,
        durationMs: Date.now() - startedAt,
      });
    }
  });
  next();
});

const sessions = loadSessions();

const publicProxyCache = loadPublicProxyCache();

function ensureFileDir(filePath: string): void {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
}

// Truncate server log at startup so it doesn't accumulate across runs.
ensureFileDir(serverLogFile);
fs.writeFileSync(serverLogFile, "", "utf8");

function logServer(
  level: "INFO" | "WARN" | "ERROR",
  message: string,
  details?: unknown,
): void {
  ensureFileDir(serverLogFile);
  const suffix = details === undefined ? "" : ` ${JSON.stringify(details)}`;
  const line = `${new Date().toISOString()} [${level}] ${message}${suffix}`;
  fs.appendFileSync(serverLogFile, `${line}\n`, "utf8");

  if (level === "ERROR") {
    console.error(line);
    return;
  }
  if (level === "WARN") {
    console.warn(line);
    return;
  }
  console.log(line);
}

function ensureStoreDir(): void {
  fs.mkdirSync(path.dirname(sessionStoreFile), { recursive: true });
}

function ensurePublicProxyCacheDir(): void {
  fs.mkdirSync(path.dirname(publicProxyCacheFile), { recursive: true });
}

function loadSessions(): Map<string, StoredSession> {
  ensureStoreDir();
  if (!fs.existsSync(sessionStoreFile)) {
    return new Map<string, StoredSession>();
  }

  try {
    const raw = fs.readFileSync(sessionStoreFile, "utf8");
    const parsed = JSON.parse(raw) as StoredSession[];
    return new Map(parsed.map((entry) => [entry.sessionId, entry]));
  } catch {
    return new Map<string, StoredSession>();
  }
}

function saveSessions(): void {
  ensureStoreDir();
  fs.writeFileSync(
    sessionStoreFile,
    JSON.stringify([...sessions.values()], null, 2),
    "utf8",
  );
}

function prunePublicProxyCacheEntries(
  cache: Map<string, ProxyCacheEntry>,
): void {
  const now = Date.now();
  for (const [cacheKey, entry] of cache.entries()) {
    if (entry.staleAtMs <= now) {
      cache.delete(cacheKey);
    }
  }
}

function loadPublicProxyCache(): Map<string, ProxyCacheEntry> {
  ensurePublicProxyCacheDir();
  if (!fs.existsSync(publicProxyCacheFile)) {
    return new Map<string, ProxyCacheEntry>();
  }

  try {
    const raw = fs.readFileSync(publicProxyCacheFile, "utf8");
    const parsed = JSON.parse(raw) as Array<{
      cacheKey: string;
      status: number;
      body: string;
      expiresAtMs: number;
      staleAtMs?: number;
    }>;
    const cache = new Map<string, ProxyCacheEntry>();
    for (const entry of parsed) {
      if (!entry || typeof entry.cacheKey !== "string") {
        continue;
      }
      cache.set(entry.cacheKey, {
        status: entry.status,
        body: entry.body,
        expiresAtMs: entry.expiresAtMs,
        staleAtMs: entry.staleAtMs ?? entry.expiresAtMs,
      });
    }
    prunePublicProxyCacheEntries(cache);
    return cache;
  } catch {
    return new Map<string, ProxyCacheEntry>();
  }
}

function savePublicProxyCache(): void {
  ensurePublicProxyCacheDir();
  prunePublicProxyCacheEntries(publicProxyCache);
  fs.writeFileSync(
    publicProxyCacheFile,
    JSON.stringify(
      [...publicProxyCache.entries()].map(([cacheKey, entry]) => ({
        cacheKey,
        status: entry.status,
        body: entry.body,
        expiresAtMs: entry.expiresAtMs,
        staleAtMs: entry.staleAtMs,
      })),
      null,
      2,
    ),
    "utf8",
  );
}

function nowSeconds(): number {
  return Math.floor(Date.now() / 1000);
}

function secondsRemaining(session: StoredSession): number {
  if (!session.active) {
    return 0;
  }
  return Math.max(
    0,
    session.expiresInSeconds - (nowSeconds() - session.createdAtSeconds),
  );
}

function sessionView(session: StoredSession) {
  return {
    sessionId: session.sessionId,
    active: session.active,
    authenticated: session.authenticated,
    verificationUrl: session.verificationUrl ?? "",
    verificationUrlComplete: session.verificationUrlComplete ?? "",
    userCode: session.userCode ?? "",
    statusMessage: session.statusMessage ?? "",
    pollIntervalSeconds: session.pollIntervalSeconds,
    expiresInSeconds: session.expiresInSeconds,
    secondsRemaining: secondsRemaining(session),
    accountDisplayName: session.accountDisplayName ?? "",
    accountAvatarUrl: session.accountAvatarUrl ?? "",
  };
}

async function postForm(
  url: string,
  form: URLSearchParams,
): Promise<{ status: number; body: string }> {
  const response = await fetch(url, {
    method: "POST",
    headers: {
      Accept: "application/json",
      "Content-Type": "application/x-www-form-urlencoded",
    },
    body: form,
  });
  return {
    status: response.status,
    body: await response.text(),
  };
}

async function fetchYouTubeJson(
  endpoint: string,
  params: string,
  session?: StoredSession,
): Promise<ProxyResponse> {
  const url = new URL(`https://www.googleapis.com/youtube/v3/${endpoint}`);
  const search = new URLSearchParams(params);
  search.forEach((value, key) => url.searchParams.set(key, value));
  if (!session?.accessToken && youtubeApiKey) {
    url.searchParams.set("key", youtubeApiKey);
  }

  const headers: Record<string, string> = {
    Accept: "application/json",
  };
  if (session?.accessToken) {
    headers.Authorization = `Bearer ${session.accessToken}`;
  }

  const response = await fetch(url, { headers });
  return {
    status: response.status,
    body: await response.text(),
  };
}

function parseYouTubeErrorBody(body: string): {
  code?: number;
  reason?: string;
  message?: string;
} | null {
  try {
    const parsed = JSON.parse(body) as YouTubeErrorPayload;
    return {
      code: parsed.error?.code,
      reason: parsed.error?.errors?.[0]?.reason,
      message: parsed.error?.errors?.[0]?.message ?? parsed.error?.message,
    };
  } catch {
    return null;
  }
}

function publicProxyCacheKey(endpoint: string, params: string): string {
  return `${endpoint}?${params}`;
}

function readPublicProxyCache(
  cacheKey: string,
  allowStale = false,
): ProxyResponse | null {
  const cached = publicProxyCache.get(cacheKey);
  if (!cached) {
    return null;
  }

  const now = Date.now();
  if (!allowStale && cached.expiresAtMs <= now) {
    return null;
  }
  if (cached.staleAtMs <= now) {
    publicProxyCache.delete(cacheKey);
    savePublicProxyCache();
    return null;
  }

  return {
    status: cached.status,
    body: cached.body,
  };
}

function getProxyCacheTtlMs(response: ProxyResponse): number {
  if (response.status >= 200 && response.status < 300) {
    return Math.max(0, publicProxyCacheTtlMs);
  }

  const error = parseYouTubeErrorBody(response.body);
  if (response.status === 403 && error?.reason === "quotaExceeded") {
    return Math.max(0, quotaErrorCacheTtlMs);
  }

  return 0;
}

function writePublicProxyCache(
  cacheKey: string,
  response: ProxyResponse,
): void {
  const ttlMs = getProxyCacheTtlMs(response);
  if (ttlMs <= 0) {
    publicProxyCache.delete(cacheKey);
    savePublicProxyCache();
    return;
  }

  const now = Date.now();
  publicProxyCache.set(cacheKey, {
    status: response.status,
    body: response.body,
    expiresAtMs: now + ttlMs,
    staleAtMs: now + Math.max(ttlMs, publicProxyStaleTtlMs),
  });
  savePublicProxyCache();
}

async function refreshSessionAccessToken(
  session: StoredSession,
): Promise<boolean> {
  if (!session.refreshToken || !youtubeClientId) {
    return false;
  }

  const form = new URLSearchParams({
    client_id: youtubeClientId,
    refresh_token: session.refreshToken,
    grant_type: "refresh_token",
  });
  if (youtubeClientSecret) {
    form.set("client_secret", youtubeClientSecret);
  }

  const response = await postForm("https://oauth2.googleapis.com/token", form);
  if (response.status < 200 || response.status >= 300) {
    return false;
  }

  const parsed = JSON.parse(response.body) as {
    access_token?: string;
    refresh_token?: string;
  };
  if (!parsed.access_token) {
    return false;
  }

  session.accessToken = parsed.access_token;
  if (parsed.refresh_token) {
    session.refreshToken = parsed.refresh_token;
  }
  session.authenticated = true;
  saveSessions();
  return true;
}

async function ensureAccountProfile(session: StoredSession): Promise<void> {
  if (
    !session.authenticated ||
    !session.accessToken ||
    (session.accountDisplayName && session.accountAvatarUrl)
  ) {
    return;
  }

  const response = await fetchYouTubeJson(
    "channels",
    "part=snippet&mine=true",
    session,
  );
  if (response.status === 401 && (await refreshSessionAccessToken(session))) {
    await ensureAccountProfile(session);
    return;
  }
  if (response.status < 200 || response.status >= 300) {
    return;
  }

  const parsed = JSON.parse(response.body) as {
    items?: Array<{
      snippet?: {
        title?: string;
        thumbnails?: Record<string, { url?: string }>;
      };
    }>;
  };
  const snippet = parsed.items?.[0]?.snippet;
  const title = snippet?.title;
  if (title) {
    session.accountDisplayName = title;
  }
  const thumbs = snippet?.thumbnails;
  session.accountAvatarUrl =
    thumbs?.high?.url ??
    thumbs?.medium?.url ??
    thumbs?.default?.url ??
    session.accountAvatarUrl;
  saveSessions();
}

function requireSession(req: Request, res: Response): StoredSession | null {
  const sessionId =
    req.header("x-aio-youtube-session") ??
    (typeof req.body?.sessionId === "string" ? req.body.sessionId : "");
  if (!sessionId) {
    res.status(401).json({ message: "Missing YouTube session id." });
    return null;
  }

  const session = sessions.get(sessionId);
  if (!session) {
    res.status(401).json({ message: "Unknown YouTube session." });
    return null;
  }
  return session;
}

app.get("/health", (_req, res) => {
  res.json({
    ok: true,
    region: youtubeRegion,
    hasApiKey: Boolean(youtubeApiKey),
    hasOAuthClientId: Boolean(youtubeClientId),
  });
});

app.all("/api/youtube/device/start", async (_req, res) => {
  if (!youtubeClientId) {
    res.status(503).json({
      message: "Server is missing YOUTUBE_OAUTH_CLIENT_ID.",
      active: false,
      authenticated: false,
    });
    return;
  }

  const form = new URLSearchParams({
    client_id: youtubeClientId,
    scope: youtubeReadonlyScope,
  });
  const response = await postForm(
    "https://oauth2.googleapis.com/device/code",
    form,
  );
  if (response.status < 200 || response.status >= 300) {
    let upstreamMessage = "Google device sign-in could not be started.";
    try {
      const parsed = JSON.parse(response.body) as {
        error?: string;
        error_description?: string;
      };
      if (parsed.error_description) {
        upstreamMessage = parsed.error_description;
      } else if (parsed.error) {
        upstreamMessage = `Google device auth error: ${parsed.error}`;
      }
    } catch {
      if (response.body.trim().length > 0) {
        upstreamMessage = response.body.trim();
      }
    }

    console.error(
      "[YouTubeServer] Device auth start failed",
      JSON.stringify({
        upstreamStatus: response.status,
        message: upstreamMessage,
        body: response.body,
      }),
    );
    logServer("ERROR", "Device auth start failed", {
      upstreamStatus: response.status,
      message: upstreamMessage,
      body: response.body,
    });

    res.status(502).json({
      message: upstreamMessage,
      upstreamStatus: response.status,
      active: false,
      authenticated: false,
    });
    return;
  }

  const parsed = JSON.parse(response.body) as {
    device_code?: string;
    user_code?: string;
    verification_url?: string;
    verification_uri?: string;
    verification_url_complete?: string;
    verification_uri_complete?: string;
    interval?: number;
    expires_in?: number;
  };
  if (!parsed.device_code || !parsed.user_code) {
    res.status(502).json({
      message: "Google returned an incomplete device-code response.",
      active: false,
      authenticated: false,
    });
    return;
  }

  const session: StoredSession = {
    sessionId: randomUUID(),
    active: true,
    authenticated: false,
    deviceCode: parsed.device_code,
    userCode: parsed.user_code,
    verificationUrl: parsed.verification_url ?? parsed.verification_uri ?? "",
    verificationUrlComplete:
      parsed.verification_url_complete ??
      parsed.verification_uri_complete ??
      "",
    statusMessage:
      "Approve the code on another device to finish connecting YouTube.",
    pollIntervalSeconds: Math.max(5, parsed.interval ?? 5),
    expiresInSeconds: Math.max(60, parsed.expires_in ?? 1800),
    createdAtSeconds: nowSeconds(),
  };
  sessions.set(session.sessionId, session);
  saveSessions();
  res.json(sessionView(session));
});

app.post("/api/youtube/device/poll", async (req, res) => {
  const session = requireSession(req, res);
  if (!session) {
    return;
  }

  if (!session.active) {
    await ensureAccountProfile(session);
    res.json(sessionView(session));
    return;
  }

  if (secondsRemaining(session) <= 0) {
    session.active = false;
    session.statusMessage =
      "This connection code expired. Start sign-in again.";
    saveSessions();
    res.json(sessionView(session));
    return;
  }

  const form = new URLSearchParams({
    client_id: youtubeClientId,
    device_code: session.deviceCode ?? "",
    grant_type: "urn:ietf:params:oauth:grant-type:device_code",
  });
  if (youtubeClientSecret) {
    form.set("client_secret", youtubeClientSecret);
  }

  const response = await postForm("https://oauth2.googleapis.com/token", form);
  const parsed = JSON.parse(response.body) as {
    access_token?: string;
    refresh_token?: string;
    error?: string;
  };

  if (parsed.access_token) {
    session.accessToken = parsed.access_token;
    if (parsed.refresh_token) {
      session.refreshToken = parsed.refresh_token;
    }
    session.active = false;
    session.authenticated = true;
    session.statusMessage = "YouTube account connected.";
    await ensureAccountProfile(session);
    saveSessions();
    res.json(sessionView(session));
    return;
  }

  switch (parsed.error) {
    case "authorization_pending":
      session.statusMessage = "Waiting for approval from Google...";
      break;
    case "slow_down":
      session.pollIntervalSeconds += 5;
      session.statusMessage =
        "Google asked for slower polling. Still waiting...";
      break;
    case "access_denied":
      session.active = false;
      session.statusMessage = "YouTube sign-in was denied.";
      break;
    default:
      session.active = false;
      session.statusMessage = "YouTube sign-in failed or expired.";
      break;
  }

  saveSessions();
  res.json(sessionView(session));
});

app.get("/api/youtube/account", async (req, res) => {
  const session = requireSession(req, res);
  if (!session) {
    return;
  }
  await ensureAccountProfile(session);
  res.json({
    authenticated: session.authenticated,
    accountDisplayName: session.accountDisplayName ?? "",
    accountAvatarUrl: session.accountAvatarUrl ?? "",
  });
});

app.post("/api/youtube/logout", (req, res) => {
  const sessionId =
    typeof req.body?.sessionId === "string" ? req.body.sessionId : "";
  if (sessionId) {
    sessions.delete(sessionId);
    saveSessions();
  }
  res.json({ ok: true });
});

app.get("/api/youtube/proxy", async (req, res) => {
  const endpoint =
    typeof req.query.endpoint === "string" ? req.query.endpoint : "";
  const params = typeof req.query.params === "string" ? req.query.params : "";
  const requireAuth = req.query.requireAuth === "1";

  if (!allowedProxyEndpoints.has(endpoint)) {
    res.status(400).json({ message: "Endpoint is not allowed." });
    return;
  }

  let session: StoredSession | undefined;
  const sessionId = req.header("x-aio-youtube-session");
  if (sessionId) {
    session = sessions.get(sessionId);
  }

  if (requireAuth && !session?.authenticated) {
    res.status(401).json({
      message: "This request requires an authenticated YouTube session.",
    });
    return;
  }
  if (!requireAuth && !youtubeApiKey && !session?.accessToken) {
    res.status(503).json({ message: "Server is missing YOUTUBE_API_KEY." });
    return;
  }

  const usePublicCache = !requireAuth && !session?.accessToken;
  const cacheKey = usePublicCache ? publicProxyCacheKey(endpoint, params) : "";

  if (usePublicCache) {
    const cached = readPublicProxyCache(cacheKey);
    if (cached) {
      res.status(cached.status).type("application/json").send(cached.body);
      return;
    }
  }

  const executeProxyFetch = async (): Promise<ProxyResponse> => {
    let response = await fetchYouTubeJson(endpoint, params, session);
    if (
      response.status === 401 &&
      session &&
      (await refreshSessionAccessToken(session))
    ) {
      response = await fetchYouTubeJson(endpoint, params, session);
    }
    return response;
  };

  let response: ProxyResponse;
  if (usePublicCache) {
    const inFlight = inflightPublicProxyRequests.get(cacheKey);
    if (inFlight) {
      response = await inFlight;
    } else {
      const request = executeProxyFetch().finally(() => {
        inflightPublicProxyRequests.delete(cacheKey);
      });
      inflightPublicProxyRequests.set(cacheKey, request);
      response = await request;
      writePublicProxyCache(cacheKey, response);
    }
  } else {
    response = await executeProxyFetch();
  }

  if (response.status >= 400) {
    const upstreamError = parseYouTubeErrorBody(response.body);
    const shouldServeStaleCache =
      usePublicCache &&
      (response.status >= 500 || upstreamError?.reason === "quotaExceeded");
    if (shouldServeStaleCache) {
      const staleCached = readPublicProxyCache(cacheKey, true);
      if (staleCached) {
        logServer(
          "WARN",
          "Serving stale YouTube public cache after upstream failure",
          {
            endpoint,
            statusCode: response.status,
            reason: upstreamError?.reason,
          },
        );
        res
          .status(staleCached.status)
          .type("application/json")
          .send(staleCached.body);
        return;
      }
    }

    logServer("WARN", "YouTube upstream request failed", {
      endpoint,
      requireAuth,
      statusCode: response.status,
      reason: upstreamError?.reason,
      message: upstreamError?.message,
      usingApiKey: !session?.accessToken,
      hasSession: Boolean(session?.accessToken),
    });
  }

  res.status(response.status).type("application/json").send(response.body);
});

// ---------------------------------------------------------------------------
// Steam API proxy
// ---------------------------------------------------------------------------
// Uses the Steam search results endpoint to build a paged, near-infinite feed
// that can back native infinite scroll and server-side search.
const steamSearchUrl = "https://store.steampowered.com/search/results/";
const steamCacheTtlMs = 24 * 60 * 60 * 1000; // 24 hours
const steamCacheSchemaVersion = 3;
const steamCacheFile = path.resolve(serverRootDir, "./data/steam-cache.json");

type SteamCacheEntry = { key: string; fetchedAtMs: number; body: string };

type SteamCacheStore = {
  schemaVersion: number;
  entries: SteamCacheEntry[];
};

type SteamStoreItemType = 0 | 1 | 2;

function loadSteamCache(): Map<string, SteamCacheEntry> {
  try {
    if (!fs.existsSync(steamCacheFile))
      return new Map<string, SteamCacheEntry>();
    const raw = fs.readFileSync(steamCacheFile, "utf8");
    const parsed = JSON.parse(raw) as SteamCacheStore;
    if (parsed.schemaVersion !== steamCacheSchemaVersion) {
      return new Map<string, SteamCacheEntry>();
    }
    const entries = new Map<string, SteamCacheEntry>();
    for (const entry of parsed.entries ?? []) {
      if (!entry?.key || !entry.body || !entry.fetchedAtMs) {
        continue;
      }
      if (Date.now() - entry.fetchedAtMs > steamCacheTtlMs) {
        continue;
      }
      entries.set(entry.key, entry);
    }
    return entries;
  } catch {
    return new Map<string, SteamCacheEntry>();
  }
}

const steamCacheEntries = loadSteamCache();

function saveSteamCache(key: string, body: string): void {
  try {
    steamCacheEntries.set(key, { key, body, fetchedAtMs: Date.now() });
    fs.mkdirSync(path.dirname(steamCacheFile), { recursive: true });
    fs.writeFileSync(
      steamCacheFile,
      JSON.stringify(
        {
          schemaVersion: steamCacheSchemaVersion,
          entries: [...steamCacheEntries.values()],
        } satisfies SteamCacheStore,
        null,
        2,
      ),
      "utf8",
    );
  } catch (e) {
    logServer("WARN", "Failed to save Steam cache", { error: String(e) });
  }
}

type SteamRawItem = {
  id?: number;
  appid?: number;
  type?: SteamStoreItemType;
  name?: string;
  final_price?: number;
  original_price?: number;
  discount_percent?: number;
  discounted?: boolean;
  header_image?: string;
  small_capsule_image?: string;
  large_capsule_image?: string;
};

type SteamSearchParams = {
  category: string;
  query: string;
  start: number;
  count: number;
};

function decodeHtmlAttribute(value: string): string {
  return value
    .replace(/&amp;/g, "&")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">");
}

function appendUniqueUrl(urls: string[], candidate: unknown): void {
  if (typeof candidate !== "string") {
    return;
  }
  const trimmed = candidate.trim();
  if (!trimmed || urls.includes(trimmed)) {
    return;
  }
  urls.push(trimmed);
}

function extractSteamStoreMetaImages(html: string): string[] {
  const urls: string[] = [];
  const patterns = [
    /<meta\s+(?:property|name)=["'](?:og:image|twitter:image)["']\s+content=["']([^"']+)["']/gi,
    /<link\s+rel=["']image_src["']\s+href=["']([^"']+)["']/gi,
  ];

  for (const pattern of patterns) {
    for (const match of html.matchAll(pattern)) {
      appendUniqueUrl(urls, decodeHtmlAttribute(match[1] ?? ""));
    }
  }

  return urls;
}

async function fetchSteamStoreMetaArt(
  itemType: SteamStoreItemType,
  appid: number,
): Promise<string[]> {
  const pageKind = itemType === 2 ? "bundle" : itemType === 1 ? "sub" : "app";
  if (pageKind === "app") {
    return [];
  }

  const storeUrl = `https://store.steampowered.com/${pageKind}/${appid}/?cc=US&l=en`;

  try {
    const response = await fetch(storeUrl, {
      headers: {
        "User-Agent": "Mozilla/5.0 (compatible; AIOServer/1.0)",
        Accept: "text/html,application/xhtml+xml",
      },
      redirect: "follow",
      signal: AbortSignal.timeout(5000),
    });

    if (response.status < 200 || response.status >= 300) {
      logServer("WARN", "Steam store page art enrichment failed", {
        appid,
        itemType,
        status: response.status,
      });
      return [];
    }

    return extractSteamStoreMetaImages(await response.text());
  } catch (error) {
    logServer("WARN", "Steam store page art enrichment error", {
      appid,
      itemType,
      error: String(error),
    });
    return [];
  }
}

async function buildSteamCoverArtUrls(item: SteamRawItem): Promise<string[]> {
  const urls: string[] = [];
  appendUniqueUrl(urls, item.large_capsule_image);
  appendUniqueUrl(urls, item.header_image);
  appendUniqueUrl(urls, item.small_capsule_image);

  const itemType = item.type ?? 0;
  const appid = item.id ?? item.appid ?? 0;
  if ((itemType === 1 || itemType === 2) && appid > 0) {
    for (const url of await fetchSteamStoreMetaArt(itemType, appid)) {
      appendUniqueUrl(urls, url);
    }
  }

  return urls;
}

function categoryLabelForKey(category: string): string {
  switch (category) {
    case "top-sellers":
      return "Top Sellers";
    case "new-releases":
      return "New Releases";
    case "on-sale":
      return "On Sale";
    case "coming-soon":
      return "Coming Soon";
    default:
      return "All";
  }
}

function normalizeSteamSearchParams(req: Request): SteamSearchParams {
  const category = String(req.query.category ?? "all")
    .trim()
    .toLowerCase();
  const query = String(req.query.q ?? "").trim();
  const start = Math.max(
    0,
    Number.parseInt(String(req.query.start ?? "0"), 10) || 0,
  );
  const count = Math.min(
    100,
    Math.max(1, Number.parseInt(String(req.query.count ?? "48"), 10) || 48),
  );
  return { category, query, start, count };
}

function buildSteamSearchRequest(params: SteamSearchParams): URLSearchParams {
  const searchParams = new URLSearchParams();
  searchParams.set("cc", "US");
  searchParams.set("l", "english");
  searchParams.set("category1", "998");
  searchParams.set("supportedlang", "english");
  searchParams.set("ndl", "1");
  searchParams.set("infinite", "1");
  searchParams.set("start", String(params.start));
  searchParams.set("count", String(params.count));
  searchParams.set("query", params.query);

  switch (params.category) {
    case "top-sellers":
      searchParams.set("filter", "topsellers");
      break;
    case "new-releases":
      searchParams.set("filter", "popularnew");
      break;
    case "on-sale":
      searchParams.set("specials", "1");
      break;
    case "coming-soon":
      searchParams.set("filter", "popularcomingsoon");
      break;
    default:
      searchParams.set("sort_by", "Released_DESC");
      break;
  }

  return searchParams;
}

function parseUsdCents(priceText: string): number {
  const normalized = decodeHtmlAttribute(priceText)
    .replace(/<[^>]+>/g, " ")
    .replace(/\s+/g, " ")
    .trim();
  if (!normalized || /free/i.test(normalized)) {
    return 0;
  }
  const match = normalized.match(/\$\s*([\d,.]+)/);
  if (!match) {
    return 0;
  }
  const amount = Number.parseFloat(match[1].replace(/,/g, ""));
  if (!Number.isFinite(amount)) {
    return 0;
  }
  return Math.round(amount * 100);
}

function extractAppId(resultHtml: string): number {
  const attrMatch = resultHtml.match(/data-ds-appid=["'](?:\[)?(\d+)/i);
  if (attrMatch) {
    return Number.parseInt(attrMatch[1], 10);
  }
  const hrefMatch = resultHtml.match(/\/app\/(\d+)\//i);
  return hrefMatch ? Number.parseInt(hrefMatch[1], 10) : 0;
}

function parseSteamSearchResultsHtml(html: string, category: string) {
  const apps: Array<{
    appid: number;
    name: string;
    category: string;
    type: SteamStoreItemType;
    cover_art_url: string;
    cover_art_urls: string[];
    final_price: number;
    original_price: number;
    discount_percent: number;
    discounted: boolean;
  }> = [];
  const rowRegex =
    /<a\b[^>]*class=["'][^"']*search_result_row[^"']*["'][\s\S]*?<\/a>/gi;
  const seen = new Set<number>();

  for (const match of html.matchAll(rowRegex)) {
    const block = match[0] ?? "";
    const appid = extractAppId(block);
    if (!appid || seen.has(appid)) {
      continue;
    }
    const titleMatch = block.match(
      /<span\s+class=["']title["']>([\s\S]*?)<\/span>/i,
    );
    const imgMatch = block.match(/<img[^>]+src=["']([^"']+)["']/i);
    const discountMatch = block.match(
      /<div[^>]+class=["'][^"']*discount_pct[^"']*["'][^>]*>([\s\S]*?)<\/div>/i,
    );
    const finalPriceMatch =
      block.match(
        /<div[^>]+class=["'][^"']*discount_final_price[^"']*["'][^>]*>([\s\S]*?)<\/div>/i,
      ) ??
      block.match(
        /<div[^>]+class=["'][^"']*search_price[^"']*["'][^>]*>([\s\S]*?)<\/div>/i,
      );
    const originalPriceMatch = block.match(
      /<div[^>]+class=["'][^"']*discount_original_price[^"']*["'][^>]*>([\s\S]*?)<\/div>/i,
    );
    const name = decodeHtmlAttribute(
      (titleMatch?.[1] ?? "").replace(/<[^>]+>/g, "").trim(),
    );
    if (!name) {
      continue;
    }

    const discountPercent = discountMatch
      ? Number.parseInt((discountMatch[1] ?? "").replace(/[^\d-]/g, ""), 10) *
        -1
      : 0;
    const finalPrice = parseUsdCents(finalPriceMatch?.[1] ?? "");
    const originalPrice = parseUsdCents(originalPriceMatch?.[1] ?? "");
    const coverArtUrl = decodeHtmlAttribute(imgMatch?.[1] ?? "");

    seen.add(appid);
    apps.push({
      appid,
      name,
      category: categoryLabelForKey(category),
      type: 0,
      cover_art_url: coverArtUrl,
      cover_art_urls: coverArtUrl ? [coverArtUrl] : [],
      final_price: finalPrice,
      original_price: originalPrice,
      discount_percent: Math.max(0, discountPercent),
      discounted: Math.max(0, discountPercent) > 0,
    });
  }

  return apps;
}

app.get("/api/steam/apps", async (req, res) => {
  const params = normalizeSteamSearchParams(req);
  const cacheKey = JSON.stringify(params);
  const cached = steamCacheEntries.get(cacheKey);
  if (cached && Date.now() - cached.fetchedAtMs <= steamCacheTtlMs) {
    logServer("INFO", "Serving Steam app list from cache", params);
    res.status(200).type("application/json").send(cached.body);
    return;
  }

  const query = buildSteamSearchRequest(params);
  const upstreamUrl = `${steamSearchUrl}?${query.toString()}`;
  logServer("INFO", "Fetching Steam search results from upstream", {
    ...params,
    url: upstreamUrl,
  });

  try {
    const response = await fetch(upstreamUrl, {
      headers: {
        "User-Agent": "Mozilla/5.0 (compatible; AIOServer/1.0)",
        Accept: "application/json,text/plain,*/*",
      },
      redirect: "follow",
    });

    const body = await response.text();

    if (response.status < 200 || response.status >= 300) {
      logServer("WARN", "Steam search upstream error", {
        status: response.status,
        body: body.slice(0, 200),
      });
      res
        .status(502)
        .json({ error: `Steam API returned HTTP ${response.status}` });
      return;
    }

    let parsed: unknown;
    try {
      parsed = JSON.parse(body);
    } catch {
      logServer("WARN", "Steam search returned invalid JSON", {
        body: body.slice(0, 200),
      });
      res.status(502).json({ error: "Steam search returned invalid JSON" });
      return;
    }

    const root = parsed as { results_html?: string; total_count?: number };
    const apps = parseSteamSearchResultsHtml(
      root.results_html ?? "",
      params.category,
    );
    const transformed = JSON.stringify({
      applist: { apps },
      meta: {
        start: params.start,
        count: params.count,
        total_count: root.total_count ?? apps.length,
        has_more:
          params.start + apps.length < (root.total_count ?? apps.length),
        query: params.query,
        category: params.category,
      },
    });
    saveSteamCache(cacheKey, transformed);
    logServer("INFO", "Steam app list fetched and cached", {
      bytes: transformed.length,
      count: apps.length,
      totalCount: root.total_count ?? apps.length,
    });
    res.status(200).type("application/json").send(transformed);
  } catch (e) {
    logServer("ERROR", "Steam API fetch failed", {
      ...params,
      error: String(e),
    });
    res.status(502).json({ error: `Steam API fetch failed: ${String(e)}` });
  }
});

// Steam personal library proxy
// Requires a Steam Web API key and a 64-bit Steam ID.
// The user's Steam privacy settings must allow "Game details" to be public.
app.get("/api/steam/library", async (req, res) => {
  const apiKey =
    typeof req.query.apikey === "string" ? req.query.apikey.trim() : "";
  const steamId =
    typeof req.query.steamid === "string" ? req.query.steamid.trim() : "";

  if (!apiKey || !steamId) {
    res.status(400).json({ error: "apikey and steamid are required" });
    return;
  }

  // SteamID64 is a numeric string up to 20 digits — reject anything else
  // to prevent URL injection into the upstream path.
  if (!/^\d{1,20}$/.test(steamId)) {
    res.status(400).json({ error: "Invalid steamid format" });
    return;
  }

  const upstreamUrl =
    `https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/` +
    `?key=${encodeURIComponent(apiKey)}` +
    `&steamid=${encodeURIComponent(steamId)}` +
    `&include_appinfo=1&include_played_free_games=1&format=json`;

  // Log without the API key for safety
  logServer("INFO", "Fetching Steam personal library", { steamid: steamId });

  try {
    const response = await fetch(upstreamUrl, {
      headers: {
        "User-Agent": "AIOServer/1.0",
        Accept: "application/json",
      },
      redirect: "follow",
    });

    const body = await response.text();

    if (response.status === 401 || response.status === 403) {
      logServer("WARN", "Steam library: auth error", {
        status: response.status,
      });
      res.status(401).json({
        error:
          "Invalid API key or private Steam profile. " +
          "Set your Steam profile's Game Details to Public in Privacy Settings.",
      });
      return;
    }

    if (response.status !== 200) {
      logServer("WARN", "Steam library upstream error", {
        status: response.status,
      });
      res
        .status(502)
        .json({ error: `Steam API returned HTTP ${response.status}` });
      return;
    }

    logServer("INFO", "Steam library fetched successfully", {
      bytes: body.length,
    });
    res.status(200).type("application/json").send(body);
  } catch (e) {
    logServer("ERROR", "Steam library fetch failed", { error: String(e) });
    res.status(502).json({ error: `Steam library fetch failed: ${String(e)}` });
  }
});

// ── Steam OpenID auth (in-app browser flow) ──────────────────────────────────
//
// Flow:
//   1. Qt opens /steam/auth/start in a QWebEngineView.
//   2. User logs in on Steam's site.
//   3. Steam redirects to /steam/auth/callback?openid.claimed_id=...
//   4. Server validates the OpenID assertion, stores the Steam ID.
//   5. Qt polls /api/steam/auth/status until { authenticated: true, steamId }.

// In-memory pending auth state (single-user local server).
let pendingSteamId: string | null = null;
let pendingAuthToken: string | null = null; // random nonce matched per poll

// Step 1 – redirect to Steam OpenID
app.get("/steam/auth/start", (req, res) => {
  pendingSteamId = null;
  pendingAuthToken = randomUUID();

  const returnTo = `http://127.0.0.1:${port}/steam/auth/callback`;
  const realm = `http://127.0.0.1:${port}`;

  const params = new URLSearchParams({
    "openid.ns": "http://specs.openid.net/auth/2.0",
    "openid.mode": "checkid_setup",
    "openid.return_to": returnTo,
    "openid.realm": realm,
    "openid.identity": "http://specs.openid.net/auth/2.0/identifier_select",
    "openid.claimed_id": "http://specs.openid.net/auth/2.0/identifier_select",
  });

  logServer("INFO", "Steam OpenID: redirecting to Steam login");
  res.redirect(
    302,
    `https://steamcommunity.com/openid/login?${params.toString()}`,
  );
});

// Step 3 – Steam redirects back here after login
app.get("/steam/auth/callback", async (req, res) => {
  const claimedId =
    typeof req.query["openid.claimed_id"] === "string"
      ? req.query["openid.claimed_id"]
      : "";

  // Extract Steam ID64 from claimed_id URL:
  // https://steamcommunity.com/openid/id/76561198XXXXXXXXX
  const steamIdMatch = claimedId.match(/\/openid\/id\/(\d{15,20})$/);
  if (!steamIdMatch) {
    logServer("WARN", "Steam OpenID callback: could not extract Steam ID", {
      claimedId,
    });
    res
      .status(400)
      .send(
        "<html><body style='background:#1a1a1a;color:#f0f0f0;font-family:sans-serif;" +
          "display:flex;align-items:center;justify-content:center;height:100vh;margin:0'>" +
          "<div style='text-align:center'><h2>Authentication failed</h2>" +
          "<p>Could not extract Steam ID from response.</p></div></body></html>",
      );
    return;
  }

  const steamId = steamIdMatch[1];

  // Validate the assertion with Steam (check_authentication)
  const params = new URLSearchParams();
  params.set("openid.ns", "http://specs.openid.net/auth/2.0");
  params.set("openid.mode", "check_authentication");
  for (const [k, v] of Object.entries(req.query)) {
    if (k !== "openid.mode" && typeof v === "string") {
      params.set(k, v);
    }
  }

  try {
    const validationRes = await fetch(
      "https://steamcommunity.com/openid/login",
      {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: params.toString(),
      },
    );
    const validationBody = await validationRes.text();
    if (!validationBody.includes("is_valid:true")) {
      logServer("WARN", "Steam OpenID: assertion invalid", { steamId });
      res
        .status(401)
        .send(
          "<html><body style='background:#1a1a1a;color:#f0f0f0;font-family:sans-serif;" +
            "display:flex;align-items:center;justify-content:center;height:100vh;margin:0'>" +
            "<div style='text-align:center'><h2>Authentication failed</h2>" +
            "<p>Steam could not verify the login.</p></div></body></html>",
        );
      return;
    }
  } catch (e) {
    logServer("ERROR", "Steam OpenID: validation network error", {
      error: String(e),
    });
    // On network error, fall through and trust the claimed_id anyway
    // (Steam's validation endpoint is not always reachable)
  }

  pendingSteamId = steamId;
  logServer("INFO", "Steam OpenID: authenticated", { steamId });

  // Show a success page — the QWebEngineView detects this URL and closes
  res.send(
    "<html><body style='background:#1a1a1a;color:#f0f0f0;font-family:sans-serif;" +
      "display:flex;align-items:center;justify-content:center;height:100vh;margin:0'>" +
      "<div style='text-align:center'>" +
      "<h2 style='color:#a5d6a7'>Signed in to Steam</h2>" +
      "<p>You can close this window — AIO Server will pick up your session.</p>" +
      "</div></body></html>",
  );
});

// Step 5 – Qt polls this until authenticated
app.get("/api/steam/auth/status", (req, res) => {
  if (pendingSteamId) {
    const id = pendingSteamId;
    pendingSteamId = null; // consume it — one-time delivery
    logServer("INFO", "Steam auth status: delivering Steam ID to client", {
      steamId: id,
    });
    res.json({ authenticated: true, steamId: id });
  } else {
    res.json({ authenticated: false });
  }
});

// ── Steam library via community XML feed (no API key required) ───────────────
// Requires the user's Steam profile Game Details to be set to Public.
app.get("/api/steam/games-xml", async (req, res) => {
  const steamId =
    typeof req.query.steamid === "string" ? req.query.steamid.trim() : "";

  if (!steamId || !/^\d{15,20}$/.test(steamId)) {
    res.status(400).json({ error: "Valid steamid required" });
    return;
  }

  const url = `https://steamcommunity.com/profiles/${encodeURIComponent(steamId)}/games/?tab=all&xml=1`;
  logServer("INFO", "Fetching Steam games XML feed", { steamId });

  try {
    const response = await fetch(url, {
      headers: { "User-Agent": "AIOServer/1.0", Accept: "text/xml" },
    });
    const body = await response.text();

    if (response.status !== 200) {
      res.status(502).json({ error: `Steam returned HTTP ${response.status}` });
      return;
    }

    // Detect any <error> node — covers private profiles and other Steam errors
    const errorMatch = body.match(/<error>([^<]*)<\/error>/i);
    if (errorMatch) {
      const errorText = errorMatch[1].trim();
      logServer("WARN", "Steam games XML: error node in response", {
        error: errorText,
      });
      res.status(403).json({
        error:
          errorText ||
          "Steam returned an error. Check your profile Game Details privacy setting.",
      });
      return;
    }

    logServer("INFO", "Steam games XML fetched", { bytes: body.length });
    res.status(200).type("text/xml").send(body);
  } catch (e) {
    logServer("ERROR", "Steam games XML fetch failed", { error: String(e) });
    res
      .status(502)
      .json({ error: `Steam games XML fetch failed: ${String(e)}` });
  }
});

app.use((req, res) => {
  logServer("WARN", "Unhandled route", {
    method: req.method,
    path: req.originalUrl,
  });
  res.status(404).json({ message: "Route not found." });
});

app.use(
  (
    error: unknown,
    req: Request,
    res: Response,
    _next: express.NextFunction,
  ) => {
    logServer("ERROR", "Unhandled server error", {
      method: req.method,
      path: req.originalUrl,
      error:
        error instanceof Error ? (error.stack ?? error.message) : String(error),
    });
    res.status(500).json({ message: "Internal server error." });
  },
);

process.on("uncaughtException", (error) => {
  logServer("ERROR", "Uncaught exception", {
    error: error.stack ?? error.message,
  });
});

process.on("unhandledRejection", (reason) => {
  logServer("ERROR", "Unhandled promise rejection", {
    reason:
      reason instanceof Error
        ? (reason.stack ?? reason.message)
        : String(reason),
  });
});

app.listen(port, () => {
  logServer("INFO", "YouTube server listening", {
    url: `http://127.0.0.1:${port}`,
    envFile: serverEnvFile,
    logFile: serverLogFile,
    publicCacheFile: publicProxyCacheFile,
    hasApiKey: Boolean(youtubeApiKey),
    hasOAuthClientId: Boolean(youtubeClientId),
  });
});
