#!/usr/bin/env python3
"""
Visual Development Loop — interactive automation engine for AIO Server.

Provides CLI subcommands that an agent can call in sequence to:
  boot the app, focus the window, take screenshots, send input,
  perform basic image analysis (OCR, color stats, non-black checks),
  and compare screenshots.

Usage:
    visual_dev_loop.py boot   [--rom ROM | --app APP] [--wait-ms MS] [--input-script PATH]
    visual_dev_loop.py focus
    visual_dev_loop.py screenshot [--output PATH] [--window]
    visual_dev_loop.py snapshot [--rom ROM | --app APP] [--wait-ms MS] [--input-script PATH] [--output PATH] [--full-screen] [--window-only]
    visual_dev_loop.py click   --x X --y Y [--window-relative]
    visual_dev_loop.py click-percent --percent-x X --percent-y Y [--image PATH]
    visual_dev_loop.py key-sequence --keys "Down Down Enter"
    visual_dev_loop.py key     --name KEYNAME [--modifiers MOD]
    visual_dev_loop.py type    --text TEXT
    visual_dev_loop.py wait    --ms MS
    visual_dev_loop.py analyze --image PATH [--ocr] [--colors] [--nonblack] [--region X,Y,W,H]
    visual_dev_loop.py status
    visual_dev_loop.py kill
    visual_dev_loop.py session --plan PLAN_JSON

All commands print structured JSON to stdout for easy agent consumption.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Optional, cast


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def workspace_root() -> Path:
    return Path(__file__).resolve().parent.parent


def aioserver_path() -> Path:
    return workspace_root() / "build" / "bin" / "AIOServer"


def _internal_dir() -> Path:
    """Internal directory for logs, PID file, OCR temp files."""
    d = workspace_root() / "test_output" / "visual_loop"
    d.mkdir(parents=True, exist_ok=True)
    return d


# Keep old name for non-screenshot internals.
output_dir = _internal_dir


def screenshot_dir() -> Path:
    """Screenshots go in the workspace root for easy #file: reference."""
    return workspace_root()


def _probe_capture_path() -> Path:
    return output_dir() / "_probe_capture.png"


def _metadata_dir() -> Path:
    metadata_dir = output_dir() / "metadata"
    metadata_dir.mkdir(parents=True, exist_ok=True)
    return metadata_dir


_MAX_SCREENSHOTS = 1
_SCREENSHOT_PREFIX = "visual_"
_LEGACY_SCREENSHOT_PREFIXES = ("visual_", "screenshot_")
_OCR_WORD_CACHE: dict[tuple[str, int, int, int], list[dict[str, Any]]] = {}


def _screenshot_metadata_path(image_path: Path) -> Path:
    resolved = image_path.expanduser().resolve()
    digest = hashlib.sha1(str(resolved).encode("utf-8")).hexdigest()[:10]
    return _metadata_dir() / f"{resolved.stem}_{digest}.json"


def _legacy_workspace_metadata_path(image_path: Path) -> Path:
    return image_path.with_suffix(image_path.suffix + ".json")


def _sanitize_artifact_label(label: Optional[str]) -> str:
    normalized = (label or "state").strip().lower()
    normalized = re.sub(r"[^a-z0-9]+", "-", normalized)
    normalized = normalized.strip("-")
    return normalized or "state"


def _default_screenshot_path(label: Optional[str]) -> Path:
    safe_label = _sanitize_artifact_label(label)
    return screenshot_dir() / f"{_SCREENSHOT_PREFIX}{safe_label}_{ts()}.png"


def _delete_screenshot_artifact(image_path: Path) -> list[str]:
    deleted: list[str] = []
    metadata_candidates = {
        _screenshot_metadata_path(image_path),
        _legacy_workspace_metadata_path(image_path),
    }
    for candidate in (image_path, *metadata_candidates):
        if candidate.exists():
            candidate.unlink(missing_ok=True)
            deleted.append(str(candidate))
    return deleted


def _managed_workspace_screenshots() -> list[Path]:
    screenshots: list[Path] = []
    for prefix in _LEGACY_SCREENSHOT_PREFIXES:
        screenshots.extend(screenshot_dir().glob(f"{prefix}*.png"))
    return sorted(set(path.resolve() for path in screenshots), key=lambda path: path.stat().st_mtime)


def _cleanup_orphaned_metadata() -> list[str]:
    deleted: list[str] = []
    for prefix in _LEGACY_SCREENSHOT_PREFIXES:
        for metadata_path in screenshot_dir().glob(f"{prefix}*.png.json"):
            metadata_path.unlink(missing_ok=True)
            deleted.append(str(metadata_path))

    for metadata_path in _metadata_dir().glob("*.json"):
        try:
            payload = json.loads(metadata_path.read_text())
        except Exception:
            metadata_path.unlink(missing_ok=True)
            deleted.append(str(metadata_path))
            continue

        image_value = payload.get("path")
        if not image_value:
            metadata_path.unlink(missing_ok=True)
            deleted.append(str(metadata_path))
            continue

        image_path = Path(str(image_value)).expanduser()
        if image_path.exists():
            continue
        metadata_path.unlink(missing_ok=True)
        deleted.append(str(metadata_path))
    return deleted


def _write_screenshot_metadata(image_path: Path, metadata: dict[str, Any]) -> None:
    metadata_path = _screenshot_metadata_path(image_path)
    metadata_path.write_text(json.dumps(metadata, indent=2))


def _read_screenshot_metadata(image_path: Path) -> Optional[dict[str, Any]]:
    metadata_path = _screenshot_metadata_path(image_path)
    if not metadata_path.exists():
        return None
    try:
        return json.loads(metadata_path.read_text())
    except Exception:
        return None


def _ocr_cache_key(image_path: Path, psm: int) -> tuple[str, int, int, int]:
    stat = image_path.stat()
    return (str(image_path), int(psm), stat.st_mtime_ns, stat.st_size)


def _display_metrics() -> Optional[dict[str, float]]:
    try:
        import Quartz

        display_id = Quartz.CGMainDisplayID()
        bounds = Quartz.CGDisplayBounds(display_id)
        point_width = float(bounds.size.width)
        point_height = float(bounds.size.height)
        pixel_width = float(Quartz.CGDisplayPixelsWide(display_id))
        pixel_height = float(Quartz.CGDisplayPixelsHigh(display_id))
        return {
            "display_width": point_width,
            "display_height": point_height,
            "display_scale_x": round(pixel_width / max(point_width, 1.0), 4),
            "display_scale_y": round(pixel_height / max(point_height, 1.0), 4),
        }
    except Exception:
        return None


def _cleanup_old_screenshots() -> list[str]:
    """Remove oldest screenshots if more than _MAX_SCREENSHOTS exist.

    Returns list of deleted paths.
    """
    pngs = _managed_workspace_screenshots()
    deleted = _cleanup_orphaned_metadata()
    while len(pngs) >= _MAX_SCREENSHOTS:
        oldest = pngs.pop(0)
        deleted.extend(_delete_screenshot_artifact(oldest))
    return deleted


def _cleanup_workspace_screenshots(keep_path: Optional[Path]) -> list[str]:
    deleted = _cleanup_orphaned_metadata()
    keep_resolved = keep_path.resolve() if keep_path and keep_path.exists() else None
    for image_path in _managed_workspace_screenshots():
        if keep_resolved and image_path.resolve() == keep_resolved:
            continue
        deleted.extend(_delete_screenshot_artifact(image_path))
    return deleted


def _cleanup_probe_capture() -> list[str]:
    return _delete_screenshot_artifact(_probe_capture_path())


