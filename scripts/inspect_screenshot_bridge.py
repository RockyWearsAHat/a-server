#!/usr/bin/env python3
"""Call the local AIOServer vision bridge exposed by the VS Code extension."""

import argparse
import json
import sys
import urllib.error
import urllib.request


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Inspect a screenshot through the local AIOServer vision bridge",
    )
    parser.add_argument("--image", required=True, help="Absolute screenshot path")
    parser.add_argument("--question", required=True, help="Question to answer")
    parser.add_argument("--context", default="", help="Optional additional context")
    parser.add_argument("--port", type=int, default=39377, help="Bridge port")
    parser.add_argument(
        "--timeout",
        type=float,
        default=90.0,
        help="HTTP timeout in seconds",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    payload = {
        "imagePath": args.image,
        "question": args.question,
    }
    if args.context:
        payload["context"] = args.context

    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url=f"http://127.0.0.1:{args.port}/inspect",
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=args.timeout) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        text = error.read().decode("utf-8", errors="replace")
        print(text)
        return 1
    except urllib.error.URLError as error:
        print(json.dumps({"ok": False, "error": str(error.reason)}))
        return 2

    print(body)

    try:
        parsed = json.loads(body)
    except json.JSONDecodeError:
        return 3

    return 0 if parsed.get("ok") else 4


if __name__ == "__main__":
    sys.exit(main())
