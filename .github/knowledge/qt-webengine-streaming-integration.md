# Qt WebEngine Streaming Integration

## Scope

Reusable Qt 6 WebEngine facts for AIO Server streaming-platform work built on `QWebEngineView`, `QWebEnginePage`, `QWebEngineProfile`, and `QWebEngineSettings`.

## Key Facts

- `QWebEngineView` is the widget-level browser surface. It owns presentation, forwards load/title/icon/url signals, and exposes page actions, zoom, print, and window-creation hooks.
- `QWebEnginePage` owns navigation behavior, page lifecycle, JavaScript execution, permission-related signals, URL interception, and most policy hooks.
- `QWebEngineProfile` owns shared storage, cookies, cache, scripts, download handling, and request interception across pages that use the same profile.
- `QWebEngineSettings` provides per-page settings with fallback to the profile settings when a page does not override a value.
- If you create a page or view with a non-default `QWebEngineProfile`, that profile must outlive the page/view.
- Off-the-record profiles keep cookies and cache in memory only and do not persist storage to disk.
- Profile paths and cache settings should be configured before creating pages that belong to the profile.
- For TV-style navigation, `QWebEngineSettings::FocusOnNavigationEnabled` and `QWebEngineSettings::SpatialNavigationEnabled` are high-signal settings to evaluate.
- Fullscreen video support requires handling `QWebEnginePage::fullScreenRequested` and enabling `QWebEngineSettings::FullScreenSupportEnabled`.
- Pop-up and new-window flows require `createWindow()` or `newWindowRequested()` handling. JavaScript-initiated popups also require `JavascriptCanOpenWindows`.
- URL interception can be done per page or per profile, but profile interception runs first and page interception only sees requests not already blocked or redirected by the profile interceptor.
- `QWebEnginePage::acceptNavigationRequest()` is the main hook for delegating or denying navigation in embedded app flows.
- `runJavaScript()` callbacks are always invoked, but may fire during page destruction; callbacks must not assume the page/view is still safe to touch.
- `setHtml()` and `setContent()` need a base URL for relative resources. Large inline HTML can hit Chromium size limits after percent-encoding.

## AIO Server Implications

- Streaming-platform stability and behavior should usually be shaped at the `QWebEnginePage` and `QWebEngineProfile` layers, not by ad hoc widget-only fixes.
- Shared streaming-service behavior such as cookies, storage, cache policy, and request interception belongs in profile setup.
- D-pad/remote UX issues should be evaluated against focus/navigation settings and page-level navigation hooks before adding brittle JavaScript workarounds.

## Sources

- Qt 6.10 `QWebEngineView` documentation
- Qt 6.10 `QWebEnginePage` documentation
- Qt 6.10 `QWebEngineProfile` documentation
- Qt 6.10 `QWebEngineSettings` documentation

## Last Verified

- 2026-03-14
