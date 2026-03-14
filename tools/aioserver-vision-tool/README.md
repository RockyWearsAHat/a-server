# AIOServer Vision Tool

This workspace-local VS Code extension contributes one unified image analysis tool (with two naming variants for compatibility):

- `aioserver-analyze-images`
- `aioserver_analyze_images`

It also contributes one chat participant:

- `@aioserver-vision`

The tool accepts 1–10 image paths and a freeform goal. It reads the image bytes from disk, attaches them all as real image data to a vision-capable Copilot model (Claude Sonnet 4.6 by default), and returns the model's analysis. Use it for single-image inspection, before/after comparisons, batch design evaluation, or any visual analysis.

It also provides a manual command:

- `AIOServer: Inspect Screenshot With Copilot Vision` (supports multi-select)

Environment variables:

- `AIOSERVER_VISION_MODEL_IDS`: comma-separated model id/name preferences used when choosing a model. Default: `claude-sonnet-4.6`.
- `AIOSERVER_VISION_ALLOW_UNDECLARED_IMAGE_MODEL`: optional override for undeclared-image fallback. The extension now defaults to allowing the preferred-model fallback because current Copilot model metadata may report `imageInput: false` even when image requests still work. Set this to `0` or `false` to disable that fallback explicitly.

## Why this exists

The `.agent.md` and skill files cannot directly force binary screenshots to be attached as image context during execution. That requires an extension tool or participant using the VS Code Language Model API.

This extension is the bridge between:

- screenshots captured by `scripts/visual_dev_loop.py`
- Copilot models with `imageInput` capability
- the Visual Development Loop agent

## Expected usage

Once the extension is installed in the normal VS Code profile, Copilot Chat can use `@aioserver-vision` for direct image analysis, and the extension exposes the same image-aware analysis through the language model tools above.

## Activation

1. Run `make install-vision-tool` from the repository root.
2. Reload the current VS Code window once so the newly installed extension is activated in the running editor.
3. After that one-time reload, the extension activates at editor startup and the tool should be available to new Copilot sessions without a manual warm-up command.
4. Use `@aioserver-vision /analyze /path/to/image.png :: What to evaluate?` in Copilot Chat for manual analysis.
5. For multi-image: `@aioserver-vision /analyze /path/to/img1.png :: /path/to/img2.png :: Compare these designs`

The separate Extension Development Host path in `.vscode/launch.json` is still available for extension development, but it is no longer required for normal use.

Use `make reinstall-vision-tool` after editing the extension code so the current profile gets the rebuilt VSIX.

## Architecture: two layers, one pipeline

There are **two distinct config files** in `.vscode/` that are easy to confuse. They are not redundant — they serve different layers of the same pipeline.

```
Copilot / Agent
     │
     │  calls tool via MCP protocol
     ▼
.vscode/mcp.json
  → launches: node ./tools/aioserver-vision-tool/mcp-server.js
                │
                │  reads at runtime (every call)
                ▼
        .vscode/aioserver-vision-ipc.json
          { "socketPath": "/tmp/aioserver-vision-*.sock" }
                │
                │  Unix domain socket request
                ▼
        VS Code extension (extension.js, running inside the editor)
          → selects a vision-capable Copilot model (claude-sonnet-4.6 by default)
          → reads all image bytes from disk (up to 10)
          → attaches images to the model via LanguageModelDataPart.image(...)
          → returns the model's response back up the chain
```

### What each file does

| File                                        | Role                                                                                | Written by                       |
| ------------------------------------------- | ----------------------------------------------------------------------------------- | -------------------------------- |
| `.vscode/mcp.json`                          | Registers the MCP server so Copilot/agents can call the vision tool                 | You (static config)              |
| `.vscode/aioserver-vision-ipc.json`         | Runtime service-discovery: holds the Unix socket path to the live extension backend | The VS Code extension at startup |
| `tools/aioserver-vision-tool/mcp-server.js` | MCP protocol layer — receives tool calls, forwards them over the socket             | This repo                        |
| `tools/aioserver-vision-tool/extension.js`  | Vision backend — runs inside the editor, owns the Copilot model API access          | This repo                        |

### Why the split?

Copilot agents running in the chat panel cannot directly call VS Code's `LanguageModelDataPart.image(...)` API — only a running VS Code extension can. The MCP server provides a stable JSON-RPC endpoint that any agent or model can call, and it proxies the request to the extension that actually owns the image-attachment capability.

### Prerequisites for tool calls to succeed

All three of the following must be true at call time:

1. **Extension is installed and active** — run `make install-vision-tool` once, then reload the window. The extension writes `aioserver-vision-ipc.json` when it activates.
2. **`aioserver-vision-ipc.json` exists and has a live socket path** — if this file is missing or stale (e.g. after a window reload without re-activation), the MCP server will fail with `IPC metadata missing socketPath` or a connection error. The fix is to reload the VS Code window so the extension re-activates and rewrites the file.
3. **A vision-capable Copilot model is available** — `claude-sonnet-4.6` by default. Configure via `AIOSERVER_VISION_MODEL_IDS` if needed.

### How agents should call the tool

Agents and models interact with this pipeline through one tool:

**`mcp_aioserver-vis_analyze_images`** — analyze 1–10 images with a freeform goal

```
image_paths: ["/absolute/path/to/screenshot1.png", "/absolute/path/to/screenshot2.png"]
goal:        "Compare these two designs for layout and color consistency"
```

The `mcp_` prefix and the `aioserver-vis` server name come from how VS Code exposes MCP tools to the language model. The underlying tool name registered in `mcp-server.js` is `analyze_images`.

### Common failure modes

| Symptom                                       | Cause                                                | Fix                                                             |
| --------------------------------------------- | ---------------------------------------------------- | --------------------------------------------------------------- |
| `IPC metadata missing socketPath`             | `aioserver-vision-ipc.json` is missing               | Reload VS Code window                                           |
| `Extension IPC timeout` or `ENOENT` on socket | Extension not active, socket stale                   | Reload VS Code window                                           |
| `No chat model with image input capability`   | No vision model available in current Copilot session | Check `AIOSERVER_VISION_MODEL_IDS`; ensure Copilot is signed in |
| Tool not visible to agent                     | `mcp.json` not picked up                             | Restart VS Code; confirm `.vscode/mcp.json` exists              |
