# AIOServer Vision Tool

This workspace-local VS Code extension contributes two equivalent language model tools:

- `aioserver-inspect-screenshot`
- `aioserver_inspect_screenshot`

It also contributes one chat participant:

- `@aioserver-vision`

Both tool names read a screenshot or frame image from disk, attach the real image bytes to a vision-capable Copilot model with `LanguageModelDataPart.image(...)`, and return the model's analysis as a tool result.

It also provides a manual command:

- `AIOServer: Inspect Screenshot With Copilot Vision`

It also starts a local bridge endpoint on activation:

- `http://127.0.0.1:39377/inspect` (POST JSON)
- `http://127.0.0.1:39377/health` (GET)

Environment variables:

- `AIOSERVER_VISION_MODEL_IDS`: comma-separated model id/name preferences used when choosing a model. Default: `claude-haiku-4.5,claude-haiku-4`.
- `AIOSERVER_VISION_ALLOW_UNDECLARED_IMAGE_MODEL`: when set to `1`/`true`, the extension will attempt the preferred model even if `imageInput` capability is reported as false.
- `AIOSERVER_VISION_BRIDGE_PORT`: optional bridge port override (default `39377`).

## Why this exists

The `.agent.md` and skill files cannot directly force binary screenshots to be attached as image context during execution. That requires an extension tool or participant using the VS Code Language Model API.

This extension is the bridge between:

- screenshots captured by `scripts/visual_dev_loop.py`
- Copilot models with `imageInput` capability
- the Visual Development Loop agent

## Expected usage

Once the extension is installed in the normal VS Code profile, Copilot Chat can use `@aioserver-vision` for direct screenshot inspection and the extension can expose the same image-aware inspection internally through `aioserver-inspect-screenshot`.

## Activation

1. Run `make install-vision-tool` from the repository root.
2. Reload the current VS Code window once so the newly installed extension is activated in the running editor.
3. After that one-time reload, the extension activates at editor startup and the tool should be available to new Copilot sessions without a manual warm-up command.
4. Use `@aioserver-vision /inspect /absolute/path/to/image.png :: question` in Copilot Chat when you want the manual participant flow.

The separate Extension Development Host path in `.vscode/launch.json` is still available for extension development, but it is no longer required for normal use.

Use `make reinstall-vision-tool` after editing the extension code so the current profile gets the rebuilt VSIX.

## Runtime Bridge Fallback

If Copilot agent tool routing does not expose `aioserver-inspect-screenshot`, you can still route image inspection through the extension using the local bridge.

Request shape:

```json
{
  "imagePath": "/absolute/path/to/frame.png",
  "question": "Is Crash visible on the title screen?",
  "context": "Optional extra context"
}
```

Example via helper script:

```bash
python3 scripts/inspect_screenshot_bridge.py \
	--image "/Users/alexwaldmann/Desktop/AIO Server/test_output/visual_verification/crash_bandicoot/20260312_latest_local/frame_30000ms.png" \
	--question "Is the expected Crash title/menu 3D character visible?"
```

Example via curl:

```bash
curl -sS -X POST "http://127.0.0.1:39377/inspect" \
	-H "Content-Type: application/json" \
	-d '{"imagePath":"/absolute/path/to/frame.png","question":"What is on screen?"}'
```
