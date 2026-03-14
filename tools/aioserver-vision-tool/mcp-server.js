#!/usr/bin/env node

const fs = require("fs");
const net = require("net");
const path = require("path");
const readline = require("readline");

const MCP_VERSION = "2024-11-05";
const IPC_INFO_PATH =
  process.env.AIOSERVER_VISION_IPC_INFO_PATH ||
  path.join(process.cwd(), ".vscode", "aioserver-vision-ipc.json");

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

function loadIpcInfo() {
  const raw = fs.readFileSync(IPC_INFO_PATH, "utf8");
  const parsed = JSON.parse(raw);
  if (!parsed.socketPath) {
    throw new Error("IPC metadata missing socketPath");
  }
  return parsed;
}

async function connectAndSend(request) {
  let lastError;
  for (let attempt = 0; attempt < 10; attempt += 1) {
    try {
      const ipcInfo = loadIpcInfo();
      return await new Promise((resolve, reject) => {
        const socket = net.createConnection(ipcInfo.socketPath, () => {
          socket.write(`${JSON.stringify(request)}\n`);
        });

        let buffer = "";

        socket.setEncoding("utf8");
        socket.on("data", (chunk) => {
          buffer += chunk;
          const lines = buffer.split("\n");
          buffer = lines.pop() || "";

          for (const line of lines) {
            if (!line.trim()) {
              continue;
            }

            try {
              const response = JSON.parse(line);
              socket.destroy();
              resolve(response);
              return;
            } catch {
              // Ignore malformed partial lines and wait for the next chunk.
            }
          }
        });

        socket.on("error", reject);
        socket.setTimeout(120000, () => {
          socket.destroy(new Error("Extension IPC timeout (120s)"));
        });
      });
    } catch (error) {
      lastError = error;
      await new Promise((resolve) => setTimeout(resolve, 200));
    }
  }

  throw lastError || new Error("Extension IPC not configured");
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
        serverInfo: { name: "aioserver-vision", version: "1.0.0" },
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
            name: "analyze_images",
            description:
              "Analyze up to 10 images with a vision model. Supports single inspection, comparisons, batch evaluation, and any custom analysis goal.",
            inputSchema: {
              type: "object",
              properties: {
                image_paths: {
                  type: "array",
                  items: { type: "string" },
                  minItems: 1,
                  maxItems: 10,
                  description:
                    "Absolute paths to image files (1–10). Order matters: reference images first when comparing.",
                },
                goal: {
                  type: "string",
                  description:
                    "What to determine from the images. Be specific: inspect quality, compare differences, evaluate design, verify a fix, etc.",
                },
                context: {
                  type: "string",
                  description: "Optional extra context about the images.",
                },
                style_context: {
                  type: "string",
                  description:
                    "Optional QSS/CSS stylesheet content for the page being viewed. Helps the model suggest concrete selector-level fixes.",
                },
              },
              required: ["image_paths", "goal"],
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

    if (toolName !== "analyze_images") {
      sendError(id, -32601, `Unknown tool: ${toolName}`);
      return;
    }

    try {
      const response = await connectAndSend({
        method: toolName,
        arguments: toolArguments,
      });

      if (!response.ok) {
        sendError(id, -32603, response.error || "Extension IPC failed");
        return;
      }

      send({
        jsonrpc: "2.0",
        id,
        result: {
          content: [
            {
              type: "text",
              text: `Model: ${response.model}\n\n${response.result}`,
            },
          ],
        },
      });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      sendError(id, -32603, message);
    }
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
