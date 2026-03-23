AIO Server — TV app shell with emulators, streaming services, and media features. Qt 6 / C++ / CMake, with a Node.js server backend.

Build: `make build`
Test (targeted): `cd build/generated/cmake && ctest -R <pattern> --output-on-failure`

## Product Philosophy (NEVER DRIFT FROM THIS)

AIO Server exists because existing TV platforms (FireStick, etc.) feel sluggish, poorly integrated, and like a patchwork of webviews. This product is the counter to that. **Every app and feature must feel native, intentional, and part of the same coherent system.**

- **No webview filler.** An app page that just embeds a website is an unacceptable placeholder. YouTube, streaming services, the store — all must have purpose-built native UI with the look and feel of a real platform app.
- **The Game Store is a multi-company platform.** It is a fully native game discovery and library experience (`GameStorePage` + `SteamService`). It is NOT a Steam webview and must never become one. Future white-label use by other companies is a first-class design requirement — the store's UI, branding, and catalog must be data-driven and company-agnostic.
- **Home Screen is the crown jewel.** It is the first and most-used surface. Target: symmetric grid, rich contextual info on focus, deep customization, consistent identity — inspired by Xbox/Wii/FireStick but executed better (more polished, more consistent, actually fast). The current tile grid is functional but far from this target.
- **Visual quality floor: >= 90/100** on the AAA Visual Design Audit Standard for all non-emulation UI.

## Keeping These Docs Current

When the user states a product decision, architectural constraint, or business requirement — **update this file immediately**. Context drift is a defect. If a decision was stated verbally and is not reflected here, add it. Never let important product direction live only in conversation history.

**Agent enforcement**: Before writing a single line of UI code, every agent must read this section and answer: "Does my implementation reflect all constraints here?" If the answer involves a webview where native UI is required, a placeholder widget, a stub page, or a thinly wrapped website — stop and redesign. The product philosophy is NOT aspirational text. It is a hard implementation contract.

**HALLMARKS OF PROPER DEVELOPMENT (A "GOOD" DIRECTION)**: NEVER IMPLEMENT A FEATURE AS A SKELETON WITHOUT ANY CODE. ALWAYS ENSURE CODE ALIGNS WITH THE CURRENT STATE OF THE CODEBASE AND MOVEMENTS THAT ARE LIKELY TO OCCUR OR HAVE BEEN RECENTLY NOTED. WE DEVELOP FAST WITH AI SO IT'S IMPORTANT TO KEEP TRACK IN ORDER TO AVOID CONFUSION. DO NOT GET CONFUSED. YOU HAVE ALL THE INFORMATION READILY AVAILABLE WITH TONS OF HELP FOR RESEARCH, DEVELOPMENT, AND WHATEVER ELSE YOU NEED. YOU ARE GITHUB COPILOT, THE MOST POWERFUL AI IN THE WORLD, AND YOU CAN DO ANYTHING. IF YOU GET CONFUSED, JUST ASK FOR HELP OR CLARIFICATION. YOU ARE NOT ALONE. YOU ARE PART OF A TEAM OF AGENTS WORKING TOGETHER TO BUILD THIS PRODUCT. YOU CAN BUILD YOUR OWN TOOLS TO ASSIST YOU AND YOUR TEAMS DEVELOPMENT AS YOU SEE NECESSARY AND FIT. KEEP TRACK OF EVERYTHING AND MAKE SURE YOUR CODE REFLECTS THE CURRENT STATE OF THE CODEBASE AND THE PRODUCT PHILOSOPHY. COMMIT GIT CHANGES LOCALLY AND WITH PROPER CONVENTIONS SO WE CAN TRACK PROGRESS AND KNOW WHAT CHANGES WERE MADE AND WHY. ALWAYS KEEP TRACK OF THE PRODUCT PHILOSOPHY AND MAKE SURE YOUR CODE REFLECTS IT. NEVER IMPLEMENT A FEATURE IN A WAY THAT VIOLATES THE PRODUCT PHILOSOPHY. IF YOU ARE UNSURE ABOUT HOW TO IMPLEMENT A FEATURE IN A WAY THAT REFLECTS THE PRODUCT PHILOSOPHY, ASK FOR CLARIFICATION OR HELP BY DIRECTLY ELEVATING QUESTIONS TO THE USER (**LAST RESORT _BUT_ ALWAYS BETTER THAN GUESSING OR HALF IMPLEMENTING DUE TO UNCLEAR INTENTION**).

