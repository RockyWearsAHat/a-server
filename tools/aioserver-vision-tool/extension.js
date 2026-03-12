const vscode = require("vscode");
const http = require("http");
const path = require("path");

function mimeTypeForFile(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  if (ext === ".png") {
    return "image/png";
  }
  if (ext === ".jpg" || ext === ".jpeg") {
    return "image/jpeg";
  }
  if (ext === ".webp") {
    return "image/webp";
  }
  if (ext === ".bmp") {
    return "image/bmp";
  }
  if (ext === ".gif") {
    return "image/gif";
  }
  throw new Error(`Unsupported image extension: ${ext || "(none)"}`);
}

function scoreModel(model) {
  const haystack =
    `${model.vendor || ""} ${model.family || ""} ${model.id || ""} ${model.name || ""}`.toLowerCase();
  let score = 0;
  if (model.capabilities?.imageInput) {
    score += 1000;
  }
  if (haystack.includes("claude") && haystack.includes("haiku")) {
    score += 200;
  }
  if (haystack.includes("gpt-4o")) {
    score += 150;
  }
  if (haystack.includes("mini")) {
    score -= 10;
  }
  return score;
}

function normalizedModelFields(model) {
  return {
    id: (model.id || "").toLowerCase(),
    name: (model.name || "").toLowerCase(),
    family: (model.family || "").toLowerCase(),
  };
}

function parsePreferredModelIds() {
  const raw =
    process.env.AIOSERVER_VISION_MODEL_IDS || "claude-haiku-4.5,claude-haiku-4";
  return raw
    .split(",")
    .map((item) => item.trim().toLowerCase())
    .filter(Boolean);
}

function modelMatchesPreference(model, preferredIds) {
  const fields = normalizedModelFields(model);
  return preferredIds.some((pref) => {
    return (
      fields.id.includes(pref) ||
      fields.name.includes(pref) ||
      fields.family.includes(pref)
    );
  });
}

function allowsUndeclaredImageModel() {
  const raw = process.env.AIOSERVER_VISION_ALLOW_UNDECLARED_IMAGE_MODEL || "";
  return ["1", "true", "yes", "on"].includes(raw.toLowerCase());
}

async function selectVisionModel() {
  let models = await vscode.lm.selectChatModels({ vendor: "copilot" });
  if (!models.length) {
    models = await vscode.lm.selectChatModels({});
  }

  const preferredIds = parsePreferredModelIds();
  const visionModels = models.filter((model) => model.capabilities?.imageInput);
  const preferredVisionModels = visionModels.filter((model) =>
    modelMatchesPreference(model, preferredIds),
  );

  if (preferredVisionModels.length) {
    preferredVisionModels.sort(
      (left, right) => scoreModel(right) - scoreModel(left),
    );
    return preferredVisionModels[0];
  }

  if (visionModels.length) {
    visionModels.sort((left, right) => scoreModel(right) - scoreModel(left));
    return visionModels[0];
  }

  if (allowsUndeclaredImageModel()) {
    const preferredModels = models.filter((model) =>
      modelMatchesPreference(model, preferredIds),
    );
    if (preferredModels.length) {
      preferredModels.sort(
        (left, right) => scoreModel(right) - scoreModel(left),
      );
      return preferredModels[0];
    }
  }

  const candidates = models
    .map((model) => ({
      id: model.id || "unknown-id",
      name: model.name || "unknown-name",
      vendor: model.vendor || "unknown-vendor",
      imageInput: Boolean(model.capabilities?.imageInput),
    }))
    .slice(0, 20);

  const guidance = allowsUndeclaredImageModel()
    ? "No preferred fallback model was found."
    : "Set AIOSERVER_VISION_ALLOW_UNDECLARED_IMAGE_MODEL=1 to try a preferred model anyway.";

  throw new Error(
    `No chat model with image input capability is currently available. ${guidance} Candidates: ${JSON.stringify(candidates)}`,
  );
}

