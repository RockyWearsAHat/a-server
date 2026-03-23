# AIOServer Research Tool

Workspace-local MCP server for documentation-first research and repo-local knowledge retrieval.

It exposes the `aioserver-research/*` namespace with these tools:

- `search_web` — no-credential web search for documentation discovery
- `search_knowledge_cache` — search `.github/knowledge/` for durable repo notes
- `read_knowledge_note` — read a specific `.github/knowledge/` note by relative path
- `write_knowledge_note` — create or overwrite a `.github/knowledge/` note
- `update_knowledge_note` — replace a specific section (by heading) in an existing note
- `append_to_knowledge_note` — append content to an existing note
- `google_search` — Google Custom Search results with titles, URLs, and snippets
- `fetch_pages` — fast cleaned text extraction for selected pages

## Why this exists

The built-in web fetch capability is useful once you already know which URLs matter. This server adds the missing search step for external docs and a reliable search/read path for the repo-local durable knowledge cache.

## Configuration

`search_web`, `search_knowledge_cache`, `read_knowledge_note`, `write_knowledge_note`, `update_knowledge_note`, `append_to_knowledge_note`, and `fetch_pages` work without credentials.

Set these environment variables only if you want to use `google_search`:

- `GOOGLE_CUSTOM_SEARCH_API_KEY`
- `GOOGLE_CUSTOM_SEARCH_ENGINE_ID`

These come from two different Google surfaces:

1. `GOOGLE_CUSTOM_SEARCH_API_KEY`
   Source: Google Cloud Console -> `APIs & Services` -> `Credentials` -> `Create credentials` -> `API key`
   Requirement: the `Custom Search API` must be enabled for the same project.

2. `GOOGLE_CUSTOM_SEARCH_ENGINE_ID`
   Source: Google Programmable Search Engine (`programmablesearchengine.google.com`)
   Value name: `Search engine ID`

Google's naming is confusing because the API is still called `Custom Search API` while the search-engine product is now usually labeled `Programmable Search Engine`. This tool uses both: the API key from Google Cloud and the search engine ID from the Programmable Search Engine control panel.

Important: Google's `Custom Search JSON API` documentation currently states that the API is closed to new customers and remains available only for existing customers through January 1, 2027. Because of that, `search_web` is the default recommended search path for this repo, and `google_search` should be treated as optional legacy support.

If you want broad web results, create a Programmable Search Engine configured to search the entire web rather than a single site. Then narrow specific calls with the `site_filter` parameter when needed.

`fetch_pages` does not require Google credentials.

## Usage

The workspace registers this server in `.vscode/mcp.json` as `aioserver-research`.

Recommended flow:

1. `search_knowledge_cache` to check whether the answer is already captured in `.github/knowledge/`.
2. `read_knowledge_note` for the most relevant cached note.
3. If the cache is missing or stale, use `search_web` with a focused query and optional site filter.
4. `fetch_pages` on the most promising result URLs.
5. Summarize the evidence and only then propose code changes.

If you already have a working Google Programmable Search Engine setup, you can substitute `google_search` for step 1.

Example searches:

- `query`: `ARM7TDMI pipeline timing gba`, `site_filter`: `problemkaputt.de`
- `query`: `MIPS R3000A DMA sync`, `site_filter`: `psx-spx.consoledev.net`
- `query`: `Qt 6 QWebEngine keyboard focus navigation tv ui`

## Notes

- `search_web` is the default search path and does not need credentials.
- `search_knowledge_cache` searches only `.github/knowledge/` in the current workspace.
- `read_knowledge_note` reads only files under `.github/knowledge/`.
- `google_search` returns at most 10 results per call.
- `fetch_pages` returns at most 5 page extracts per call.
- The page extraction is intentionally lightweight and optimized for quick technical reading, not perfect article rendering.
