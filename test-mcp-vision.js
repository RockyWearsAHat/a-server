#!/usr/bin/env node

/**
 * Test script for MCP Vision extension.
 * Supports both single image analysis and comparison between reference and test images.
 *
 * Usage:
 *   node test-mcp-vision.js                          # Compare default images
 *   node test-mcp-vision.js <test_image>             # Analyze single image
 *   node test-mcp-vision.js <ref> <test>             # Compare reference vs test
 */

const net = require("net");
const fs = require("fs");
const path = require("path");
const os = require("os");

const DEFAULT_TEST_IMAGE =
  "/Users/alexwaldmann/Desktop/AIO Server/test_output/visual_verification/crash_bandicoot/20260312_latest_local/frame_30000ms.png";
const DEFAULT_REF_IMAGE =
  "/Users/alexwaldmann/Downloads/6aedf8972fcb062a-600x338.jpg";

// Parse command line arguments
function parseArgs() {
  const args = process.argv.slice(2);

  if (args.length === 0) {
    // Use defaults - check if reference exists
    if (fs.existsSync(DEFAULT_REF_IMAGE)) {
      return {
        mode: "compare",
        reference: DEFAULT_REF_IMAGE,
        test: DEFAULT_TEST_IMAGE,
      };
    } else {
      return { mode: "analyze", image: DEFAULT_TEST_IMAGE };
    }
  } else if (args.length === 1) {
    return { mode: "analyze", image: args[0] };
  } else if (args.length === 2) {
    // Two arguments: could be (image, question) or (reference, test)
    // Check if second arg is a file path (exists) or looks like a question
    if (fs.existsSync(args[1])) {
      // Both are files - compare mode
      return { mode: "compare", reference: args[0], test: args[1] };
    } else {
      // Second arg is a question - analyze mode with custom question
      return { mode: "analyze", image: args[0], question: args[1] };
    }
  } else {
    // Three+ arguments: (reference, test, question)
    return {
      mode: "compare",
      reference: args[0],
      test: args[1],
      question: args[2],
    };
  }
}

// Find the socket file (it has a dynamic name)
function findSocketPath() {
  const tmpDir = os.tmpdir();
  const files = fs.readdirSync(tmpDir);
  const socketFiles = files
    .filter((f) => f.startsWith("aioserver-vision-"))
    .map((fileName) => {
      const fullPath = path.join(tmpDir, fileName);
      return {
        fileName,
        fullPath,
        mtimeMs: fs.statSync(fullPath).mtimeMs,
      };
    })
    .sort((left, right) => right.mtimeMs - left.mtimeMs);

  if (socketFiles.length === 0) {
    console.error("❌ IPC socket not found. Extension not activated?");
    console.error("   Checked:", tmpDir);
    console.error("   Files with 'aioserver-vision-' prefix not found");
    return null;
  }

  return socketFiles[0].fullPath;
}

async function sendRequest(method, args) {
  return new Promise((resolve, reject) => {
    const socketPath = findSocketPath();

    if (!socketPath) {
      reject(new Error("IPC socket not found"));
      return;
    }

    const socket = net.createConnection(socketPath, () => {
      const request = {
        method,
        arguments: args,
      };

      socket.write(JSON.stringify(request) + "\n");
    });

    let buffer = "";
    let response = null;

    socket.on("data", (chunk) => {
      buffer += chunk.toString();
      const lines = buffer.split("\n");
      buffer = lines.pop() || "";

      for (const line of lines) {
        if (!line.trim()) continue;

        try {
          response = JSON.parse(line);
          socket.destroy();
          resolve(response);
        } catch (err) {
          // Parse error, continue
        }
      }
    });

    socket.on("error", reject);

    // Timeout after 120 seconds (image analysis can take time)
    setTimeout(() => {
      if (!socket.destroyed) {
        socket.destroy();
        reject(new Error("Request timeout"));
      }
    }, 120000);
  });
}

async function testAnalyze(imagePath, question) {
  console.log("🧪 Testing MCP Vision Extension - Analyze Mode");
  console.log("==============================================");
  console.log("");

  if (!fs.existsSync(imagePath)) {
    console.error(`❌ Image not found: ${imagePath}`);
    process.exit(1);
  }

  console.log(`📸 Image: ${imagePath}`);
  console.log(`   Size: ${fs.statSync(imagePath).size} bytes`);
  if (question) {
    console.log(`   Question: ${question}`);
  }
  console.log("");

  console.log("📤 Sending analysis request...");

  try {
    const response = await sendRequest("inspect_screenshot", {
      image_path: imagePath,
      question:
        question ||
        "Describe this screenshot in detail. What do you see? Are there any visual issues or corruption?",
    });

    console.log("📥 Response received:");
    console.log("");

    if (response.error) {
      console.error(`❌ Error: ${response.error}`);
      process.exit(1);
    } else {
      const text = response.result || response;
      console.log("✓ Analysis:");
      console.log("---");
      console.log(text);
      console.log("---");
    }
  } catch (err) {
    console.error(`❌ ${err.message}`);
    process.exit(1);
  }
}

async function testCompare(refImagePath, testImagePath, question) {
  console.log("🧪 Testing MCP Vision Extension - Compare Mode");
  console.log("==============================================");
  console.log("");

  if (!fs.existsSync(refImagePath)) {
    console.error(`❌ Reference image not found: ${refImagePath}`);
    process.exit(1);
  }

  if (!fs.existsSync(testImagePath)) {
    console.error(`❌ Test image not found: ${testImagePath}`);
    process.exit(1);
  }

  console.log(`📸 Reference: ${refImagePath}`);
  console.log(`   Size: ${fs.statSync(refImagePath).size} bytes`);
  console.log("");
  console.log(`📸 Test Image: ${testImagePath}`);
  console.log(`   Size: ${fs.statSync(testImagePath).size} bytes`);
  if (question) {
    console.log(`   Question: ${question}`);
  }
  console.log("");

  console.log("📤 Sending comparison request...");

  try {
    const response = await sendRequest("compare_screenshots", {
      reference_image_path: refImagePath,
      test_image_path: testImagePath,
      question:
        question ||
        "Compare these two screenshots. The first is a reference from original hardware. The second is from an emulator. What are the key differences? Are there rendering issues, missing geometry, corruption, or UI problems in the emulator version?",
    });

    console.log("📥 Response received:");
    console.log("");

    if (response.error) {
      console.error(`❌ Error: ${response.error}`);
      process.exit(1);
    } else {
      const text = response.result || response;
      console.log("✓ Comparison:");
      console.log("---");
      console.log(text);
      console.log("---");
    }
  } catch (err) {
    console.error(`❌ ${err.message}`);
    process.exit(1);
  }
}

async function main() {
  const args = parseArgs();

  console.log("");

  try {
    if (args.mode === "analyze") {
      await testAnalyze(args.image, args.question);
    } else {
      await testCompare(args.reference, args.test, args.question);
    }

    console.log("");
    console.log("✅ Test complete");
  } catch (err) {
    console.error("❌ Test failed:", err.message);
    process.exit(1);
  }
}

main();