async function readOptionalSidecar(imageUri) {
  const sidecarUri = imageUri.with({ path: `${imageUri.path}.json` });
  try {
    const raw = await vscode.workspace.fs.readFile(sidecarUri);
    return new TextDecoder().decode(raw);
  } catch {
    return undefined;
  }
}

async function inspectScreenshot(input, token) {
  if (!input?.imagePath || !input?.question) {
    throw new Error("inspectScreenshot requires imagePath and question.");
  }

  const imageUri = vscode.Uri.file(input.imagePath);
  const imageBytes = await vscode.workspace.fs.readFile(imageUri);
  const mime = mimeTypeForFile(input.imagePath);
  const sidecar = await readOptionalSidecar(imageUri);
  const model = await selectVisionModel();

  const promptLines = [
    "You are analyzing a screenshot captured from AIOServer during automated visual testing.",
    "Use the image itself as the primary evidence.",
    "If sidecar metadata is present, use it only as supporting context.",
    "Answer the question directly and concisely. If the screenshot does not contain enough evidence, say so plainly.",
    "",
    `Question: ${input.question}`,
  ];

  if (input.context) {
    promptLines.push("", `Additional context: ${input.context}`);
  }

  if (sidecar) {
    promptLines.push("", "Supporting screenshot metadata:", sidecar);
  }

  const messages = [
    vscode.LanguageModelChatMessage.User([
      new vscode.LanguageModelTextPart(promptLines.join("\n")),
      vscode.LanguageModelDataPart.image(imageBytes, mime),
    ]),
  ];

  const response = await model.sendRequest(messages, {}, token);
  let text = "";
  for await (const chunk of response.text) {
    text += chunk;
  }

  if (!text.trim()) {
    throw new Error("Vision model returned an empty response.");
  }

  return {
    model: model.name || model.id || model.family || "unknown",
    response: text.trim(),
  };
}

function makeToolResult(value) {
  return new vscode.LanguageModelToolResult([
    new vscode.LanguageModelTextPart(value),
  ]);
}

function parseInspectionPrompt(prompt) {
  const trimmed = (prompt || "").trim();
  if (!trimmed) {
    return undefined;
  }

  const separatorIndex = trimmed.indexOf("::");
  if (separatorIndex === -1) {
    return undefined;
  }

  const imagePath = trimmed
    .slice(0, separatorIndex)
    .trim()
    .replace(/^['"]|['"]$/g, "");
  const question = trimmed.slice(separatorIndex + 2).trim();
  if (!imagePath || !question) {
    return undefined;
  }

  return { imagePath, question };
}

function usageMarkdown() {
  return [
    "Use `@aioserver-vision /inspect` with this format:",
    "",
    "`/inspect /absolute/path/to/screenshot.png :: What should be verified?`",
    "",
    "Example:",
    "",
    "`/inspect /Users/alexwaldmann/Desktop/AIO Server/tmp/final.png :: Is the YouTube landing screen visible and are there obvious UI problems?`",
  ].join("\n");
}

function registerChatParticipant(context) {
  const participant = vscode.chat.createChatParticipant(
    "local.aioserver-vision",
    async (request, _chatContext, stream, token) => {
      const parsed = parseInspectionPrompt(request.prompt);
      if (request.command !== "inspect" || !parsed) {
        stream.markdown(usageMarkdown());
        return;
      }

      try {
        stream.progress(`Inspecting ${path.basename(parsed.imagePath)}`);
        const result = await inspectScreenshot(parsed, token);
        stream.markdown(`Model: ${result.model}\n\n${result.response}`);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        stream.markdown(`Screenshot inspection failed: ${message}`);
      }
    },
  );

  context.subscriptions.push(participant);
}

function registerTool(context) {
  const toolNames = [
    "aioserver-inspect-screenshot",
    "aioserver_inspect_screenshot",
  ];

  const tool = {
    async invoke(options, token) {
      const result = await inspectScreenshot(options.input, token);
      return makeToolResult(`Model: ${result.model}\n\n${result.response}`);
    },
    async prepareInvocation(options) {
      return {
        invocationMessage: `Inspecting screenshot ${path.basename(options.input.imagePath || "image")}`,
      };
    },
  };

  for (const toolName of toolNames) {
    context.subscriptions.push(vscode.lm.registerTool(toolName, tool));
  }
}

function registerCommand(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand(
      "aioserver.inspectScreenshotManual",
      async () => {
        const imageUris = await vscode.window.showOpenDialog({
          canSelectMany: false,
          openLabel: "Inspect Screenshot",
          filters: {
            Images: ["png", "jpg", "jpeg", "webp", "bmp", "gif"],
          },
        });
        if (!imageUris || !imageUris.length) {
          return;
        }

        const question = await vscode.window.showInputBox({
          prompt: "What should Copilot determine from this screenshot?",
          placeHolder:
            "Example: Is the YouTube landing screen visible and are there obvious UI issues?",
        });
        if (!question) {
          return;
        }

        try {
          const result = await inspectScreenshot(
            { imagePath: imageUris[0].fsPath, question },
            new vscode.CancellationTokenSource().token,
          );
          const document = await vscode.workspace.openTextDocument({
            language: "markdown",
            content: `# Screenshot Analysis\n\n- Image: ${imageUris[0].fsPath}\n- Model: ${result.model}\n\n${result.response}\n`,
          });
          await vscode.window.showTextDocument(document, { preview: false });
        } catch (error) {
          const message =
            error instanceof Error ? error.message : String(error);
          void vscode.window.showErrorMessage(
            `AIOServer screenshot inspection failed: ${message}`,
          );
        }
      },
    ),
  );
}

function parseBridgePort() {
  const fallbackPort = 39377;
  const raw = process.env.AIOSERVER_VISION_BRIDGE_PORT;
  if (!raw) {
    return fallbackPort;
  }
  const parsed = Number.parseInt(raw, 10);
  if (!Number.isInteger(parsed) || parsed < 1 || parsed > 65535) {
    return fallbackPort;
  }
  return parsed;
}

function writeJson(res, statusCode, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
    "Cache-Control": "no-store",
  });
  res.end(body);
}

