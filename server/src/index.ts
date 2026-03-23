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
// Uses store.steampowered.com/api/featuredcategories — the old
// ISteamApps/GetAppList/v2 endpoint was removed by Valve.
// Response is transformed to {applist:{apps:[{appid,name,category}]}} so the
// Qt SteamService parser needs no changes.
const steamFeaturedUrl =
  "https://store.steampowered.com/api/featuredcategories/?cc=US&l=en";
const steamCacheTtlMs = 24 * 60 * 60 * 1000; // 24 hours
const steamCacheFile = path.resolve(serverRootDir, "./data/steam-cache.json");

type SteamCacheEntry = {
  fetchedAtMs: number;
  body: string;
};

function loadSteamCache(): SteamCacheEntry | null {
  try {
    if (!fs.existsSync(steamCacheFile)) return null;
    const raw = fs.readFileSync(steamCacheFile, "utf8");
    const parsed = JSON.parse(raw) as SteamCacheEntry;
    if (!parsed.fetchedAtMs || !parsed.body) return null;
    if (Date.now() - parsed.fetchedAtMs > steamCacheTtlMs) return null;
    return parsed;
  } catch {
    return null;
  }
}

function saveSteamCache(body: string): void {
  try {
    fs.mkdirSync(path.dirname(steamCacheFile), { recursive: true });
    fs.writeFileSync(
      steamCacheFile,
      JSON.stringify(
        { fetchedAtMs: Date.now(), body } satisfies SteamCacheEntry,
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
  name?: string;
  final_price?: number;
  original_price?: number;
  discount_percent?: number;
  discounted?: boolean;
};

function transformFeaturedToAppList(raw: unknown): string {
  const data = raw as Record<string, { items?: SteamRawItem[] }>;
  const categoryOrder = [
    "top_sellers",
    "new_releases",
    "specials",
    "coming_soon",
  ];
  const seen = new Set<number>();
  const apps: Array<{
    appid: number;
    name: string;
    category: string;
    final_price: number;
    original_price: number;
    discount_percent: number;
    discounted: boolean;
  }> = [];

  for (const cat of categoryOrder) {
    const items = data[cat]?.items ?? [];
    for (const item of items) {
      const appid = item.id ?? item.appid ?? 0;
      const name = item.name ?? "";
      if (appid > 0 && name && !seen.has(appid)) {
        seen.add(appid);
        apps.push({
          appid,
          name,
          category: cat,
          final_price: item.final_price ?? 0,
          original_price: item.original_price ?? 0,
          discount_percent: item.discount_percent ?? 0,
          discounted: item.discounted ?? false,
        });
      }
    }
  }

  return JSON.stringify({ applist: { apps } });
}

app.get("/api/steam/apps", async (_req, res) => {
  // Serve from cache if fresh
  const cached = loadSteamCache();
  if (cached) {
    logServer("INFO", "Serving Steam app list from cache");
    res.status(200).type("application/json").send(cached.body);
    return;
  }

  logServer("INFO", "Fetching Steam featured categories from upstream", {
    url: steamFeaturedUrl,
  });

  try {
    const response = await fetch(steamFeaturedUrl, {
      headers: {
        "User-Agent": "Mozilla/5.0 (compatible; AIOServer/1.0)",
        Accept: "application/json",
      },
      redirect: "follow",
    });

    const body = await response.text();

    if (response.status < 200 || response.status >= 300) {
      logServer("WARN", "Steam API upstream error", {
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
      logServer("WARN", "Steam API returned invalid JSON", {
        body: body.slice(0, 200),
      });
      res.status(502).json({ error: "Steam API returned invalid JSON" });
      return;
    }

    const transformed = transformFeaturedToAppList(parsed);
    saveSteamCache(transformed);
    logServer("INFO", "Steam app list fetched and cached", {
      bytes: transformed.length,
    });
    res.status(200).type("application/json").send(transformed);
  } catch (e) {
    logServer("ERROR", "Steam API fetch failed", { error: String(e) });
    res.status(502).json({ error: `Steam API fetch failed: ${String(e)}` });
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