def _cleanup_internal_metadata() -> list[str]:
    deleted: list[str] = []
    for metadata_path in _metadata_dir().glob("*.json"):
        metadata_path.unlink(missing_ok=True)
        deleted.append(str(metadata_path))
    return deleted


def _is_managed_workspace_screenshot(image_path: Optional[Path]) -> bool:
    if not image_path:
        return False
    try:
        resolved = image_path.resolve()
    except FileNotFoundError:
        return False
    if resolved.parent != screenshot_dir().resolve():
        return False
    return resolved.suffix == ".png" and any(
        resolved.name.startswith(prefix) for prefix in _LEGACY_SCREENSHOT_PREFIXES
    )


def ts() -> str:
    return time.strftime("%Y%m%d_%H%M%S")


def _json(obj: dict[str, Any]) -> None:
    """Print JSON result and flush."""
    print(json.dumps(obj, indent=2))
    sys.stdout.flush()


def _ok(**kw: Any) -> None:
    _json({"ok": True, **kw})


def _err(msg: str, **kw: Any) -> None:
    _json({"ok": False, "error": msg, **kw})
    sys.exit(1)


PID_FILE = _internal_dir() / ".aioserver.pid"


def _save_pid(pid: int) -> None:
    PID_FILE.write_text(str(pid))


def _read_pid() -> Optional[int]:
    if PID_FILE.exists():
        try:
            return int(PID_FILE.read_text().strip())
        except ValueError:
            pass
    return None