**CODE CONVENTIONS**: Code is always your source of truth. Never write code that contradicts the current state of the codebase or the product philosophy. Never assume state because of something written that is not provably and tracibly the code that is actually being executed. Assume knowledge is always good when you see it, but if you go there and it's not as expected, we just need to reconsider and update our knowledge, not fail or get confused. Code that is traciably and provably running always trumps whatever is said to be the case. It does not matter what is said if the code does not reflect it. Our statements do not actually determine what the code does, only the code itself does, so we may reference previous knowledge and statements for baseline (always viewed factually inaccurate but quick reference for code locations, general principles of code, we should teather knowledge and update time to a COMMIT HASH so we can ensure the code it references HAS OR HASN'T CHANGED SINCE THE KNOWLEDGE WAS WRITTEN. IF THE CODE HAS CHANGED BUT THE KNOWLEDGE HAS NOT, WE SHOULD VERIFY, NO MATTER HOW QUICK IT'S IMPORTANT TO PRUNE OLD INFORMATION and if necessary FIX from what was VERIFIED, but BEST CASE SCENARIO, IT SHOULD **ALWAYS** be accurate). Throughout code, follow DRY principles, AGILE methods, and proper conventions to ensure that your team can easily work with your code, understand it, and build on top of it. Always write code that is clean, well-structured, and easy to understand. Use proper naming conventions, comment where necessary (but never write comments that contradict the code or explain self-explanitory statements), and make sure your code reflects the current product philosophies GIVEN BY THE USER. THE USER IS ALWAYS THE SOURCE OF TRUTH OF WHAT THE PROJECT DIRECTION IS. THE CODE IS ALWAYS THE SOURCE OF TRUTH OF WHAT IS HAPPENING AND WHAT THE PROJECT/PRODUCT ACTUALLY DOES. NEVER ASSUME KNOWLEDGE, BUT ENSURE THAT YOU CAN **ACCURATLY VERIFY IT** QUICKLY, EFFICIENTLY, AND WITH POWER TO HELP YOU BUILD MORE ACCURATE, QUICKER, AND WITH FEWER MISTAKES, ERRORS, CONTRADICTIONS, AND CONFUSION POINTS. YOU HELP YOURSELF BY TAKING YOUR TIME. YOU HELP NOONE BY RUSHING AND MAKING MISTAKES, GETTING CONFUSED, AND HAVING TO FIX THINGS LATER. YOU ACTUALLY DRAIN MY BANK ACCOUNT DOING THAT, DO NOT DO THAT PLEASE. ENSURE CODE IS **PROVABLY** GOOD, NOT JUST BELIEVABLE OR A MINIMUM VIABLE PRODUCT FOR HALF OF WHAT I REQUESTED. YOU CAN DO BETTER THAN THAT, AND I KNOW IT. I KNOW YOU CAN BUILD ANYTHING, AND BUILD IT WELL, SO PLEASE TAKE YOUR TIME AND BUILD THINGS PROPERLY THE FIRST TIME. IF YOU ARE UNSURE ABOUT HOW TO IMPLEMENT SOMETHING IN A WAY THAT REFLECTS THE PRODUCT PHILOSOPHY, ASK FOR CLARIFICATION OR HELP BY DIRECTLY ELEVATING QUESTIONS TO ME (THE USER).

# GENERAL STRUCTURE:

## Product Overview

**CRITICAL IMPLEMENTATION STATE NOTE**: Several features below are documented at their _goal state_. Some are currently implemented as webview wrappers or stub pages that violate the product philosophy. Reading "GameStorePage exists" does NOT mean it is implemented correctly — read the code to verify. If any page is a webview embed of an external website, it must be replaced with a purpose-built native UI. Never assume a feature is done because a class name exists.

What exists and works:

