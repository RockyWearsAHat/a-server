#!/usr/bin/env node

const fs = require("fs/promises");
const path = require("path");
const readline = require("readline");

const MCP_VERSION = "2024-11-05";
const DEFAULT_USER_AGENT =
  "Mozilla/5.0 (compatible; AIOServerResearchTool/0.0.1; +https://github.com/github/copilot)";
const WORKSPACE_ROOT = process.cwd();
const KNOWLEDGE_ROOT = path.join(WORKSPACE_ROOT, ".github", "knowledge");

function send(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

function sendError(id, code, message) {
  send({
    jsonrpc: "2.0",
    id,
    error: { code, message },
  });
}

function requireEnv(name) {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Missing required environment variable: ${name}`);
  }
  return value;
}

function toPositiveInt(value, fallback, min, max) {
  const parsed = Number.parseInt(String(value ?? fallback), 10);
  if (!Number.isFinite(parsed)) {
    return fallback;
  }
  return Math.min(max, Math.max(min, parsed));
}

function decodeHtmlEntities(text) {
  return text
    .replace(/&nbsp;/gi, " ")
    .replace(/&amp;/gi, "&")
    .replace(/&quot;/gi, '"')
    .replace(/&#39;/gi, "'")
    .replace(/&lt;/gi, "<")
    .replace(/&gt;/gi, ">")
    .replace(/&#(\d+);/g, (_, code) =>
      String.fromCharCode(Number.parseInt(code, 10)),
    )
    .replace(/&#x([0-9a-f]+);/gi, (_, code) =>
      String.fromCharCode(Number.parseInt(code, 16)),
    );
}

function stripHtml(html) {
  return decodeHtmlEntities(
    html
      .replace(/<script\b[^<]*(?:(?!<\/script>)<[^<]*)*<\/script>/gis, " ")
      .replace(/<style\b[^<]*(?:(?!<\/style>)<[^<]*)*<\/style>/gis, " ")
      .replace(
        /<noscript\b[^<]*(?:(?!<\/noscript>)<[^<]*)*<\/noscript>/gis,
        " ",
      )
      .replace(/<svg\b[^<]*(?:(?!<\/svg>)<[^<]*)*<\/svg>/gis, " ")
      .replace(/<br\s*\/?>/gi, "\n")
      .replace(
        /<\/(p|div|section|article|main|aside|header|footer|li|tr|table|h1|h2|h3|h4|h5|h6)>/gi,
        "$&\n",
      )
      .replace(/<[^>]+>/g, " ")
      .replace(/[ \t]+/g, " ")
      .replace(/\n{3,}/g, "\n\n")
      .trim(),
  );
}

function getTitle(html) {
  const match = html.match(/<title[^>]*>([\s\S]*?)<\/title>/i);
  return match ? decodeHtmlEntities(match[1].trim()) : "Untitled";
}

function summarizeText(text, maxChars) {
  if (text.length <= maxChars) {
    return text;
  }

  const clipped = text.slice(0, maxChars);
  const lastBreak = Math.max(
    clipped.lastIndexOf("\n\n"),
    clipped.lastIndexOf(". "),
  );
  if (lastBreak > maxChars * 0.6) {
    return `${clipped.slice(0, lastBreak).trim()}...`;
  }
  return `${clipped.trim()}...`;
}

function summarizeInline(text, maxChars) {
  return summarizeText(text.replace(/\s+/g, " ").trim(), maxChars);
}

function getMarkdownTitle(text, fallback) {
  const headingMatch = text.match(/^#\s+(.+)$/m);
  return headingMatch ? headingMatch[1].trim() : fallback;
}

function escapeRegExp(text) {
  return text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function tokenizeQuery(query) {
  return String(query)
    .toLowerCase()
    .split(/[^a-z0-9_.-]+/)
    .map((term) => term.trim())
    .filter((term) => term.length >= 2);
}

async function collectMarkdownFiles(rootDir) {
  const files = [];

  async function walk(currentDir) {
    const entries = await fs.readdir(currentDir, { withFileTypes: true });
    for (const entry of entries) {
      const fullPath = path.join(currentDir, entry.name);
      if (entry.isDirectory()) {
        await walk(fullPath);
        continue;
      }
      if (entry.isFile() && entry.name.endsWith(".md")) {
        files.push(fullPath);
      }
    }
  }

  await walk(rootDir);
  return files;
}

function buildKnowledgeSnippet(text, terms) {
  const compact = text.replace(/\s+/g, " ").trim();
  if (!compact) {
    return "No extractable text.";
  }

  let firstIndex = -1;
  for (const term of terms) {
    const index = compact.toLowerCase().indexOf(term);
    if (index !== -1 && (firstIndex === -1 || index < firstIndex)) {
      firstIndex = index;
    }
  }

  if (firstIndex === -1) {
    return summarizeInline(compact, 220);
  }

  const start = Math.max(0, firstIndex - 80);
  const end = Math.min(compact.length, firstIndex + 180);
  const prefix = start > 0 ? "..." : "";
  const suffix = end < compact.length ? "..." : "";
  return `${prefix}${compact.slice(start, end).trim()}${suffix}`;
}

function scoreKnowledgeMatch(relativePath, title, body, terms) {
  const pathText = relativePath.toLowerCase();
  const titleText = title.toLowerCase();
  const bodyText = body.toLowerCase();
  let score = 0;

  for (const term of terms) {
    const regex = new RegExp(escapeRegExp(term), "g");
    score += (titleText.match(regex) || []).length * 10;
    score += (pathText.match(regex) || []).length * 6;
    score += (bodyText.match(regex) || []).length * 2;
  }

  return score;
}

async function searchKnowledgeCache(args) {
  const query = String(args.query || "").trim();
  if (!query) {
    throw new Error("search_knowledge_cache requires a non-empty query.");
  }

  const maxResults = toPositiveInt(args.max_results, 5, 1, 20);
  const terms = tokenizeQuery(query);
  if (!terms.length) {
    throw new Error(
      "search_knowledge_cache query must include searchable terms.",
    );
  }

  let files;
  try {
    files = await collectMarkdownFiles(KNOWLEDGE_ROOT);
  } catch (error) {
    if (error && error.code === "ENOENT") {
      return {
        query,
        root: ".github/knowledge",
        total_results: "0",
        results: [],
      };
    }
    throw error;
  }

  const results = [];
  for (const filePath of files) {
    const body = await fs.readFile(filePath, "utf8");
    const relativePath = path.relative(WORKSPACE_ROOT, filePath);
    const title = getMarkdownTitle(
      body,
      path.basename(filePath, path.extname(filePath)),
    );
    const score = scoreKnowledgeMatch(relativePath, title, body, terms);
    if (score <= 0) {
      continue;
    }

    results.push({
      path: relativePath,
      title,
      score,
      snippet: buildKnowledgeSnippet(body, terms),
    });
  }

  results.sort(
    (left, right) =>
      right.score - left.score || left.path.localeCompare(right.path),
  );

  return {
    query,
    root: ".github/knowledge",
    total_results: String(results.length),
    results: results.slice(0, maxResults).map((result, index) => ({
      rank: index + 1,
      path: result.path,
      title: result.title,
      snippet: result.snippet,
    })),
  };
}

async function readKnowledgeNote(args) {
  const notePath = String(args.path || "").trim();
  if (!notePath) {
    throw new Error("read_knowledge_note requires a non-empty path.");
  }

  const resolvedPath = path.resolve(WORKSPACE_ROOT, notePath);
  const relativeToRoot = path.relative(KNOWLEDGE_ROOT, resolvedPath);
  if (relativeToRoot.startsWith("..") || path.isAbsolute(relativeToRoot)) {
    throw new Error(
      "read_knowledge_note only allows files under .github/knowledge/.",
    );
  }

  const maxChars = toPositiveInt(args.max_chars, 4000, 500, 20000);
  const text = await fs.readFile(resolvedPath, "utf8");

  return {
    path: path.relative(WORKSPACE_ROOT, resolvedPath),
    title: getMarkdownTitle(
      text,
      path.basename(resolvedPath, path.extname(resolvedPath)),
    ),
    text: summarizeText(text.trim(), maxChars),
  };
}

function resolveKnowledgePath(notePath) {
  const resolvedPath = path.resolve(WORKSPACE_ROOT, notePath);
  const relativeToRoot = path.relative(KNOWLEDGE_ROOT, resolvedPath);
  if (relativeToRoot.startsWith("..") || path.isAbsolute(relativeToRoot)) {
    throw new Error("Knowledge note path must be under .github/knowledge/.");
  }
  if (!resolvedPath.endsWith(".md")) {
    throw new Error("Knowledge notes must be .md files.");
  }
  return resolvedPath;
}

async function writeKnowledgeNote(args) {
  const notePath = String(args.path || "").trim();
  if (!notePath) {
    throw new Error("write_knowledge_note requires a non-empty path.");
  }

  const content = String(args.content || "").trim();
  if (!content) {
    throw new Error("write_knowledge_note requires non-empty content.");
  }

  const resolvedPath = resolveKnowledgePath(notePath);

  let exists = false;
  try {
    await fs.access(resolvedPath);
    exists = true;
  } catch {
    // file doesn't exist — will create
  }

  if (exists && !args.overwrite) {
    throw new Error(
      `File already exists: ${path.relative(WORKSPACE_ROOT, resolvedPath)}. Set overwrite=true to replace it.`,
    );
  }

  await fs.mkdir(path.dirname(resolvedPath), { recursive: true });
  await fs.writeFile(resolvedPath, content, "utf8");

  return {
    action: exists ? "overwritten" : "created",
    path: path.relative(WORKSPACE_ROOT, resolvedPath),
  };
}

async function updateKnowledgeNote(args) {
  const notePath = String(args.path || "").trim();
  if (!notePath) {
    throw new Error("update_knowledge_note requires a non-empty path.");
  }

  const heading = String(args.heading || "").trim();
  if (!heading) {
    throw new Error(
      "update_knowledge_note requires a heading to locate the section.",
    );
  }

  const content = String(args.content || "").trim();
  if (!content) {
    throw new Error("update_knowledge_note requires non-empty content.");
  }

  const resolvedPath = resolveKnowledgePath(notePath);
  const text = await fs.readFile(resolvedPath, "utf8");

  // Match the heading line (any heading level)
  const escapedHeading = escapeRegExp(heading);
  const headingRegex = new RegExp(`^(#{1,6})\\s+${escapedHeading}\\s*$`, "m");
  const headingMatch = headingRegex.exec(text);
  if (!headingMatch) {
    throw new Error(
      `Heading "${heading}" not found in ${path.relative(WORKSPACE_ROOT, resolvedPath)}.`,
    );
  }

  const headingLevel = headingMatch[1].length;
  const sectionStart = headingMatch.index + headingMatch[0].length;

  // Find the next heading at the same or higher level
  const nextHeadingRegex = new RegExp(`^#{1,${headingLevel}}\\s+`, "m");
  const rest = text.slice(sectionStart);
  const nextMatch = nextHeadingRegex.exec(rest);
  const sectionEnd = nextMatch ? sectionStart + nextMatch.index : text.length;

  const updated =
    text.slice(0, sectionStart) +
    "\n" +
    content +
    "\n\n" +
    text.slice(sectionEnd);
  await fs.writeFile(resolvedPath, updated, "utf8");

  return {
    action: "updated",
    path: path.relative(WORKSPACE_ROOT, resolvedPath),
    heading,
  };
}

async function appendToKnowledgeNote(args) {
  const notePath = String(args.path || "").trim();
  if (!notePath) {
    throw new Error("append_to_knowledge_note requires a non-empty path.");
  }

  const content = String(args.content || "").trim();
  if (!content) {
    throw new Error("append_to_knowledge_note requires non-empty content.");
  }

  const resolvedPath = resolveKnowledgePath(notePath);
  const existing = await fs.readFile(resolvedPath, "utf8");
  const separator = existing.endsWith("\n") ? "\n" : "\n\n";
  await fs.writeFile(
    resolvedPath,
    existing + separator + content + "\n",
    "utf8",
  );

  return {
    action: "appended",
    path: path.relative(WORKSPACE_ROOT, resolvedPath),
  };
}

async function fetchJson(url) {
  const response = await fetch(url, {
    headers: {
      "user-agent": DEFAULT_USER_AGENT,
      accept: "application/json",
    },
  });

  if (!response.ok) {
    const body = await response.text();
    throw new Error(
      `HTTP ${response.status} from ${url}: ${body.slice(0, 400)}`,
    );
  }

  return response.json();
}

async function fetchText(url) {
  const response = await fetch(url, {
    headers: {
      "user-agent": DEFAULT_USER_AGENT,
      accept:
        "text/html,application/xhtml+xml;q=0.9,text/plain;q=0.8,*/*;q=0.5",
    },
    redirect: "follow",
  });

  if (!response.ok) {
    throw new Error(`HTTP ${response.status} while fetching ${url}`);
  }

  return response.text();
}

function decodeJsEscapes(text) {
  return decodeHtmlEntities(
    text
      .replace(/\\u003C/gi, "<")
      .replace(/\\u003E/gi, ">")
      .replace(/\\u0026/gi, "&")
      .replace(/\\u0027/gi, "'")
      .replace(/\\u0022/gi, '"')
      .replace(/\\n/g, "\n")
      .replace(/\\\//g, "/")
      .replace(/\\"/g, '"')
      .replace(/\\'/g, "'"),
  );
}

function extractBraveResults(html, limit) {
  const results = [];
  const sectionMatch = html.match(
    /web:\{type:"search"[\s\S]*?results:\[([\s\S]*?)\],bo_left_right_divisive/s,
  );
  if (!sectionMatch) {
    return results;
  }

  const objectRegex =
    /title:"([\s\S]*?)",url:"([\s\S]*?)",full_title:[\s\S]*?description:"([\s\S]*?)",page_age:/g;

  let match;
  while (
    (match = objectRegex.exec(sectionMatch[1])) &&
    results.length < limit
  ) {
    const title = decodeJsEscapes(match[1])
      .replace(/<[^>]+>/g, " ")
      .trim();
    const url = decodeJsEscapes(match[2]).trim();
    const snippet = decodeJsEscapes(match[3])
      .replace(/<[^>]+>/g, " ")
      .trim();
    if (!url || !title) {
      continue;
    }

    results.push({
      rank: results.length + 1,
      title,
      url,
      display_url: url,
      snippet,
    });
  }

  return results;
}

async function searchWeb(args) {
  const query = String(args.query || "").trim();
  if (!query) {
    throw new Error("search_web requires a non-empty query.");
  }

  const num = toPositiveInt(args.num_results, 5, 1, 10);
  const terms = [query];

  if (args.site_filter) {
    terms.push(`site:${String(args.site_filter).trim()}`);
  }
  if (args.exact_terms) {
    terms.push(`\"${String(args.exact_terms).trim()}\"`);
  }
  if (args.exclude_terms) {
    for (const term of String(args.exclude_terms)
      .split(/\s+/)
      .filter(Boolean)) {
      terms.push(`-${term}`);
    }
  }
  if (args.file_type) {
    terms.push(`filetype:${String(args.file_type).trim()}`);
  }

  const searchUrl = `https://search.brave.com/search?q=${encodeURIComponent(terms.join(" "))}&source=web`;
  const html = await fetchText(searchUrl);
  const results = extractBraveResults(html, num);

  return {
    query: terms.join(" "),
    provider: "brave-search",
    total_results: String(results.length),
    results,
  };
}

async function googleSearch(args) {
  const apiKey = requireEnv("GOOGLE_CUSTOM_SEARCH_API_KEY");
  const searchEngineId = requireEnv("GOOGLE_CUSTOM_SEARCH_ENGINE_ID");

  const query = String(args.query || "").trim();
  if (!query) {
    throw new Error("google_search requires a non-empty query.");
  }

  const num = toPositiveInt(args.num_results, 5, 1, 10);
  const params = new URLSearchParams({
    key: apiKey,
    cx: searchEngineId,
    q: query,
    num: String(num),
  });

  if (args.site_filter) {
    params.set("siteSearch", String(args.site_filter));
  }
  if (args.exact_terms) {
    params.set("exactTerms", String(args.exact_terms));
  }
  if (args.exclude_terms) {
    params.set("excludeTerms", String(args.exclude_terms));
  }
  if (args.file_type) {
    params.set("fileType", String(args.file_type));
  }
  if (args.language) {
    params.set("lr", `lang_${String(args.language).replace(/^lang_/, "")}`);
  }
  if (args.date_restrict) {
    params.set("dateRestrict", String(args.date_restrict));
  }

  const payload = await fetchJson(
    `https://customsearch.googleapis.com/customsearch/v1?${params.toString()}`,
  );

  const items = Array.isArray(payload.items) ? payload.items : [];
  const results = items.map((item, index) => ({
    rank: index + 1,
    title: item.title || "Untitled",
    url: item.link,
    display_url: item.displayLink || item.formattedUrl || item.link,
    snippet: item.snippet || "",
  }));

  return {
    query,
    total_results:
      payload.searchInformation?.totalResults || String(results.length),
    search_time_seconds: payload.searchInformation?.searchTime || null,
    results,
  };
}

async function fetchPages(args) {
  const urls = Array.isArray(args.urls) ? args.urls : [];
  if (!urls.length) {
    throw new Error("fetch_pages requires at least one URL.");
  }

  const maxPages = toPositiveInt(urls.length, urls.length, 1, 5);
  const maxChars = toPositiveInt(args.max_chars, 8000, 1000, 20000);

  const pages = [];
  for (const rawUrl of urls.slice(0, maxPages)) {
    const url = String(rawUrl).trim();
    if (!url) {
      continue;
    }

    const html = await fetchText(url);
    const title = getTitle(html);
    const text = summarizeText(stripHtml(html), maxChars);

    pages.push({
      url,
      title,
      text,
    });
  }

  return { pages };
}

function formatGoogleSearchResult(result) {
  const lines = [
    `Query: ${result.query}`,
    `Total results: ${result.total_results}`,
  ];

  if (result.search_time_seconds != null) {
    lines.push(`Search time: ${result.search_time_seconds}s`);
  }

  lines.push("", "Results:");

  for (const item of result.results) {
    lines.push(`${item.rank}. ${item.title}`);
    lines.push(`   URL: ${item.url}`);
    if (item.snippet) {
      lines.push(`   Snippet: ${item.snippet}`);
    }
  }

  if (!result.results.length) {
    lines.push("No results returned.");
  }

  return lines.join("\n");
}

function formatKnowledgeSearchResult(result) {
  const lines = [
    `Query: ${result.query}`,
    `Knowledge root: ${result.root}`,
    `Total results: ${result.total_results}`,
    "",
    "Results:",
  ];

  for (const item of result.results) {
    lines.push(`${item.rank}. ${item.title}`);
    lines.push(`   Path: ${item.path}`);
    if (item.snippet) {
      lines.push(`   Snippet: ${item.snippet}`);
    }
  }

  if (!result.results.length) {
    lines.push("No cached knowledge notes matched.");
  }

  return lines.join("\n");
}

function formatKnowledgeNoteResult(result) {
  return [
    `Title: ${result.title}`,
    `Path: ${result.path}`,
    "",
    result.text || "No text available.",
  ].join("\n");
}

function formatKnowledgeWriteResult(result) {
  const lines = [`Action: ${result.action}`, `Path: ${result.path}`];
  if (result.heading) {
    lines.push(`Heading: ${result.heading}`);
  }
  return lines.join("\n");
}

function formatFetchPagesResult(result) {
  const lines = [];

  result.pages.forEach((page, index) => {
    if (index > 0) {
      lines.push("", "---", "");
    }
    lines.push(`Title: ${page.title}`);
    lines.push(`URL: ${page.url}`);
    lines.push("", page.text || "No extractable text.");
  });

  return lines.join("\n");
}

async function handleRequest(request) {
  const { id, method } = request;

  if (method === "initialize") {
    send({
      jsonrpc: "2.0",
      id,
      result: {
        protocolVersion: MCP_VERSION,
        capabilities: { tools: {} },
        serverInfo: { name: "aioserver-research", version: "1.0.0" },
      },
    });
    return;
  }

  if (method === "notifications/initialized") {
    return;
  }

  if (method === "tools/list") {
    send({
      jsonrpc: "2.0",
      id,
      result: {
        tools: [
          {
            name: "search_knowledge_cache",
            description:
              "Search the repo-local durable knowledge cache under .github/knowledge/ and return matching note paths with snippets.",
            inputSchema: {
              type: "object",
              properties: {
                query: {
                  type: "string",
                  description: "Search query.",
                },
                max_results: {
                  type: "integer",
                  description: "Number of note matches to return (1-20).",
                },
              },
              required: ["query"],
            },
          },
          {
            name: "read_knowledge_note",
            description:
              "Read a specific knowledge note from .github/knowledge/ using a workspace-relative path.",
            inputSchema: {
              type: "object",
              properties: {
                path: {
                  type: "string",
                  description:
                    "Workspace-relative path to a note under .github/knowledge/.",
                },
                max_chars: {
                  type: "integer",
                  description:
                    "Maximum number of characters to return (500-20000). Default 4000.",
                },
              },
              required: ["path"],
            },
          },
          {
            name: "search_web",
            description:
              "Search the web without credentials and return structured results with titles and URLs. Best for documentation discovery when no dedicated API key is configured.",
            inputSchema: {
              type: "object",
              properties: {
                query: {
                  type: "string",
                  description: "Search query.",
                },
                num_results: {
                  type: "integer",
                  description: "Number of results to return (1-10).",
                },
                site_filter: {
                  type: "string",
                  description:
                    "Optional domain to scope the search, for example gbatek.com or problemkaputt.de.",
                },
                exact_terms: {
                  type: "string",
                  description: "Terms that must appear in results.",
                },
                exclude_terms: {
                  type: "string",
                  description: "Terms that should be excluded from results.",
                },
                file_type: {
                  type: "string",
                  description: "Optional file type filter, for example pdf.",
                },
              },
              required: ["query"],
            },
          },
          {
            name: "google_search",
            description:
              "Search the web through Google Custom Search and return structured results with titles, URLs, and snippets. Requires an existing Programmable Search Engine and API key.",
            inputSchema: {
              type: "object",
              properties: {
                query: {
                  type: "string",
                  description: "Search query.",
                },
                num_results: {
                  type: "integer",
                  description: "Number of results to return (1-10).",
                },
                site_filter: {
                  type: "string",
                  description:
                    "Optional domain to scope the search, for example gbatek.com or problemkaputt.de.",
                },
                exact_terms: {
                  type: "string",
                  description: "Terms that must appear in results.",
                },
                exclude_terms: {
                  type: "string",
                  description: "Terms that should be excluded from results.",
                },
                file_type: {
                  type: "string",
                  description: "Optional file type filter, for example pdf.",
                },
                language: {
                  type: "string",
                  description: "Optional language code, for example en.",
                },
                date_restrict: {
                  type: "string",
                  description:
                    "Optional Google date restriction, for example m6, y1, or d30.",
                },
              },
              required: ["query"],
            },
          },
          {
            name: "write_knowledge_note",
            description:
              "Create or overwrite a knowledge note under .github/knowledge/. Use for persisting research findings, architecture facts, or debug lessons learned.",
            inputSchema: {
              type: "object",
              properties: {
                path: {
                  type: "string",
                  description:
                    "Workspace-relative path for the note, e.g. .github/knowledge/my-topic.md",
                },
                content: {
                  type: "string",
                  description: "Full markdown content to write.",
                },
                overwrite: {
                  type: "boolean",
                  description:
                    "Set to true to replace an existing file. Default false (fails if file exists).",
                },
              },
              required: ["path", "content"],
            },
          },
          {
            name: "update_knowledge_note",
            description:
              "Replace a specific section (identified by heading) in an existing knowledge note. Preserves all other sections.",
            inputSchema: {
              type: "object",
              properties: {
                path: {
                  type: "string",
                  description:
                    "Workspace-relative path to the note under .github/knowledge/.",
                },
                heading: {
                  type: "string",
                  description:
                    "Exact text of the heading to replace (without the # prefix).",
                },
                content: {
                  type: "string",
                  description:
                    "New content to place under the heading. The heading line is preserved; only the body below it is replaced.",
                },
              },
              required: ["path", "heading", "content"],
            },
          },
          {
            name: "append_to_knowledge_note",
            description:
              "Append content to the end of an existing knowledge note. Use when adding new findings to an existing topic.",
            inputSchema: {
              type: "object",
              properties: {
                path: {
                  type: "string",
                  description:
                    "Workspace-relative path to the note under .github/knowledge/.",
                },
                content: {
                  type: "string",
                  description:
                    "Markdown content to append at the end of the file.",
                },
              },
              required: ["path", "content"],
            },
          },
          {
            name: "fetch_pages",
            description:
              "Fetch up to 5 web pages and return cleaned text extracts for fast documentation review.",
            inputSchema: {
              type: "object",
              properties: {
                urls: {
                  type: "array",
                  items: { type: "string" },
                  minItems: 1,
                  maxItems: 5,
                  description: "Absolute URLs to fetch.",
                },
                max_chars: {
                  type: "integer",
                  description:
                    "Maximum extracted text per page (1000-20000). Default 8000.",
                },
              },
              required: ["urls"],
            },
          },
        ],
      },
    });
    return;
  }

  if (method === "tools/call") {
    const toolName = request.params?.name;
    const toolArguments = request.params?.arguments || {};

    try {
      if (toolName === "search_knowledge_cache") {
        const result = await searchKnowledgeCache(toolArguments);
        send({
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: formatKnowledgeSearchResult(result),
              },
            ],
          },
        });
        return;
      }

      if (toolName === "read_knowledge_note") {
        const result = await readKnowledgeNote(toolArguments);
        send({
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: formatKnowledgeNoteResult(result),
              },
            ],
          },
        });
        return;
      }

      if (toolName === "search_web") {
        const result = await searchWeb(toolArguments);
        send({
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: formatGoogleSearchResult(result),
              },
            ],
          },
        });
        return;
      }

      if (toolName === "google_search") {
        const result = await googleSearch(toolArguments);
        send({
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: formatGoogleSearchResult(result),
              },
            ],
          },
        });
        return;
      }

      if (toolName === "write_knowledge_note") {
        const result = await writeKnowledgeNote(toolArguments);
        send({
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: formatKnowledgeWriteResult(result),
              },
            ],
          },
        });
        return;
      }

      if (toolName === "update_knowledge_note") {
        const result = await updateKnowledgeNote(toolArguments);
        send({
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: formatKnowledgeWriteResult(result),
              },
            ],
          },
        });
        return;
      }

      if (toolName === "append_to_knowledge_note") {
        const result = await appendToKnowledgeNote(toolArguments);
        send({
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: formatKnowledgeWriteResult(result),
              },
            ],
          },
        });
        return;
      }

      if (toolName === "fetch_pages") {
        const result = await fetchPages(toolArguments);
        send({
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: formatFetchPagesResult(result),
              },
            ],
          },
        });
        return;
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      sendError(id, -32603, message);
      return;
    }

    sendError(id, -32601, `Unknown tool: ${toolName}`);
    return;
  }

  sendError(id, -32601, `Unknown method: ${method}`);
}

const lineReader = readline.createInterface({
  input: process.stdin,
  crlfDelay: Infinity,
});

lineReader.on("line", async (line) => {
  if (!line.trim()) {
    return;
  }

  try {
    const request = JSON.parse(line);
    await handleRequest(request);
  } catch {
    sendError(null, -32700, "Parse error");
  }
});