def _is_running(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def _stop_existing_aioserver() -> None:
    old_pid = _read_pid()
    if old_pid and _is_running(old_pid):
        os.kill(old_pid, signal.SIGTERM)
        time.sleep(0.5)
    subprocess.run(["pkill", "-f", "AIOServer"], capture_output=True)
    time.sleep(0.3)


def _resolve_optional_path(path_str: Optional[str], error_label: str) -> Optional[Path]:
    if not path_str:
        return None
    resolved = Path(path_str).expanduser().resolve()
    if not resolved.exists():
        _err(f"{error_label} not found: {resolved}")
    return resolved


def _normalize_launch_app(app_name: Optional[str]) -> Optional[str]:
    if not app_name:
        return None

    normalized = app_name.strip().lower()
    aliases = {
        "youtube": "youtube",
        "netflix": "netflix",
        "disney+": "disneyplus",
        "disneyplus": "disneyplus",
        "disney-plus": "disneyplus",
        "hulu": "hulu",
    }
    resolved = aliases.get(normalized)
    if not resolved:
        _err(f"Unsupported app: {app_name}", supported_apps=sorted(set(aliases.values())))
    return resolved


def _launch_aioserver(
    rom_path: Optional[Path],
    input_script_path: Optional[Path],
    wait_ms: int,
    app_name: Optional[str] = None,
) -> dict[str, Any]:
    if not aioserver_path().exists():
        _err("AIOServer not built. Run 'make build' first.")

    if rom_path and app_name:
        _err("Choose either a ROM or an app launch target, not both")

    _stop_existing_aioserver()

    cmd = [str(aioserver_path())]
    if rom_path:
        cmd += ["--rom", str(rom_path)]
    if app_name:
        cmd += ["--launch-app", app_name]
    if input_script_path:
        cmd += ["--input-script", str(input_script_path)]

    log_path = output_dir() / f"session_{ts()}.log"
    cmd += ["--log-file", str(log_path)]

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    _save_pid(proc.pid)

    start_time = time.monotonic()
    window = _wait_for_window_info(proc.pid, timeout_ms=max(min(wait_ms, 2000), 250))

    remaining_ms = wait_ms - int((time.monotonic() - start_time) * 1000)
    if remaining_ms > 0:
        time.sleep(remaining_ms / 1000.0)

    if proc.poll() is not None:
        _err("AIOServer exited immediately", returncode=proc.returncode)

    if not window:
        window = _wait_for_window_info(proc.pid, timeout_ms=2000)

    return {
        "pid": proc.pid,
        "log": str(log_path),
        "window": _window_bounds(window),
        "window_id": window.get("id") if window else None,
        "process": proc,
    }


def _kill_aioserver() -> None:
    pid = _read_pid()
    if pid and _is_running(pid):
        os.kill(pid, signal.SIGTERM)
        time.sleep(0.5)
    subprocess.run(["pkill", "-f", "AIOServer"], capture_output=True)
    if PID_FILE.exists():
        PID_FILE.unlink()


def _capture_screenshot(
    output_path: Optional[str],
    window_only: bool,
    allow_fullscreen_fallback: bool,
    label: Optional[str] = None,
) -> dict[str, Any]:
    out = Path(output_path) if output_path else _default_screenshot_path(label)
    out.parent.mkdir(parents=True, exist_ok=True)

    cleaned = _cleanup_old_screenshots()
    mode = "full_screen"
    window_info = None

    if window_only:
        expected_pid = _read_pid()
        window_info = _wait_for_window_info(expected_pid, timeout_ms=1500)
        if window_info and window_info.get("id") is not None:
            subprocess.run(
                ["screencapture", "-x", "-o", "-l", str(window_info["id"]), str(out)],
                capture_output=True,
                timeout=10,
            )
            mode = "window_id"
        elif window_info:
            rect = (
                f"{window_info['x']},{window_info['y']},"
                f"{window_info['w']},{window_info['h']}"
            )
            subprocess.run(["screencapture", "-x", "-R", rect, str(out)],
                           capture_output=True, timeout=10)
            mode = "window_rect"
        elif not allow_fullscreen_fallback:
            _err("AIOServer window not found")

    if not out.exists() or out.stat().st_size == 0:
        if window_only and not allow_fullscreen_fallback:
            _err("Screenshot capture failed")
        subprocess.run(["screencapture", "-x", str(out)],
                       capture_output=True, timeout=10)
        mode = "full_screen"

    if not out.exists() or out.stat().st_size == 0:
        _err("Screenshot capture failed")

    info: dict[str, Any] = {
        "path": str(out),
        "size_bytes": out.stat().st_size,
        "capture_mode": mode,
    }
    if window_info:
        info["window"] = _window_bounds(window_info)
        info["window_id"] = window_info.get("id")
    display_metrics = _display_metrics()
    if display_metrics:
        info.update(display_metrics)
    if cleaned:
        info["cleaned"] = cleaned
    try:
        from PIL import Image
        img = Image.open(out)
        info["width"] = img.width
        info["height"] = img.height
        rgba = img.convert("RGBA")
        pixels = list(rgba.getdata())
        total = len(pixels)
        if total > 0:
            transparent = sum(1 for _, _, _, alpha in pixels if alpha < 8)
            non_black = sum(1 for red, green, blue, alpha in pixels if alpha >= 8 and red + green + blue > 30)
            info["transparent_ratio"] = round(transparent / total, 4)
            info["nonblack_ratio"] = round(non_black / total, 4)
            info["looks_blank"] = info["transparent_ratio"] > 0.95 or info["nonblack_ratio"] < 0.01
        if window_info:
            info["window_scale_x"] = round(img.width / max(window_info["w"], 1), 4)
            info["window_scale_y"] = round(img.height / max(window_info["h"], 1), 4)
    except Exception:
        pass

    if info.get("looks_blank") and mode != "full_screen" and allow_fullscreen_fallback:
        subprocess.run(["screencapture", "-x", str(out)],
                       capture_output=True, timeout=10)
        if not out.exists() or out.stat().st_size == 0:
            _err("Screenshot capture failed")
        info = {
            "path": str(out),
            "size_bytes": out.stat().st_size,
            "capture_mode": "full_screen_fallback_after_blank_window",
            "window_capture_failed": True,
        }
        if window_info:
            info["window"] = _window_bounds(window_info)
            info["window_id"] = window_info.get("id")
        if cleaned:
            info["cleaned"] = cleaned
        display_metrics = _display_metrics()
        if display_metrics:
            info.update(display_metrics)
        try:
            from PIL import Image
            img = Image.open(out)
            info["width"] = img.width
            info["height"] = img.height
            rgba = img.convert("RGBA")
            pixels = list(rgba.getdata())
            total = len(pixels)
            if total > 0:
                transparent = sum(1 for _, _, _, alpha in pixels if alpha < 8)
                non_black = sum(1 for red, green, blue, alpha in pixels if alpha >= 8 and red + green + blue > 30)
                info["transparent_ratio"] = round(transparent / total, 4)
                info["nonblack_ratio"] = round(non_black / total, 4)
                info["looks_blank"] = info["transparent_ratio"] > 0.95 or info["nonblack_ratio"] < 0.01
        except Exception:
            pass
    return info


# ---------------------------------------------------------------------------
# Window management (macOS)
# ---------------------------------------------------------------------------

def _window_bounds(window_info: Optional[dict[str, Any]]) -> Optional[dict[str, int]]:
    if not window_info:
        return None
    return {
        "x": int(window_info["x"]),
        "y": int(window_info["y"]),
        "w": int(window_info["w"]),
        "h": int(window_info["h"]),
    }


def _find_window_info(expected_pid: Optional[int] = None) -> Optional[dict[str, Any]]:
    """Return metadata for the best AIOServer window via Quartz CGWindowList.

    Uses kCGWindowListOptionAll so Qt windows that don't expose AXWindow
    accessibility roles are still found.
    """
    try:
        import Quartz
        wl = Quartz.CGWindowListCopyWindowInfo(
            Quartz.kCGWindowListOptionAll, Quartz.kCGNullWindowID)
        layer_zero_candidates: list[dict[str, Any]] = []
        fallback_candidates: list[dict[str, Any]] = []
        for w in wl:
            owner = w.get("kCGWindowOwnerName", "")
            if "AIOServer" not in owner:
                continue
            owner_pid = int(w.get("kCGWindowOwnerPID", 0) or 0)
            if expected_pid and owner_pid and owner_pid != expected_pid:
                continue
            b = w.get("kCGWindowBounds")
            if not b:
                continue
            width = int(b.get("Width", 0))
            height = int(b.get("Height", 0))
            if width < 50 or height < 50:
                continue  # skip menu bar and tiny helper windows
            candidate = {
                "id": int(w.get("kCGWindowNumber", 0) or 0),
                "pid": owner_pid,
                "owner": owner,
                "name": w.get("kCGWindowName", ""),
                "layer": int(w.get("kCGWindowLayer", 0) or 0),
                "x": int(b["X"]),
                "y": int(b["Y"]),
                "w": width,
                "h": height,
                "area": width * height,
            }
            if candidate["layer"] == 0:
                layer_zero_candidates.append(candidate)
            else:
                fallback_candidates.append(candidate)
        if layer_zero_candidates:
            return max(layer_zero_candidates, key=lambda candidate: candidate["area"])
        if fallback_candidates:
            return max(fallback_candidates, key=lambda candidate: candidate["area"])
    except Exception:
        pass
    return None


def _wait_for_window_info(expected_pid: Optional[int], timeout_ms: int) -> Optional[dict[str, Any]]:
    deadline = time.time() + (timeout_ms / 1000.0)
    while time.time() < deadline:
        window_info = _find_window_info(expected_pid)
        if window_info:
            return window_info
        time.sleep(0.1)
    return _find_window_info(expected_pid)


def _find_window_bounds() -> Optional[dict[str, int]]:
    return _window_bounds(_find_window_info(_read_pid()))


def _focus_window() -> bool:
    """Bring AIOServer window to front."""
    script = (
        'tell application "System Events"\n'
        '  set procs to every process whose name contains "AIOServer"\n'
        '  if (count of procs) = 0 then return "NOT_FOUND"\n'
        '  set frontmost of item 1 of procs to true\n'
        'end tell\n'
        'return "OK"'
    )
    r = subprocess.run(["osascript", "-e", script], capture_output=True, text=True, timeout=10)
    return "OK" in r.stdout


def _ensure_window_focus() -> Optional[dict[str, Any]]:
    window_info = _find_window_info(_read_pid())
    if not window_info:
        return None
    if not _focus_window():
        return None
    time.sleep(0.2)
    return _find_window_info(_read_pid()) or window_info


def _image_to_screen_coordinates(
    image_path: Path,
    image_x: int,
    image_y: int,
    bounds: Optional[dict[str, int]],
) -> tuple[int, int, dict[str, Any]]:
    from PIL import Image

    img = Image.open(image_path)
    if img.width <= 0 or img.height <= 0:
        _err(f"Image has invalid dimensions: {image_path}")

    metadata = _read_screenshot_metadata(image_path) or {}
    capture_mode = str(metadata.get("capture_mode", ""))
    if capture_mode.startswith("full_screen"):
        display_scale_x = metadata.get("display_scale_x")
        display_scale_y = metadata.get("display_scale_y")
        if display_scale_x and display_scale_y:
            screen_x = round(image_x / float(display_scale_x))
            screen_y = round(image_y / float(display_scale_y))
            return screen_x, screen_y, {
                "image": str(image_path),
                "image_x": image_x,
                "image_y": image_y,
                "image_width": img.width,
                "image_height": img.height,
                "capture_mode": capture_mode,
                "coordinate_space": "screen",
                "display_scale_x": display_scale_x,
                "display_scale_y": display_scale_y,
            }

    if not bounds:
        display_metrics = _display_metrics()
        if display_metrics:
            display_width = float(display_metrics["display_width"])
            display_height = float(display_metrics["display_height"])
            if display_width > 0 and display_height > 0:
                screen_x = round((image_x / img.width) * display_width)
                screen_y = round((image_y / img.height) * display_height)
                return screen_x, screen_y, {
                    "image": str(image_path),
                    "image_x": image_x,
                    "image_y": image_y,
                    "image_width": img.width,
                    "image_height": img.height,
                    "capture_mode": capture_mode or "screen_estimate",
                    "coordinate_space": "screen_estimate",
                    "display_width": display_width,
                    "display_height": display_height,
                }
        _err("Window bounds are required for window-relative image clicks")

    window_bounds = cast(dict[str, int], bounds)

    local_x = round((image_x / img.width) * window_bounds["w"])
    local_y = round((image_y / img.height) * window_bounds["h"])
    local_x = max(0, min(local_x, max(window_bounds["w"] - 1, 0)))
    local_y = max(0, min(local_y, max(window_bounds["h"] - 1, 0)))

    screen_x = window_bounds["x"] + local_x
    screen_y = window_bounds["y"] + local_y
    return screen_x, screen_y, {
        "image": str(image_path),
        "image_x": image_x,
        "image_y": image_y,
        "image_width": img.width,
        "image_height": img.height,
        "capture_mode": capture_mode or "window",
        "coordinate_space": "window",
        "window_x": window_bounds["x"],
        "window_y": window_bounds["y"],
        "window_width": window_bounds["w"],
        "window_height": window_bounds["h"],
        "window_relative_x": local_x,
        "window_relative_y": local_y,
    }


def _normalize_text(text: str) -> str:
    return re.sub(r"\s+", " ", text.strip().lower())


def _resolve_image_target(image_target: Optional[str], last_screenshot: Optional[str]) -> Optional[Path]:
    resolved_target = image_target
    if not resolved_target or resolved_target == "$LAST_SCREENSHOT":
        resolved_target = last_screenshot
    elif last_screenshot and "$LAST_SCREENSHOT" in resolved_target:
        resolved_target = resolved_target.replace("$LAST_SCREENSHOT", last_screenshot)

    if not resolved_target:
        return None

    image_path = Path(resolved_target).expanduser().resolve()
    if not image_path.exists():
        return None
    return image_path


def _ocr_words(image_path: Path, psm: int = 6) -> list[dict[str, Any]]:
    cache_key = _ocr_cache_key(image_path, psm)
    cached = _OCR_WORD_CACHE.get(cache_key)
    if cached is not None:
        return cached

    if not shutil.which("tesseract"):
        raise RuntimeError("tesseract not installed")

    result = subprocess.run(
        ["tesseract", str(image_path), "stdout", "--psm", str(psm), "tsv"],
        capture_output=True,
        text=True,
        timeout=15,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "tesseract OCR failed")

    lines = result.stdout.splitlines()
    if not lines:
        return []

    words: list[dict[str, Any]] = []
    for line in lines[1:]:
        parts = line.split("\t")
        if len(parts) < 12:
            continue
        text = parts[11].strip()
        if not text:
            continue
        try:
            conf = float(parts[10])
            left = int(parts[6])
            top = int(parts[7])
            width = int(parts[8])
            height = int(parts[9])
            line_key = (int(parts[2]), int(parts[3]), int(parts[4]))
        except ValueError:
            continue
        if width <= 0 or height <= 0:
            continue
        words.append({
            "text": text,
            "normalized": _normalize_text(text),
            "conf": conf,
            "left": left,
            "top": top,
            "width": width,
            "height": height,
            "right": left + width,
            "bottom": top + height,
            "line_key": line_key,
        })
    _OCR_WORD_CACHE[cache_key] = words
    return words


def _read_plan_argument(plan_arg: str) -> str:
    if plan_arg == "-":
        plan_text = sys.stdin.read()
        if not plan_text.strip():
            _err("Plan stdin was empty")
        return plan_text

    candidate_path = Path(plan_arg).expanduser()
    if candidate_path.exists() and candidate_path.is_file():
        plan_text = candidate_path.read_text()
        if not plan_text.strip():
            _err(f"Plan file is empty: {candidate_path}")
        return plan_text

    return plan_arg


def _match_query_to_words(words: list[dict[str, Any]], query: str) -> Optional[dict[str, Any]]:
    target = _normalize_text(query)
    if not target:
        return None

    target_compact = target.replace(" ", "")
    best_match: Optional[dict[str, Any]] = None

    grouped: dict[tuple[int, int, int], list[dict[str, Any]]] = {}
    for word in words:
        grouped.setdefault(word["line_key"], []).append(word)

    for line_words in grouped.values():
        line_words.sort(key=lambda item: item["left"])
        for start in range(len(line_words)):
            for end in range(start, len(line_words)):
                span_words = line_words[start:end + 1]
                span_text = " ".join(word["normalized"] for word in span_words if word["normalized"])
                if not span_text:
                    continue
                span_compact = span_text.replace(" ", "")
                if not (
                    span_text == target
                    or span_compact == target_compact
                    or target in span_text
                    or target_compact in span_compact
                ):
                    continue

                avg_conf = sum(word["conf"] for word in span_words) / len(span_words)
                candidate = {
                    "text": " ".join(word["text"] for word in span_words),
                    "normalized": span_text,
                    "left": min(word["left"] for word in span_words),
                    "top": min(word["top"] for word in span_words),
                    "right": max(word["right"] for word in span_words),
                    "bottom": max(word["bottom"] for word in span_words),
                    "confidence": round(avg_conf, 2),
                }
                candidate["width"] = candidate["right"] - candidate["left"]
                candidate["height"] = candidate["bottom"] - candidate["top"]
                candidate["center_x"] = candidate["left"] + candidate["width"] // 2
                candidate["center_y"] = candidate["top"] + candidate["height"] // 2
                if not best_match or candidate["confidence"] > best_match["confidence"]:
                    best_match = candidate

    if best_match:
        return best_match

    for word in words:
        normalized = word["normalized"]
        if not normalized:
            continue
        if normalized == target:
            candidate = {
                "text": word["text"],
                "normalized": normalized,
                "left": word["left"],
                "top": word["top"],
                "right": word["right"],
                "bottom": word["bottom"],
                "width": word["width"],
                "height": word["height"],
                "center_x": word["left"] + word["width"] // 2,
                "center_y": word["top"] + word["height"] // 2,
                "confidence": round(word["conf"], 2),
            }
            if not best_match or candidate["confidence"] > best_match["confidence"]:
                best_match = candidate
            continue
        if len(target) >= 3 and len(normalized) >= 3 and (target in normalized or normalized in target):
            candidate = {
                "text": word["text"],
                "normalized": normalized,
                "left": word["left"],
                "top": word["top"],
                "right": word["right"],
                "bottom": word["bottom"],
                "width": word["width"],
                "height": word["height"],
                "center_x": word["left"] + word["width"] // 2,
                "center_y": word["top"] + word["height"] // 2,
                "confidence": round(word["conf"], 2),
            }
            if not best_match or candidate["confidence"] > best_match["confidence"]:
                best_match = candidate

    return best_match


def _locate_text(image_path: Path, query: str, psm: int = 6) -> dict[str, Any]:
    attempted_psm: list[int] = []
    for candidate_psm in (psm, 11, 6):
        if candidate_psm in attempted_psm:
            continue
        attempted_psm.append(candidate_psm)
        words = _ocr_words(image_path, psm=candidate_psm)
        match = _match_query_to_words(words, query)
        if match:
            return {
                "image": str(image_path),
                "query": query,
                "psm": candidate_psm,
                **match,
            }
    raise RuntimeError(f"Text not found: {query}")


def _ocr_lines(image_path: Path, psm: int = 6) -> list[str]:
    attempted_psm: list[int] = []
    for candidate_psm in (psm, 11, 6):
        if candidate_psm in attempted_psm:
            continue
        attempted_psm.append(candidate_psm)
        words = _ocr_words(image_path, psm=candidate_psm)
        grouped: dict[tuple[int, int, int], list[dict[str, Any]]] = {}
        for word in words:
            grouped.setdefault(word["line_key"], []).append(word)
        lines: list[str] = []
        for key in sorted(grouped.keys()):
            line_words = sorted(grouped[key], key=lambda item: item["left"])
            line_text = " ".join(word["text"] for word in line_words if word["text"].strip())
            if line_text.strip():
                lines.append(line_text.strip())
        if lines:
            return lines
    return []


def _capture_and_locate_text(
    query: str,
    timeout_ms: int,
    interval_ms: int,
    psm: int,
) -> tuple[Path, Optional[dict[str, Any]], list[str]]:
    deadline = time.time() + (timeout_ms / 1000.0)
    last_image: Optional[Path] = None
    last_lines: list[str] = []

    while True:
        capture = _capture_screenshot(
            str(_probe_capture_path()),
            window_only=True,
            allow_fullscreen_fallback=True,
        )
        last_image = Path(capture["path"])
        try:
            match = _locate_text(last_image, query, psm=psm)
            return last_image, match, _ocr_lines(last_image, psm=psm)
        except Exception:
            last_lines = _ocr_lines(last_image, psm=psm)
            if time.time() >= deadline:
                return last_image, None, last_lines
            time.sleep(interval_ms / 1000.0)


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_boot(args: argparse.Namespace) -> None:
    rom = _resolve_optional_path(args.rom, "ROM")
    script_path = _resolve_optional_path(args.input_script, "Input script")
    app_name = _normalize_launch_app(getattr(args, "app", None))
    launch = _launch_aioserver(rom, script_path, int(args.wait_ms), app_name=app_name)
    _ok(
        pid=launch["pid"],
        log=launch["log"],
        window=launch["window"],
        rom=str(rom) if rom else None,
        app=app_name,
    )


def cmd_focus(args: argparse.Namespace) -> None:
    if _focus_window():
        time.sleep(0.3)
        bounds = _find_window_bounds()
        _ok(window=bounds)
    else:
        _err("AIOServer window not found")


def cmd_screenshot(args: argparse.Namespace) -> None:
    info = _capture_screenshot(
        args.output,
        args.window,
        allow_fullscreen_fallback=False,
        label=getattr(args, "label", None),
    )
    _ok(**info)


def cmd_snapshot(args: argparse.Namespace) -> None:
    rom = _resolve_optional_path(args.rom, "ROM")
    script_path = _resolve_optional_path(args.input_script, "Input script")
    app_name = _normalize_launch_app(getattr(args, "app", None))
    force_full_screen = bool(args.full_screen)
    window_only = not force_full_screen
    allow_fullscreen_fallback = not bool(args.window_only)

    launch: Optional[dict[str, Any]] = None
    screenshot: Optional[dict[str, Any]] = None

    try:
        launch = _launch_aioserver(rom, script_path, int(args.wait_ms), app_name=app_name)
        screenshot = _capture_screenshot(
            args.output,
            window_only=window_only,
            allow_fullscreen_fallback=allow_fullscreen_fallback,
            label=getattr(args, "label", None) or app_name or (rom.stem if rom else "snapshot"),
        )
    finally:
        _kill_aioserver()

    _ok(
        pid=launch["pid"] if launch else None,
        log=launch["log"] if launch else None,
        rom=str(rom) if rom else None,
        app=app_name,
        killed=True,
        **(screenshot or {}),
    )


def cmd_click(args: argparse.Namespace) -> None:
    bounds: Optional[dict[str, int]] = None
    if not args.no_focus:
        focused = _ensure_window_focus()
        if not focused:
            _err("AIOServer window not found or could not be focused for click")
            return
        bounds = _window_bounds(focused)

    click_meta: dict[str, Any] = {}
    if args.image_x is not None or args.image_y is not None:
        if args.image_x is None or args.image_y is None:
            _err("Both --image-x and --image-y are required together")
            return
        image_path = _resolve_optional_path(args.image, "Image")
        if not image_path:
            _err("--image is required when using --image-x/--image-y")
            return
        metadata = _read_screenshot_metadata(image_path) or {}
        capture_mode = str(metadata.get("capture_mode", ""))
        if not bounds and not capture_mode.startswith("full_screen"):
            bounds = _find_window_bounds()
        if not bounds and not capture_mode.startswith("full_screen"):
            _err("AIOServer window not found for image-based click")
            return
        x, y, click_meta = _image_to_screen_coordinates(
            image_path,
            int(args.image_x),
            int(args.image_y),
            bounds,
        )
    else:
        if args.x is None or args.y is None:
            _err("--x and --y are required unless using --image-x/--image-y")
            return
        x, y = int(args.x), int(args.y)
        if args.window_relative:
            if not bounds:
                bounds = _find_window_bounds()
            if not bounds:
                _err("AIOServer window not found for relative click")
                return
            x += bounds["x"]
            y += bounds["y"]

    subprocess.run(["cliclick", f"c:{x},{y}"], capture_output=True, timeout=5)
    _ok(clicked_x=x, clicked_y=y, **click_meta)


def cmd_click_percent(args: argparse.Namespace) -> None:
    image_path = _resolve_optional_path(args.image, "Image")
    if not image_path:
        _err("--image is required for click-percent")
        return
    from PIL import Image

    img = Image.open(image_path)
    image_x = round((float(args.percent_x) / 100.0) * img.width)
    image_y = round((float(args.percent_y) / 100.0) * img.height)

    click_args = argparse.Namespace(
        x=None,
        y=None,
        image_x=image_x,
        image_y=image_y,
        image=str(image_path),
        window_relative=False,
        no_focus=False,
    )
    cmd_click(click_args)


def cmd_find_text(args: argparse.Namespace) -> None:
    image_path = _resolve_image_target(args.image, None)
    if not image_path:
        capture = _capture_screenshot(None, window_only=True, allow_fullscreen_fallback=True)
        image_path = Path(capture["path"])
    try:
        match = _locate_text(image_path, args.text, psm=int(args.psm))
        match["matched_text"] = match["text"]
        match["text"] = args.text
        _ok(**match)
    except Exception as e:
        _err(str(e), image=str(image_path), query=args.text)


def cmd_click_text(args: argparse.Namespace) -> None:
    image_path = _resolve_image_target(args.image, None)
    psm = int(args.psm)
    if image_path:
        try:
            match = _locate_text(image_path, args.text, psm=psm)
            ocr_lines = _ocr_lines(image_path, psm=psm)
        except Exception as e:
            _err(str(e), image=str(image_path), query=args.text, ocr_lines=_ocr_lines(image_path, psm=psm))
            return
    else:
        image_path, match, ocr_lines = _capture_and_locate_text(
            args.text,
            timeout_ms=int(args.timeout_ms),
            interval_ms=int(args.interval_ms),
            psm=psm,
        )
        if not match:
            _err(f"Text not found: {args.text}", image=str(image_path), query=args.text, ocr_lines=ocr_lines)
            return

    click_args = argparse.Namespace(
        x=None,
        y=None,
        image_x=match["center_x"],
        image_y=match["center_y"],
        image=str(image_path),
        window_relative=False,
        no_focus=False,
    )
    cmd_click(click_args)


def cmd_wait_for_text(args: argparse.Namespace) -> None:
    start = time.time()
    image_path, match, ocr_lines = _capture_and_locate_text(
        args.text,
        timeout_ms=int(args.timeout_ms),
        interval_ms=int(args.interval_ms),
        psm=int(args.psm),
    )
    if match:
        match["matched_text"] = match["text"]
        match["text"] = args.text
        _ok(elapsed_ms=round((time.time() - start) * 1000), ocr_lines=ocr_lines, **match)
        return
    _err(f"Text not found: {args.text}", image=str(image_path), query=args.text, ocr_lines=ocr_lines)


# Map common game keys to cliclick key names
_KEY_MAP = {
    "up": "arrow-up", "down": "arrow-down",
    "left": "arrow-left", "right": "arrow-right",
    "enter": "return", "esc": "escape",
    "start": "return",
    "a": "x", "b": "z",
    "l": "a", "r": "s",
    "select": "space",
}


def cmd_key(args: argparse.Namespace) -> None:
    """Send a key press via cliclick."""
    if not _ensure_window_focus():
        _err("AIOServer window not found or could not be focused for key input")
    key_name = args.name
    mods = args.modifiers or ""
    mapped = _KEY_MAP.get(key_name.lower(), key_name.lower())

    if mods:
        subprocess.run(["cliclick", f"kd:{mods}", f"kp:{mapped}", f"ku:{mods}"],
                       capture_output=True, timeout=5)
    else:
        subprocess.run(["cliclick", f"kp:{mapped}"],
                       capture_output=True, timeout=5)

    _ok(key=key_name, mapped_to=mapped, modifiers=mods)


def cmd_key_sequence(args: argparse.Namespace) -> None:
    if not _ensure_window_focus():
        _err("AIOServer window not found or could not be focused for key input")
    sequence = [item for item in args.keys.split() if item]
    sent: list[str] = []
    for item in sequence:
        mapped_name = str(_KEY_MAP.get(item.lower(), item.lower()))
        subprocess.run(["cliclick", f"kp:{mapped_name}"], capture_output=True, timeout=5)
        sent.append(mapped_name)
    _ok(mapped_to=sent)


def cmd_type(args: argparse.Namespace) -> None:
    if not _ensure_window_focus():
        _err("AIOServer window not found or could not be focused for text input")
    subprocess.run(["cliclick", f"t:{args.text}"], capture_output=True, timeout=10)
    _ok(typed=args.text)


def cmd_wait(args: argparse.Namespace) -> None:
    ms = int(args.ms)
    time.sleep(ms / 1000.0)
    _ok(waited_ms=ms)


def _analyze_image(img_path: Path, do_ocr: bool, do_colors: bool,
                   do_nonblack: bool, region: Optional[str]) -> dict[str, Any]:
    """Core image analysis, returns dict of results."""
    from PIL import Image
    img = Image.open(img_path)
    results: dict[str, Any] = {"path": str(img_path)}

    if region:
        parts = [int(p) for p in region.split(",")]
        if len(parts) == 4:
            rx, ry, rw, rh = parts
            img = img.crop((rx, ry, rx + rw, ry + rh))
            crop_path = img_path.with_stem(img_path.stem + "_region")
            img.save(crop_path)
            results["region_crop"] = str(crop_path)

    results["width"] = img.width
    results["height"] = img.height

    if do_nonblack:
        pixels = list(img.convert("RGB").getdata())
        total = len(pixels)
        non_black = sum(1 for r, g, b in pixels if r + g + b > 30)
        ratio = non_black / total if total > 0 else 0
        results["nonblack_ratio"] = round(ratio, 4)
        results["is_black_screen"] = ratio < 0.01

    if do_colors:
        pixels = list(img.convert("RGB").getdata())
        total = len(pixels)
        r_avg = sum(p[0] for p in pixels) / total
        g_avg = sum(p[1] for p in pixels) / total
        b_avg = sum(p[2] for p in pixels) / total
        results["color_stats"] = {
            "avg_r": round(r_avg, 1),
            "avg_g": round(g_avg, 1),
            "avg_b": round(b_avg, 1),
            "brightness": round((r_avg + g_avg + b_avg) / 3, 1),
        }
        grid: list[list[list[int]]] = []
        for gy in range(5):
            row: list[list[int]] = []
            for gx in range(5):
                sx = int(gx * max(img.width - 1, 1) / 4)
                sy = int(gy * max(img.height - 1, 1) / 4)
                px = img.convert("RGB").getpixel((sx, sy))
                row.append(list(px))
            grid.append(row)
        results["color_grid_5x5"] = grid

    if do_ocr:
        if shutil.which("tesseract"):
            try:
                tmp = output_dir() / "_ocr_temp.png"
                img.save(tmp)
                r = subprocess.run(
                    ["tesseract", str(tmp), "-", "--psm", "6"],
                    capture_output=True, text=True, timeout=15
                )
                text = r.stdout.strip()
                results["ocr_text"] = text
                results["ocr_lines"] = [line for line in text.split("\n") if line.strip()]
                tmp.unlink(missing_ok=True)
            except Exception as e:
                results["ocr_error"] = str(e)
        else:
            results["ocr_error"] = "tesseract not installed"

    return results


def cmd_analyze(args: argparse.Namespace) -> None:
    img_path = Path(args.image).expanduser().resolve()
    if not img_path.exists():
        _err(f"Image not found: {img_path}")

    try:
        results = _analyze_image(img_path, args.ocr, args.colors,
                                 args.nonblack, args.region)
        _ok(**results)
    except Exception as e:
        _err(f"Analysis failed: {e}")


def cmd_status(args: argparse.Namespace) -> None:
    pid = _read_pid()
    running = pid is not None and _is_running(pid)
    window_info = _find_window_info(pid) if running else None
    _ok(
        pid=pid,
        running=running,
        window=_window_bounds(window_info),
        window_id=window_info.get("id") if window_info else None,
    )


def cmd_kill(args: argparse.Namespace) -> None:
    _kill_aioserver()
    _ok(killed=True)


def cmd_session(args: argparse.Namespace) -> None:
    """Execute a structured plan: a sequence of actions."""
    try:
        plan = json.loads(_read_plan_argument(args.plan))
    except json.JSONDecodeError as e:
        _err(f"Invalid plan JSON: {e}")
        return

    if not isinstance(plan, list):
        _err("Plan must be a JSON array of action objects")
        return

    results: list[dict[str, Any]] = []
    last_screenshot: Optional[str] = None
    retained_screenshot: Optional[Path] = None
    capture_context = "ui"

    for i, step in enumerate(plan):
        action = step.get("action", "")
        step_result: dict[str, Any] = {"step": i, "action": action}

        try:
            if action == "boot":
                rom = step.get("rom", "")
                app_name = _normalize_launch_app(step.get("app") or step.get("launch_app"))
                if not aioserver_path().exists():
                    step_result["error"] = "AIOServer not built"
                else:
                    rom_path = None
                    if rom:
                        rom_path = Path(rom).expanduser().resolve()
                        if not rom_path.exists():
                            step_result["error"] = f"ROM not found: {rom_path}"
                            results.append(step_result)
                            break
                    input_script = None
                    if step.get("input_script"):
                        input_script = Path(step["input_script"]).expanduser().resolve()
                        if not input_script.exists():
                            step_result["error"] = f"Input script not found: {input_script}"
                            results.append(step_result)
                            break
                    wait_ms = int(step.get("wait_ms", step.get("duration_ms", 3000)))
                    launch = _launch_aioserver(
                        rom_path,
                        input_script,
                        wait_ms,
                        app_name=app_name,
                    )
                    step_result["pid"] = launch["pid"]
                    step_result["log"] = launch["log"]
                    step_result["window"] = launch["window"]
                    step_result["window_id"] = launch["window_id"]
                    step_result["app"] = app_name
                    capture_context = app_name or (rom_path.stem if rom_path else "ui")

            elif action == "focus":
                if _focus_window():
                    time.sleep(0.3)
                    step_result["window"] = _find_window_bounds()
                else:
                    step_result["error"] = "Window not found"

            elif action == "wait":
                ms = step.get("ms")
                if ms is None:
                    if "duration_ms" in step:
                        ms = step["duration_ms"]
                    elif "duration" in step:
                        ms = float(step["duration"]) * 1000
                    else:
                        ms = 1000
                time.sleep(ms / 1000.0)
                step_result["waited_ms"] = ms

            elif action == "key":
                key_name = step.get("name", "")
                key_sequence = step.get("keys", "")
                if step.get("focus", True) and not _ensure_window_focus():
                    step_result["error"] = "AIOServer window not found or could not be focused for key input"
                    results.append(step_result)
                    continue
                if key_sequence:
                    sequence = [item for item in key_sequence.split() if item]
                    sent: list[str] = []
                    for item in sequence:
                        mapped_name = str(_KEY_MAP.get(item.lower(), item.lower()))
                        subprocess.run(["cliclick", f"kp:{mapped_name}"],
                                       capture_output=True, timeout=5)
                        sent.append(mapped_name)
                    step_result["mapped_to"] = sent
                elif not key_name:
                    step_result["error"] = "Missing 'name'"
                else:
                    mapped = _KEY_MAP.get(key_name.lower(), key_name.lower())
                    mods = step.get("modifiers", "")
                    if mods:
                        subprocess.run(["cliclick", f"kd:{mods}", f"kp:{mapped}", f"ku:{mods}"],
                                       capture_output=True, timeout=5)
                    else:
                        subprocess.run(["cliclick", f"kp:{mapped}"],
                                       capture_output=True, timeout=5)
                    step_result["mapped_to"] = mapped

            elif action == "key_sequence":
                if step.get("focus", True) and not _ensure_window_focus():
                    step_result["error"] = "AIOServer window not found or could not be focused for key input"
                    results.append(step_result)
                    continue
                sequence = [item for item in str(step.get("keys", "")).split() if item]
                sent: list[str] = []
                for item in sequence:
                    mapped_name = str(_KEY_MAP.get(item.lower(), item.lower()))
                    subprocess.run(["cliclick", f"kp:{mapped_name}"], capture_output=True, timeout=5)
                    sent.append(mapped_name)
                step_result["mapped_to"] = sent

            elif action == "click":
                bounds = None
                if step.get("focus", True):
                    focused = _ensure_window_focus()
                    if not focused:
                        step_result["error"] = "AIOServer window not found or could not be focused for click"
                        results.append(step_result)
                        continue
                    bounds = _window_bounds(focused)
                image_x = step.get("image_x")
                image_y = step.get("image_y")
                if image_x is not None or image_y is not None:
                    if image_x is None or image_y is None:
                        step_result["error"] = "Both image_x and image_y are required together"
                        results.append(step_result)
                        continue
                    if not bounds:
                        bounds = _find_window_bounds()
                    if not bounds:
                        step_result["error"] = "AIOServer window not found for image-based click"
                        results.append(step_result)
                        continue
                    image_path = _resolve_image_target(step.get("image"), last_screenshot)
                    if not image_path:
                        step_result["error"] = "No screenshot available for image-based click"
                        results.append(step_result)
                        continue
                    metadata = _read_screenshot_metadata(image_path) or {}
                    capture_mode = str(metadata.get("capture_mode", ""))
                    if not bounds and not capture_mode.startswith("full_screen"):
                        bounds = _find_window_bounds()
                    if not bounds and not capture_mode.startswith("full_screen"):
                        step_result["error"] = "AIOServer window not found for image-based click"
                        results.append(step_result)
                        continue
                    cx, cy, click_meta = _image_to_screen_coordinates(
                        image_path,
                        int(image_x),
                        int(image_y),
                        bounds,
                    )
                    step_result.update(click_meta)
                else:
                    cx, cy = int(step.get("x", 0)), int(step.get("y", 0))
                    if step.get("window_relative"):
                        if not bounds:
                            bounds = _find_window_bounds()
                        if bounds:
                            cx += bounds["x"]
                            cy += bounds["y"]
                subprocess.run(["cliclick", f"c:{cx},{cy}"], capture_output=True, timeout=5)
                step_result["clicked"] = [cx, cy]

            elif action == "type":
                if step.get("focus", True) and not _ensure_window_focus():
                    step_result["error"] = "AIOServer window not found or could not be focused for text input"
                    results.append(step_result)
                    continue
                text = step.get("text", "")
                subprocess.run(["cliclick", f"t:{text}"], capture_output=True, timeout=10)
                step_result["typed"] = text

            elif action == "click_percent":
                image_path = _resolve_image_target(step.get("image"), last_screenshot)
                if not image_path:
                    step_result["error"] = "No screenshot available for percent-based click"
                    results.append(step_result)
                    continue
                from PIL import Image

                img = Image.open(image_path)
                image_x = round((float(step.get("percent_x", 50.0)) / 100.0) * img.width)
                image_y = round((float(step.get("percent_y", 50.0)) / 100.0) * img.height)
                bounds = None
                if step.get("focus", True):
                    focused = _ensure_window_focus()
                    if not focused:
                        step_result["error"] = "AIOServer window not found or could not be focused for click"
                        results.append(step_result)
                        continue
                    bounds = _window_bounds(focused)
                metadata = _read_screenshot_metadata(image_path) or {}
                capture_mode = str(metadata.get("capture_mode", ""))
                if not bounds and not capture_mode.startswith("full_screen"):
                    bounds = _find_window_bounds()
                cx, cy, click_meta = _image_to_screen_coordinates(
                    image_path,
                    image_x,
                    image_y,
                    bounds,
                )
                step_result.update(click_meta)
                subprocess.run(["cliclick", f"c:{cx},{cy}"], capture_output=True, timeout=5)
                step_result["clicked"] = [cx, cy]

            elif action == "find_text":
                image_path = _resolve_image_target(step.get("image"), last_screenshot)
                if not image_path:
                    capture = _capture_screenshot(None, window_only=True, allow_fullscreen_fallback=True)
                    image_path = Path(capture["path"])
                    last_screenshot = str(image_path)
                psm = int(step.get("psm", 6))
                match = _locate_text(image_path, str(step.get("text", "")), psm=psm)
                match["matched_text"] = match["text"]
                match["text"] = str(step.get("text", ""))
                step_result["ocr_lines"] = _ocr_lines(image_path, psm=psm)
                step_result.update(match)

            elif action == "click_text":
                image_path = _resolve_image_target(step.get("image"), last_screenshot)
                psm = int(step.get("psm", 6))
                ocr_lines: list[str] = []
                if not image_path:
                    image_path, match, ocr_lines = _capture_and_locate_text(
                        str(step.get("text", "")),
                        timeout_ms=int(step.get("timeout_ms", 3000)),
                        interval_ms=int(step.get("interval_ms", 350)),
                        psm=psm,
                    )
                    last_screenshot = str(image_path)
                    if not match:
                        step_result["error"] = f"Text not found: {step.get('text', '')}"
                        step_result["ocr_lines"] = ocr_lines
                        results.append(step_result)
                        continue
                else:
                    match = _locate_text(image_path, str(step.get("text", "")), psm=psm)
                    ocr_lines = _ocr_lines(image_path, psm=psm)
                match["matched_text"] = match["text"]
                match["text"] = str(step.get("text", ""))
                bounds = None
                if step.get("focus", True):
                    focused = _ensure_window_focus()
                    if not focused:
                        step_result["error"] = "AIOServer window not found or could not be focused for click_text"
                        results.append(step_result)
                        continue
                    bounds = _window_bounds(focused)
                cx, cy, click_meta = _image_to_screen_coordinates(
                    image_path,
                    int(match["center_x"]),
                    int(match["center_y"]),
                    bounds,
                )
                subprocess.run(["cliclick", f"c:{cx},{cy}"], capture_output=True, timeout=5)
                step_result.update(match)
                step_result["ocr_lines"] = ocr_lines
                step_result.update(click_meta)
                step_result["clicked"] = [cx, cy]

            elif action == "wait_for_text":
                image_path, matched, ocr_lines = _capture_and_locate_text(
                    str(step.get("text", "")),
                    timeout_ms=int(step.get("timeout_ms", 8000)),
                    interval_ms=int(step.get("interval_ms", 500)),
                    psm=int(step.get("psm", 6)),
                )
                last_screenshot = str(image_path)
                step_result["ocr_lines"] = ocr_lines
                if matched:
                    matched["matched_text"] = matched["text"]
                    matched["text"] = str(step.get("text", ""))
                    step_result.update(matched)
                else:
                    step_result["error"] = f"Text not found: {step.get('text', '')}"

            elif action == "status":
                pid = _read_pid()
                running = pid is not None and _is_running(pid)
                window_info = _find_window_info(pid) if running else None
                step_result["pid"] = pid
                step_result["running"] = running
                step_result["window"] = _window_bounds(window_info)
                step_result["window_id"] = window_info.get("id") if window_info else None

            elif action == "screenshot":
                output_path = step.get("output")
                force_full_screen = bool(step.get("full_screen", False))
                window_only = bool(step.get("window", not force_full_screen)) and not force_full_screen
                allow_fullscreen_fallback = bool(step.get("allow_fullscreen_fallback", True))
                screenshot = _capture_screenshot(
                    output_path,
                    window_only,
                    allow_fullscreen_fallback,
                    label=step.get("label") or f"{capture_context}-state",
                )
                step_result.update(screenshot)
                last_screenshot = screenshot["path"]
                screenshot_path = Path(screenshot["path"])
                if _is_managed_workspace_screenshot(screenshot_path):
                    retained_screenshot = screenshot_path
                if screenshot.get("looks_blank"):
                    step_result["warning"] = "Captured image appears blank or transparent"

            elif action == "snapshot":
                force_full_screen = bool(step.get("full_screen", False))
                window_only = bool(step.get("window", not force_full_screen)) and not force_full_screen
                allow_fullscreen_fallback = bool(step.get("allow_fullscreen_fallback", True))
                screenshot = _capture_screenshot(
                    step.get("output"),
                    window_only=window_only,
                    allow_fullscreen_fallback=allow_fullscreen_fallback,
                    label=step.get("label") or f"{capture_context}-snapshot",
                )
                step_result.update(screenshot)
                last_screenshot = screenshot["path"]
                screenshot_path = Path(screenshot["path"])
                if _is_managed_workspace_screenshot(screenshot_path):
                    retained_screenshot = screenshot_path
                if screenshot.get("looks_blank"):
                    step_result["warning"] = "Captured image appears blank or transparent"

            elif action == "analyze":
                image_path = _resolve_image_target(step.get("image"), last_screenshot)
                if not image_path:
                    step_result["error"] = "No image to analyze (take a screenshot first)"
                else:
                    analysis = _analyze_image(
                        image_path,
                        do_ocr=step.get("ocr", False),
                        do_colors=step.get("colors", False),
                        do_nonblack=step.get("nonblack", False),
                        region=step.get("region"),
                    )
                    step_result.update(analysis)

            elif action == "kill":
                _kill_aioserver()
                step_result["killed"] = True

            else:
                step_result["error"] = f"Unknown action: {action}"

        except Exception as e:
            step_result["error"] = str(e)

        results.append(step_result)

        if "error" in step_result and action in ("boot",):
            break

    cleanup_deleted = _cleanup_probe_capture()
    cleanup_deleted.extend(_cleanup_internal_metadata())
    cleanup_deleted.extend(_cleanup_workspace_screenshots(retained_screenshot))
    if retained_screenshot and not retained_screenshot.exists():
        retained_screenshot = None
        last_screenshot = None
    elif retained_screenshot:
        last_screenshot = str(retained_screenshot)

    _ok(steps=results, last_screenshot=last_screenshot, cleaned=cleanup_deleted)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        prog="visual_dev_loop",
        description="Visual Development Loop automation for AIO Server")
    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("boot", help="Launch AIOServer for a ROM or explicit non-ROM app flow")
    p.add_argument("--rom", default=None, help="ROM to load; omit for direct app launch")
    p.add_argument("--app", default=None, help="Streaming app to launch directly (youtube, netflix, disneyplus, hulu)")
    p.add_argument("--wait-ms", default="3000")
    p.add_argument("--input-script")

    sub.add_parser("focus", help="Focus the AIOServer window")

    p = sub.add_parser("screenshot", help="Capture a screenshot")
    p.add_argument("--output")
    p.add_argument("--label", help="Human-readable label for the screenshot filename")
    p.add_argument("--window", action="store_true", default=True)
    p.add_argument("--full-screen", action="store_true")

    p = sub.add_parser("snapshot", help="Boot, capture once, and kill AIOServer")
    p.add_argument("--rom", default=None, help="ROM to load; omit for direct app launch")
    p.add_argument("--app", default=None, help="Streaming app to launch directly (youtube, netflix, disneyplus, hulu)")
    p.add_argument("--wait-ms", default="2000")
    p.add_argument("--input-script")
    p.add_argument("--output")
    p.add_argument("--label", help="Human-readable label for the retained screenshot filename")
    p.add_argument("--full-screen", action="store_true")
    p.add_argument("--window-only", action="store_true")

    p = sub.add_parser("click", help="Click at coordinates")
    p.add_argument("--x", type=int)
    p.add_argument("--y", type=int)
    p.add_argument("--image-x", type=int)
    p.add_argument("--image-y", type=int)
    p.add_argument("--image")
    p.add_argument("--window-relative", action="store_true")
    p.add_argument("--no-focus", action="store_true")

    p = sub.add_parser("click-percent", help="Click using percentages of an image artifact")
    p.add_argument("--percent-x", required=True, type=float)
    p.add_argument("--percent-y", required=True, type=float)
    p.add_argument("--image", required=True)

    p = sub.add_parser("find-text", help="Locate text on a screenshot or current app window")
    p.add_argument("--text", required=True)
    p.add_argument("--image")
    p.add_argument("--psm", default="6")

    p = sub.add_parser("click-text", help="Locate text and click its center")
    p.add_argument("--text", required=True)
    p.add_argument("--image")
    p.add_argument("--psm", default="6")
    p.add_argument("--timeout-ms", default="3000")
    p.add_argument("--interval-ms", default="350")

    p = sub.add_parser("wait-for-text", help="Poll screenshots until text appears")
    p.add_argument("--text", required=True)
    p.add_argument("--timeout-ms", default="8000")
    p.add_argument("--interval-ms", default="500")
    p.add_argument("--psm", default="6")

    p = sub.add_parser("key-sequence", help="Send a focused sequence of keys")
    p.add_argument("--keys", required=True)

    p = sub.add_parser("key", help="Send a key press")
    p.add_argument("--name", required=True)
    p.add_argument("--modifiers")

    p = sub.add_parser("type", help="Type text")
    p.add_argument("--text", required=True)

    p = sub.add_parser("wait", help="Wait N milliseconds")
    p.add_argument("--ms", required=True, type=int)

    p = sub.add_parser("analyze", help="Analyze an image")
    p.add_argument("--image", required=True)
    p.add_argument("--ocr", action="store_true")
    p.add_argument("--colors", action="store_true")
    p.add_argument("--nonblack", action="store_true")
    p.add_argument("--region", help="X,Y,W,H region to crop")

    sub.add_parser("status", help="Check AIOServer status")

    sub.add_parser("kill", help="Kill AIOServer")

    p = sub.add_parser("session", help="Execute a structured plan")
    p.add_argument("--plan", required=True, help="JSON array, '-' for stdin, or a path to a JSON plan file")

    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        sys.exit(1)

    if args.command == "screenshot" and getattr(args, "full_screen", False):
        args.window = False

    dispatch = {
        "boot": cmd_boot, "focus": cmd_focus, "screenshot": cmd_screenshot,
        "snapshot": cmd_snapshot,
        "click": cmd_click, "click-percent": cmd_click_percent,
        "find-text": cmd_find_text, "click-text": cmd_click_text,
        "wait-for-text": cmd_wait_for_text,
        "key": cmd_key, "key-sequence": cmd_key_sequence,
        "type": cmd_type, "wait": cmd_wait,
        "analyze": cmd_analyze, "status": cmd_status, "kill": cmd_kill,
        "session": cmd_session,
    }
    dispatch[args.command](args)


if __name__ == "__main__":
    main()
