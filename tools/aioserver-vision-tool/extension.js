const vscode = require("vscode");
const fs = require("fs");
const net = require("net");
const os = require("os");
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
  if (haystack.includes("claude") && haystack.includes("sonnet")) {
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
  const raw = process.env.AIOSERVER_VISION_MODEL_IDS || "claude-sonnet-4.6";
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
  if (!raw) {
    return true;
  }
  const normalized = raw.toLowerCase();
  if (["0", "false", "no", "off"].includes(normalized)) {
    return false;
  }
  return ["1", "true", "yes", "on"].includes(normalized);
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

async function analyzeImages(input, token) {
  const paths = input.imagePaths || input.image_paths || [];
  const goal = input.goal || input.question || "";

  if (!paths.length || !goal) {
    throw new Error("analyzeImages requires image_paths (1–10) and goal.");
  }
  if (paths.length > 10) {
    throw new Error("Maximum 10 images per analysis call.");
  }

  const model = await selectVisionModel();

  // Build message parts: text prompt first, then all images in order
  const parts = [];

  const promptLines = [
    "You are analyzing screenshots captured from AIOServer during visual development and testing.",
    "Use the images themselves as the primary evidence. Answer directly and concisely.",
    `Number of images: ${paths.length}`,
    "",
    `Goal: ${goal}`,
  ];

  if (input.context) {
    promptLines.push("", `Additional context: ${input.context}`);
  }

  if (input.styleContext || input.style_context) {
    const qss = input.styleContext || input.style_context;
    promptLines.push(
      "",
      "## Active QSS/CSS Stylesheet",
      "The following stylesheet is applied to the page being viewed.",
      "When suggesting fixes, reference specific selectors and properties from this stylesheet.",
      "",
      "```css",
      qss,
      "```",
    );
  }

  // Load all images and their sidecars
  for (let i = 0; i < paths.length; i++) {
    const imageUri = vscode.Uri.file(paths[i]);
    const sidecar = await readOptionalSidecar(imageUri);
    if (sidecar) {
      promptLines.push(
        "",
        `Image ${i + 1} (${path.basename(paths[i])}) metadata:`,
        sidecar,
      );
    }
  }

  parts.push(new vscode.LanguageModelTextPart(promptLines.join("\n")));

  for (const imagePath of paths) {
    const imageUri = vscode.Uri.file(imagePath);
    const imageBytes = await vscode.workspace.fs.readFile(imageUri);
    const mime = mimeTypeForFile(imagePath);
    parts.push(vscode.LanguageModelDataPart.image(imageBytes, mime));
  }

  const messages = [vscode.LanguageModelChatMessage.User(parts)];

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
    imageCount: paths.length,
    response: text.trim(),
  };
}

async function handleVisionRequest(method, args, token) {
  if (method === "analyze_images") {
    return analyzeImages(
      {
        imagePaths: args.imagePaths || args.image_paths,
        goal: args.goal || args.question,
        context: args.context,
        styleContext: args.styleContext || args.style_context,
      },
      token,
    );
  }

  // Legacy compat: map old tool names to the unified function
  if (method === "inspect_screenshot") {
    const imagePath = args.imagePath || args.image_path;
    return analyzeImages(
      {
        imagePaths: imagePath ? [imagePath] : [],
        goal: args.question,
        context: args.context,
      },
      token,
    );
  }

  if (method === "compare_screenshots") {
    const paths = [];
    const ref = args.referenceImagePath || args.reference_image_path;
    const test = args.testImagePath || args.test_image_path;
    if (ref) paths.push(ref);
    if (test) paths.push(test);
    return analyzeImages(
      {
        imagePaths: paths,
        goal: args.question,
        context: args.context,
      },
      token,
    );
  }

  throw new Error(`Unsupported IPC method: ${method}`);
}

function makeToolResult(value) {
  return new vscode.LanguageModelToolResult([
    new vscode.LanguageModelTextPart(value),
  ]);
}