function startBridgeServer(context) {
  const host = "127.0.0.1";
  const port = parseBridgePort();
  const output = vscode.window.createOutputChannel("AIOServer Vision Tool");
  context.subscriptions.push(output);

  const server = http.createServer((req, res) => {
    const urlPath = req.url || "/";

    if (req.method === "GET" && urlPath === "/health") {
      writeJson(res, 200, {
        ok: true,
        service: "aioserver-vision-bridge",
        port,
      });
      return;
    }

    if (req.method !== "POST" || urlPath !== "/inspect") {
      writeJson(res, 404, { ok: false, error: "Not found" });
      return;
    }

    let body = "";
    req.setEncoding("utf8");

    req.on("data", (chunk) => {
      body += chunk;
      if (body.length > 1024 * 1024) {
        writeJson(res, 413, { ok: false, error: "Request body too large" });
        req.destroy();
      }
    });

    req.on("end", async () => {
      let input;
      try {
        input = JSON.parse(body || "{}");
      } catch {
        writeJson(res, 400, { ok: false, error: "Invalid JSON payload" });
        return;
      }

      const tokenSource = new vscode.CancellationTokenSource();
      try {
        const result = await inspectScreenshot(input, tokenSource.token);
        writeJson(res, 200, {
          ok: true,
          model: result.model,
          response: result.response,
        });
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        writeJson(res, 500, { ok: false, error: message });
      } finally {
        tokenSource.dispose();
      }
    });
  });

  server.on("error", (error) => {
    const message = error instanceof Error ? error.message : String(error);
    output.appendLine(
      `[bridge] failed to listen on ${host}:${port} (${message})`,
    );
  });

  server.listen(port, host, () => {
    output.appendLine(`[bridge] listening on http://${host}:${port}`);
  });

  context.subscriptions.push({
    dispose() {
      try {
        server.close();
      } catch {
        // Ignore close failures during shutdown.
      }
    },
  });
}

function activate(context) {
  registerTool(context);
  registerCommand(context);
  registerChatParticipant(context);
  startBridgeServer(context);
}

function deactivate() {}

module.exports = {
  activate,
  deactivate,
};