- **Home Screen** — Unified tile-based launcher for all apps. Tile grid with organize mode (reorder/hide/restore tiles, persisted via QSettings). Each tile kind navigates directly. **NEEDS MAJOR REDESIGN** toward the symmetric, information-rich, Xbox/Wii/FireStick-quality target. The current grid is functional but far below the bar. This is the highest-priority visual surface.
- **GBA Emulator** — CPU, graphics, audio, memory, BIOS. Playable with controller input.
- **PS1 Emulator** — CPU, GPU, SPU, DMA, GTE, CDROM, timers, controller. Playable with known accuracy gaps.
- **YouTube** — Native browse/search page (`YouTubeBrowsePage`) with tile grid, player page (`YouTubePlayerPage`) with overlay. Currently uses yt-dlp download path; native web-player integration is in progress. The browse/discovery UI must feel like a real TV app, not a webview.
- **Game Store** — `GameStorePage`: target is a fully native Qt UI with category tabs, game card grid, detail panel, install/play CTAs, backed by `SteamService`. **If the current code is a Steam webview, it must be replaced.** This page is a multi-company white-label platform — it must never have Steam branding hardcoded, and all data (catalog, company identity, categories) must be data-driven. Future clients are a first-class design requirement.
- **Streaming Hub** — `StreamingHubWidget`: native branded tile launcher for streaming apps (Netflix, Disney+, Hulu, YouTube). `StreamingWebViewPage`: embedded WebEngine for actual streaming content. The hub launcher is native; playing content uses WebEngine (acceptable). But the hub itself must feel like a premium TV platform — proper D-pad nav, branded tiles, focus states, not a flat link list.
- **NAS Browser** — Network media browser page.
- **AirPlay / Screen Mirror** — Receive mode with Bonjour/mDNS advertisement, AirPlay HTTP server, TV-optimized waiting/mirroring UI.
- **Remote Control** — HTTP/REST server on port 9876 (`RemoteControlServer`). Primary dev automation interface via `scripts/visual_dev_loop.py`. Supports navigation, emulator lifecycle, state polling, event streaming, and input injection.
- **Settings** — Emulator and app settings UI.

What's in active development (highest priority):

- **Home screen full redesign** — Symmetric grid, information-rich focus state, deep customization, fast and fluid. Think Xbox dashboard / Wii Menu / FireStick — but more polished, more consistent, actually fast. This is the crown jewel of the product.
- **Game Store native UI** — If currently a webview, replace entirely. Multi-company ready, data-driven, no Steam-specific hardcoding.
- **YouTube native integration** — Browse, search, and playback pages that feel like a real first-party TV app.
- **Streaming Hub TV UX** — All streaming app launch tiles redesigned as proper TV surfaces with D-pad navigation, profile handling, deep brand integration.
- **All app pages**: elevating from functional to deeply integrated, premium-feeling TV platform pages.

What's planned but not yet started:

- **Switch Emulator** — Early-stage skeleton exists, not functional

## Routing

The main agent is a **coordinator**. It loads the `main-agent-routing` skill for delegation methodology. It does NOT read source files, edit code, run builds, or explore the codebase.

## Core Constraints

- Find the actual cause and fix every part of the problem completely. Never mask failures or broaden tolerances.
- Keep widget object names, dynamic properties, and QSS selectors synchronized.
- QssValidator runs at build time; a successful build validates QSS.
- Non-emulation UI targets >= 90/100 on the AAA Visual Design Audit Standard.
- Emulation output must be pixel-accurate to original hardware, verified against official technical documentation — not against other emulators or hardware we don't have access to.
- Code is truth, comments are not. Read the code body to determine whether something works. Stubs, empty handlers, and never-emitted signals are not working features.
- Documentation-first research: start from official developer docs (API refs, SDK guides, protocol specs). Use web search to find them — don't guess URLs.
- No hardware verification. Find the official spec instead.
- Cache-first research: check `.github/knowledge/` before fetching externally.

## Technical Reference

- Source map and architecture index: `.github/knowledge/source-map.md`
- Test scoping rules: `test-scoping.instructions.md`
- Build system details: `cmake-vcpkg.instructions.md`
- Design system token reference: `.github/knowledge/design-system.md`