function parseAnalyzePrompt(prompt) {
  const trimmed = (prompt || "").trim();
  if (!trimmed) {
    return undefined;
  }

  // Format: /analyze path1 :: path2 :: ... :: goal
  // At minimum: one path :: goal
  const parts = trimmed.split("::").map((part) => part.trim());
  if (parts.length < 2) {
    return undefined;
  }

  const goal = parts[parts.length - 1];
  const imagePaths = parts
    .slice(0, -1)
    .map((p) => p.replace(/^['"]|['"]$/g, ""))
    .filter(Boolean);

  if (!imagePaths.length || !goal) {
    return undefined;
  }

  return { imagePaths, goal };
}

function usageMarkdown() {
  return [
    "Use `@aioserver-vision /analyze` with this format:",
    "",
    "`/analyze /path/to/image.png :: What should be evaluated?`",
    "",
    "Multiple images (up to 10):",
    "",
    "`/analyze /path/to/img1.png :: /path/to/img2.png :: /path/to/img3.png :: Compare these and identify differences`",
    "",
    "Example:",
    "",
    "`/analyze /Users/alexwaldmann/Desktop/AIO Server/tmp/before.png :: /Users/alexwaldmann/Desktop/AIO Server/tmp/after.png :: How has the homescreen design changed between these two versions?`",
  ].join("\n");
}

function registerChatParticipant(context) {
  const participant = vscode.chat.createChatParticipant(
    "local.aioserver-vision",
    async (request, _chatContext, stream, token) => {
      try {
        if (request.command === "analyze") {
          const parsed = parseAnalyzePrompt(request.prompt);
          if (!parsed) {
            stream.markdown(usageMarkdown());
            return;
          }

          const names = parsed.imagePaths.map((p) => path.basename(p));
          stream.progress(`Analyzing ${names.join(", ")}`);
          const result = await analyzeImages(
            { imagePaths: parsed.imagePaths, goal: parsed.goal },
            token,
          );
          stream.markdown(
            `Model: ${result.model} | Images: ${result.imageCount}\n\n${result.response}`,
          );
          return;
        }

        stream.markdown(usageMarkdown());
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        stream.markdown(`Image analysis failed: ${message}`);
      }
    },
  );

  context.subscriptions.push(participant);
}

function registerTool(context) {
  const toolNames = ["aioserver-analyze-images", "aioserver_analyze_images"];

  const analyzeTool = {
    async invoke(options, token) {
      const result = await analyzeImages(options.input, token);
      return makeToolResult(
        `Model: ${result.model} | Images: ${result.imageCount}\n\n${result.response}`,
      );
    },
    async prepareInvocation(options) {
      const paths = options.input.imagePaths || options.input.image_paths || [];
      const names = paths.map((p) => path.basename(p));
      return {
        invocationMessage: `Analyzing ${names.join(", ") || "images"}`,
      };
    },
  };

  for (const toolName of toolNames) {
    context.subscriptions.push(vscode.lm.registerTool(toolName, analyzeTool));
  }
}

function registerCommand(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand(
      "aioserver.inspectScreenshotManual",
      async () => {
        const imageUris = await vscode.window.showOpenDialog({
          canSelectMany: true,
          openLabel: "Analyze Images",
          filters: {
            Images: ["png", "jpg", "jpeg", "webp", "bmp", "gif"],
          },
        });
        if (!imageUris || !imageUris.length) {
          return;
        }

        const goal = await vscode.window.showInputBox({
          prompt: "What should Copilot determine from these images?",
          placeHolder:
            "Example: Evaluate the visual quality and identify any UI issues.",
        });
        if (!goal) {
          return;
        }

        try {
          const imagePaths = imageUris.map((uri) => uri.fsPath);
          const result = await analyzeImages(
            { imagePaths, goal },
            new vscode.CancellationTokenSource().token,
          );
          const pathList = imagePaths.map((p) => `- ${p}`).join("\n");
          const document = await vscode.workspace.openTextDocument({
            language: "markdown",
            content: `# Image Analysis\n\nImages (${result.imageCount}):\n${pathList}\n\nModel: ${result.model}\n\n${result.response}\n`,
          });
          await vscode.window.showTextDocument(document, { preview: false });
        } catch (error) {
          const message =
            error instanceof Error ? error.message : String(error);
          void vscode.window.showErrorMessage(
            `AIOServer image analysis failed: ${message}`,
          );
        }
      },
    ),
  );
}

function getWorkspaceRoot() {
  return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

function getIpcState(workspaceRoot) {
  const workspaceName =
    path
      .basename(workspaceRoot)
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-+|-+$/g, "") || "workspace";

  const globalCacheDir = path.join(os.homedir(), ".cache", "gsh");

  return {
    socketPath: path.join(
      os.tmpdir(),
      `aioserver-vision-${workspaceName}.sock`,
    ),
    infoPath: path.join(globalCacheDir, "aioserver-vision-ipc.json"),
  };
}

function safeUnlink(socketPath) {
  try {
    if (fs.existsSync(socketPath)) {
      fs.unlinkSync(socketPath);
    }
  } catch {
    // Ignore cleanup failures.
  }
}

function writeIpcInfo(infoPath, socketPath) {
  fs.mkdirSync(path.dirname(infoPath), { recursive: true });
  fs.writeFileSync(
    infoPath,
    JSON.stringify(
      {
        socketPath,
        updatedAt: new Date().toISOString(),
      },
      null,
      2,
    ),
    "utf8",
  );
}

function startIpcServer(context) {
  const workspaceRoot = getWorkspaceRoot();
  if (!workspaceRoot) {
    return;
  }

  const { socketPath, infoPath } = getIpcState(workspaceRoot);
  safeUnlink(socketPath);
  writeIpcInfo(infoPath, socketPath);

  const server = net.createServer((socket) => {
    let buffer = "";
    socket.setEncoding("utf8");

    socket.on("data", async (chunk) => {
      buffer += chunk;
      const lines = buffer.split("\n");
      buffer = lines.pop() || "";

      for (const line of lines) {
        if (!line.trim()) {
          continue;
        }

        let request;
        try {
          request = JSON.parse(line);
        } catch {
          socket.write(
            JSON.stringify({ ok: false, error: "Invalid JSON payload" }) + "\n",
          );
          continue;
        }

        const tokenSource = new vscode.CancellationTokenSource();
        try {
          const result = await handleVisionRequest(
            request.method,
            request.arguments || {},
            tokenSource.token,
          );
          socket.write(
            JSON.stringify({
              ok: true,
              model: result.model,
              result: result.response,
            }) + "\n",
          );
        } catch (error) {
          const message =
            error instanceof Error ? error.message : String(error);
          socket.write(JSON.stringify({ ok: false, error: message }) + "\n");
        } finally {
          tokenSource.dispose();
        }
      }
    });
  });

  server.listen(socketPath);

  context.subscriptions.push({
    dispose() {
      try {
        server.close();
      } catch {
        // Ignore close failures during shutdown.
      }
      safeUnlink(socketPath);
      try {
        fs.unlinkSync(infoPath);
      } catch {
        // Ignore cleanup failures.
      }
    },
  });
}

function activate(context) {
  startIpcServer(context);
  registerTool(context);
  registerCommand(context);
  registerChatParticipant(context);
}

function deactivate() {}

module.exports = {
  activate,
  deactivate,
};
