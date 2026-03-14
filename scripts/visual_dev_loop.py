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
    visual_dev_loop.py user-test-create [--rom ROM | --app APP] [--wait-ms MS] [--input-script PATH] [--snap-key F9] [--abort-key F10]
    visual_dev_loop.py click   --x X --y Y [--window-relative]
    visual_dev_loop.py click-percent --percent-x X --percent-y Y [--image PATH]
    visual_dev_loop.py key-sequence --keys "Down Down Enter"
    visual_dev_loop.py key     --name KEYNAME [--event press|down|up] [--modifiers MOD]
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
import urllib.error
import urllib.request
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


_MAX_SCREENSHOTS = 10
_SCREENSHOT_PREFIX = "visual_"
_LEGACY_SCREENSHOT_PREFIXES = ("visual_", "screenshot_")
_OCR_WORD_CACHE: dict[tuple[str, int, int, int], list[dict[str, Any]]] = {}

# ---------------------------------------------------------------------------
# Remote Control (HTTP-based input injection — no window focus needed)
# ---------------------------------------------------------------------------

_REMOTE_PORT_FILE = None  # set lazily
_remote_port_cache: Optional[int] = None


def _remote_port_file() -> Path:
    return output_dir() / "remote_port"


def _remote_control_port() -> Optional[int]:
    """Read the remote control port written by AIOServer, with caching."""
    global _remote_port_cache
    if _remote_port_cache is not None:
        return _remote_port_cache

    port_file = _remote_port_file()
    if not port_file.exists():
        return None
    try:
        port = int(port_file.read_text().strip())
        if 1 <= port <= 65535:
            _remote_port_cache = port
            return port
    except (ValueError, OSError):
        pass
    return None


def _remote_url() -> Optional[str]:
    """Return base URL for the remote control server, or None if unavailable."""
    port = _remote_control_port()
    if port is None:
        return None
    return f"http://127.0.0.1:{port}"


def _remote_available() -> bool:
    """Quick health check on the remote control server."""
    base = _remote_url()
    if not base:
        return False
    try:
        req = urllib.request.Request(f"{base}/health", method="GET")
        with urllib.request.urlopen(req, timeout=2) as resp:
            data = json.loads(resp.read())
            return data.get("ok", False)
    except Exception:
        return False


def _remote_post(path: str, payload: dict[str, Any]) -> dict[str, Any]:
    """POST JSON to the remote control server. Raises on failure."""
    base = _remote_url()
    if not base:
        raise ConnectionError("Remote control server not available")
    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        f"{base}{path}",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read())


def _remote_get(path: str) -> dict[str, Any]:
    """GET from the remote control server. Raises on failure."""
    base = _remote_url()
    if not base:
        raise ConnectionError("Remote control server not available")
    req = urllib.request.Request(f"{base}{path}", method="GET")
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read())


def _remote_send_key(key_name: str, event_type: str = "press") -> dict[str, Any]:
    """Send a key event via the remote control server."""
    return _remote_post("/input/key", {"key": key_name, "event": event_type})


def _remote_send_key_sequence(keys: list[str], delay_ms: int = 50) -> dict[str, Any]:
    """Send a key sequence via the remote control server."""
    return _remote_post("/input/key-sequence", {"keys": keys, "delay_ms": delay_ms})


def _remote_send_type(text: str) -> dict[str, Any]:
    """Type text via the remote control server."""
    return _remote_post("/input/type", {"text": text})


def _remote_send_click(x: int, y: int) -> dict[str, Any]:
    """Send a click at window-relative coordinates via the remote control server."""
    return _remote_post("/input/click", {"x": x, "y": y})


def _invalidate_remote_cache() -> None:
    """Clear the cached remote port (e.g. after killing the app)."""
    global _remote_port_cache
    _remote_port_cache = None


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
    # Only remove workspace-root .png.json sidecars whose image no longer exists.
    for prefix in _LEGACY_SCREENSHOT_PREFIXES:
        for metadata_path in screenshot_dir().glob(f"{prefix}*.png.json"):
            image_path = metadata_path.with_suffix("")  # strip .json → .png
            if not image_path.exists():
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
    # Write to internal metadata directory (for coordinate transforms).
    internal_path = _screenshot_metadata_path(image_path)
    payload = json.dumps(metadata, indent=2)
    internal_path.write_text(payload)
    # Also write sidecar next to the image so the vision extension finds it.
    sidecar_path = _legacy_workspace_metadata_path(image_path)
    try:
        sidecar_path.write_text(payload)
    except Exception:
        pass  # Non-critical — internal copy is the authoritative one.


def _read_screenshot_metadata(image_path: Path) -> Optional[dict[str, Any]]:
    for candidate in (_screenshot_metadata_path(image_path), _legacy_workspace_metadata_path(image_path)):
        if candidate.exists():
            try:
                return json.loads(candidate.read_text())
            except Exception:
                continue
    return None


def _ocr_cache_key(image_path: Path, psm: int) -> tuple[str, int, int, int]:
    stat = image_path.stat()
    return (str(image_path), int(psm), stat.st_mtime_ns, stat.st_size)


def _display_metrics() -> Optional[dict[str, float]]:
    """Return display dimensions in points and the true backing-store scale.

    On Retina/HiDPI Macs, CGDisplayPixelsWide returns the *logical* pixel
    count (equal to the point count), not the physical backing-store pixels.
    screencapture always writes at the backing-store resolution, so we must
    use CGDisplayModeGetPixelWidth to get the true physical width.
    """
    try:
        import Quartz

        display_id = Quartz.CGMainDisplayID()
        bounds = Quartz.CGDisplayBounds(display_id)
        point_width = float(bounds.size.width)
        point_height = float(bounds.size.height)

        # True backing-store pixels via the current display mode.
        backing_width = point_width
        backing_height = point_height
        try:
            mode = Quartz.CGDisplayCopyDisplayMode(display_id)
            if mode:
                pw = float(Quartz.CGDisplayModeGetPixelWidth(mode))
                ph = float(Quartz.CGDisplayModeGetPixelHeight(mode))
                if pw > 0 and ph > 0:
                    backing_width = pw
                    backing_height = ph
        except Exception:
            pass

        return {
            "display_width": point_width,
            "display_height": point_height,
            "display_scale_x": round(backing_width / max(point_width, 1.0), 4),
            "display_scale_y": round(backing_height / max(point_height, 1.0), 4),
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
    no_activate: bool = False,
) -> dict[str, Any]:
    if not aioserver_path().exists():
        _err("AIOServer not built. Run 'make build' first.")

    if rom_path and app_name:
        _err("Choose either a ROM or an app launch target, not both")

    _stop_existing_aioserver()
    _invalidate_remote_cache()

    cmd = [str(aioserver_path())]
    if rom_path:
        cmd += ["--rom", str(rom_path)]
    if app_name:
        cmd += ["--launch-app", app_name]
    if input_script_path:
        cmd += ["--input-script", str(input_script_path)]
    if no_activate:
        cmd += ["--no-activate"]

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
    _invalidate_remote_cache()


def _build_screenshot_info(
    image_path: Path,
    capture_mode: str,
    window_info: Optional[dict[str, Any]],
    *,
    display_metrics: Optional[dict[str, float]] = None,
    cleaned: Optional[list[str]] = None,
    extra: Optional[dict[str, Any]] = None,
) -> dict[str, Any]:
    """Build the metadata dict for a captured screenshot.

    Computes pixel stats, correct Retina scale factors, and blank detection.
    """
    info: dict[str, Any] = {
        "path": str(image_path),
        "size_bytes": image_path.stat().st_size,
        "capture_mode": capture_mode,
    }
    if extra:
        info.update(extra)
    if window_info:
        info["window"] = _window_bounds(window_info)
        info["window_id"] = window_info.get("id")
    dm = display_metrics or _display_metrics()
    if dm:
        info.update(dm)
    if cleaned:
        info["cleaned"] = cleaned
    try:
        from PIL import Image

        img = Image.open(image_path)
        info["width"] = img.width
        info["height"] = img.height

        # Compute correct scale factors from actual image dimensions.
        if window_info:
            info["window_scale_x"] = round(img.width / max(window_info["w"], 1), 4)
            info["window_scale_y"] = round(img.height / max(window_info["h"], 1), 4)
        if dm and capture_mode.startswith("full_screen"):
            dw = float(dm.get("display_width", 0))
            dh = float(dm.get("display_height", 0))
            if dw > 0 and dh > 0:
                info["display_scale_x"] = round(img.width / dw, 4)
                info["display_scale_y"] = round(img.height / dh, 4)

        # Pixel stats for blank detection.
        rgba = img.convert("RGBA")
        _getdata = getattr(rgba, "get_flattened_data", None) or rgba.getdata
        pixels = list(_getdata())
        total = len(pixels)
        if total > 0:
            transparent = sum(1 for _, _, _, alpha in pixels if alpha < 8)
            non_black = sum(
                1 for red, green, blue, alpha in pixels
                if alpha >= 8 and red + green + blue > 30
            )
            info["transparent_ratio"] = round(transparent / total, 4)
            info["nonblack_ratio"] = round(non_black / total, 4)
            info["looks_blank"] = (
                info["transparent_ratio"] > 0.95 or info["nonblack_ratio"] < 0.01
            )
    except Exception:
        pass
    return info


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

    info = _build_screenshot_info(out, mode, window_info, display_metrics=None, cleaned=cleaned)

    if info.get("looks_blank") and mode != "full_screen" and allow_fullscreen_fallback:
        subprocess.run(["screencapture", "-x", str(out)],
                       capture_output=True, timeout=10)
        if not out.exists() or out.stat().st_size == 0:
            _err("Screenshot capture failed")
        info = _build_screenshot_info(
            out, "full_screen_fallback_after_blank_window", window_info,
            display_metrics=None, cleaned=cleaned, extra={"window_capture_failed": True},
        )

    _write_screenshot_metadata(out, info)
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
    no_focus = getattr(args, "no_focus", False)
    launch = _launch_aioserver(rom, script_path, int(args.wait_ms), app_name=app_name, no_activate=no_focus)
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
    use_remote = _remote_available()

    bounds: Optional[dict[str, int]] = None
    if not use_remote and not args.no_focus:
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

        if use_remote:
            # For remote clicks, we need window-relative coordinates.
            # If the screenshot was window-only, image coords == window-relative.
            metadata = _read_screenshot_metadata(image_path) or {}
            capture_mode = str(metadata.get("capture_mode", ""))
            if capture_mode == "window":
                # Image coordinates are already window-relative (possibly scaled).
                from PIL import Image
                img = Image.open(image_path)
                win_bounds = _find_window_bounds()
                if win_bounds and img.width > 0 and img.height > 0:
                    scale_x = win_bounds["w"] / img.width
                    scale_y = win_bounds["h"] / img.height
                    wx = round(int(args.image_x) * scale_x)
                    wy = round(int(args.image_y) * scale_y)
                else:
                    wx, wy = int(args.image_x), int(args.image_y)
                result = _remote_send_click(wx, wy)
                _ok(method="remote", clicked_x=wx, clicked_y=wy, **{k: v for k, v in result.items() if k != "ok"})
                return
            else:
                # Full-screen capture or unknown — fall through to coordinate transform + cliclick
                use_remote = False
                if not bounds:
                    focused = _ensure_window_focus()
                    if focused:
                        bounds = _window_bounds(focused)

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

        if use_remote and args.window_relative:
            # Already window-relative — send directly
            result = _remote_send_click(x, y)
            _ok(method="remote", clicked_x=x, clicked_y=y, **{k: v for k, v in result.items() if k != "ok"})
            return

        if args.window_relative:
            if not bounds:
                bounds = _find_window_bounds()
            if not bounds:
                _err("AIOServer window not found for relative click")
                return
            x += bounds["x"]
            y += bounds["y"]

    subprocess.run(["cliclick", f"c:{x},{y}"], capture_output=True, timeout=5)
    _ok(method="cliclick", clicked_x=x, clicked_y=y, **click_meta)


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
    "space": "space", "tab": "tab", "shift": "shift",
    "home": "home",
    "start": "return",
    "select": "space",
    "a": "x", "b": "z",
    "l": "a", "r": "s",
    "z": "z", "x": "x", "c": "c", "v": "v",
}

_DEFAULT_USER_TEST_SNAP_KEY = "f9"
_DEFAULT_USER_TEST_ABORT_KEY = "f10"
_MAC_KEYCODE_MAP = {
    0: "a",
    1: "s",
    6: "z",
    7: "x",
    8: "c",
    9: "v",
    36: "enter",
    48: "tab",
    49: "space",
    53: "esc",
    56: "shift",
    60: "shift",
    101: "f9",
    109: "f10",
    115: "home",
    123: "left",
    124: "right",
    125: "down",
    126: "up",
}
_REPLAYABLE_USER_TEST_KEYS = {
    "a",
    "s",
    "z",
    "x",
    "c",
    "v",
    "enter",
    "tab",
    "space",
    "esc",
    "shift",
    "home",
    "left",
    "right",
    "down",
    "up",
}
_HOST_KEY_TO_GBA_SCRIPT_KEY = {
    "up": "UP",
    "down": "DOWN",
    "left": "LEFT",
    "right": "RIGHT",
    "z": "A",
    "x": "B",
    "shift": "SELECT",
    "enter": "START",
    "space": "START",
    "tab": "START",
    "a": "L",
    "s": "R",
}


def cmd_key(args: argparse.Namespace) -> None:
    """Send a key press — via remote control server (no focus needed) or cliclick fallback."""
    event_type = getattr(args, "event", "press")
    if _remote_available():
        result = _remote_send_key(args.name, event_type)
        _ok(method="remote", **result)
        return
    if not _ensure_window_focus():
        _err("AIOServer window not found or could not be focused for key input")
    key_result = _send_key_event(args.name, event_type, args.modifiers or "")
    _ok(method="cliclick", **key_result)


def cmd_key_sequence(args: argparse.Namespace) -> None:
    sequence = [item for item in args.keys.split() if item]
    if _remote_available():
        result = _remote_send_key_sequence(sequence)
        _ok(method="remote", **result)
        return
    if not _ensure_window_focus():
        _err("AIOServer window not found or could not be focused for key input")
    sent: list[str] = []
    for item in sequence:
        sent_event = _send_key_event(item, "press")
        sent.append(str(sent_event["mapped_to"]))
    _ok(method="cliclick", mapped_to=sent)


def cmd_type(args: argparse.Namespace) -> None:
    if _remote_available():
        result = _remote_send_type(args.text)
        _ok(method="remote", **result)
        return
    if not _ensure_window_focus():
        _err("AIOServer window not found or could not be focused for text input")
    subprocess.run(["cliclick", f"t:{args.text}"], capture_output=True, timeout=10)
    _ok(method="cliclick", typed=args.text)


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

    def _pixels_rgb(image: "Image.Image") -> list[tuple[int, ...]]:
        rgb = image.convert("RGB")
        _gd = getattr(rgb, "get_flattened_data", None) or rgb.getdata
        return list(_gd())

    if do_nonblack:
        pixels = _pixels_rgb(img)
        total = len(pixels)
        non_black = sum(1 for r, g, b in pixels if r + g + b > 30)
        ratio = non_black / total if total > 0 else 0
        results["nonblack_ratio"] = round(ratio, 4)
        results["is_black_screen"] = ratio < 0.01

    if do_colors:
        pixels = _pixels_rgb(img)
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
    remote_port = _remote_control_port()
    remote_ok = _remote_available() if remote_port else False
    _ok(
        pid=pid,
        running=running,
        window=_window_bounds(window_info),
        window_id=window_info.get("id") if window_info else None,
        remote_control_port=remote_port,
        remote_control_available=remote_ok,
    )


def cmd_kill(args: argparse.Namespace) -> None:
    _kill_aioserver()
    _ok(killed=True)


def cmd_poll_state(args: argparse.Namespace) -> None:
    """Poll application state from the remote control server."""
    endpoint = getattr(args, "endpoint", "state")
    path_map = {
        "state": "/state",
        "navigation": "/state/navigation",
        "input": "/state/input",
        "emulator": "/state/emulator",
    }
    path = path_map.get(endpoint, "/state")
    try:
        data = _remote_get(path)
        _ok(**data)
    except Exception as e:
        _err(f"Failed to poll state: {e}")


def cmd_clean(args: argparse.Namespace) -> None:
    """Remove all managed workspace screenshots, probes, and metadata."""
    deleted = _cleanup_workspace_screenshots(keep_path=None)
    deleted.extend(_cleanup_probe_capture())
    deleted.extend(_cleanup_internal_metadata())
    _ok(deleted=deleted, count=len(deleted))


def _normalize_user_test_key_name(name: Optional[str], default: str) -> str:
    normalized = (name or default).strip().lower()
    aliases = {
        "return": "enter",
        "escape": "esc",
    }
    return aliases.get(normalized, normalized)


def _user_test_root_dir(custom_root: Optional[str]) -> Path:
    if custom_root:
        root = Path(custom_root).expanduser().resolve()
    else:
        root = output_dir() / "user_tests"
    root.mkdir(parents=True, exist_ok=True)
    return root


def _user_test_artifact_dir(label: Optional[str], custom_root: Optional[str]) -> Path:
    root = _user_test_root_dir(custom_root)
    artifact_dir = root / f"{_sanitize_artifact_label(label or 'user-test')}_{ts()}"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    return artifact_dir


def _send_key_event(key_name: str, event_type: str, modifiers: str = "") -> dict[str, Any]:
    normalized_name = key_name.strip().lower()
    mapped = _KEY_MAP.get(normalized_name, normalized_name)
    normalized_event = event_type.strip().lower()

    if normalized_event == "press":
        if modifiers:
            subprocess.run(
                ["cliclick", f"kd:{modifiers}", f"kp:{mapped}", f"ku:{modifiers}"],
                capture_output=True,
                timeout=5,
            )
        else:
            subprocess.run(["cliclick", f"kp:{mapped}"], capture_output=True, timeout=5)
    elif normalized_event == "down":
        subprocess.run(["cliclick", f"kd:{mapped}"], capture_output=True, timeout=5)
    elif normalized_event == "up":
        subprocess.run(["cliclick", f"ku:{mapped}"], capture_output=True, timeout=5)
    else:
        raise ValueError(f"Unsupported key event: {event_type}")

    return {
        "key": normalized_name,
        "mapped_to": mapped,
        "event": normalized_event,
        "modifiers": modifiers,
    }


def _build_user_test_replay_plan(
    *,
    rom_path: Optional[Path],
    app_name: Optional[str],
    input_script_path: Optional[Path],
    wait_ms: int,
    events: list[dict[str, Any]],
    replay_capture_path: Path,
    capture_window_only: bool,
    label: str,
) -> list[dict[str, Any]]:
    plan: list[dict[str, Any]] = [
        {
            "action": "boot",
            "rom": str(rom_path) if rom_path else None,
            "app": app_name,
            "wait_ms": wait_ms,
            "input_script": str(input_script_path) if input_script_path else None,
        },
        {"action": "focus"},
    ]

    previous_ms = 0
    for event in events:
        current_ms = int(event["ms"])
        delay_ms = current_ms - previous_ms
        if delay_ms > 0:
            plan.append({"action": "wait", "ms": delay_ms})

        plan.append(
            {
                "action": "key",
                "name": str(event["name"]),
                "event": str(event["event"]),
                "focus": False,
            }
        )
        previous_ms = current_ms

    plan.append(
        {
            "action": "screenshot",
            "output": str(replay_capture_path),
            "window": capture_window_only,
            "full_screen": not capture_window_only,
            "label": f"{label}-replay",
        }
    )
    plan.append({"action": "kill"})
    return plan


def _build_gba_input_script(events: list[dict[str, Any]]) -> tuple[Optional[str], list[str]]:
    compatible_lines: list[str] = [
        "# Generated by scripts/visual_dev_loop.py user-test-create",
        "# Replay target: GBA-compatible emulator input only",
    ]
    unsupported_keys: set[str] = set()
    pressed_keys: set[str] = set()
    latest_ms = 0

    for event in events:
        name = str(event["name"])
        script_key = _HOST_KEY_TO_GBA_SCRIPT_KEY.get(name)
        if not script_key:
            unsupported_keys.add(name)
            continue

        event_ms = int(event["ms"])
        latest_ms = max(latest_ms, event_ms)
        action = "DOWN" if str(event["event"]) == "down" else "UP"
        compatible_lines.append(f"{event_ms} {script_key} {action}")
        if action == "DOWN":
            pressed_keys.add(script_key)
        else:
            pressed_keys.discard(script_key)

    if len(compatible_lines) == 2:
        return None, sorted(unsupported_keys)

    if pressed_keys:
        release_ms = latest_ms + 1
        for script_key in sorted(pressed_keys):
            compatible_lines.append(f"{release_ms} {script_key} UP")

    return "\n".join(compatible_lines) + "\n", sorted(unsupported_keys)


def _record_user_test_events(expected_pid: Optional[int], snap_key: str, abort_key: str) -> dict[str, Any]:
    try:
        import Quartz
    except Exception as exc:
        raise RuntimeError(f"Quartz bindings unavailable for user-test-create: {exc}") from exc

    state: dict[str, Any] = {
        "started_at": time.monotonic(),
        "events": [],
        "ignored_keys": set(),
        "snap_pressed": False,
        "aborted": False,
        "error": None,
    }

    event_mask = (1 << Quartz.kCGEventKeyDown) | (1 << Quartz.kCGEventKeyUp)

    def callback(_proxy: Any, event_type: Any, event: Any, _refcon: Any) -> Any:
        if event_type not in (Quartz.kCGEventKeyDown, Quartz.kCGEventKeyUp):
            return event

        keycode = int(
            Quartz.CGEventGetIntegerValueField(event, Quartz.kCGKeyboardEventKeycode)
        )
        key_name = _MAC_KEYCODE_MAP.get(keycode)
        if not key_name:
            return event

        if event_type == Quartz.kCGEventKeyDown:
            is_repeat = bool(
                Quartz.CGEventGetIntegerValueField(event, Quartz.kCGKeyboardEventAutorepeat)
            )
            if is_repeat:
                return event

        if key_name == snap_key and event_type == Quartz.kCGEventKeyDown:
            state["snap_pressed"] = True
            return event

        if key_name == abort_key and event_type == Quartz.kCGEventKeyDown:
            state["aborted"] = True
            return event

        if key_name not in _REPLAYABLE_USER_TEST_KEYS:
            cast(set[str], state["ignored_keys"]).add(key_name)
            return event

        event_ms = max(0, round((time.monotonic() - float(state["started_at"])) * 1000))
        cast(list[dict[str, Any]], state["events"]).append(
            {
                "ms": int(event_ms),
                "name": key_name,
                "event": "down" if event_type == Quartz.kCGEventKeyDown else "up",
            }
        )
        return event

    tap = Quartz.CGEventTapCreate(
        Quartz.kCGSessionEventTap,
        Quartz.kCGHeadInsertEventTap,
        Quartz.kCGEventTapOptionListenOnly,
        event_mask,
        callback,
        None,
    )
    if tap is None:
        raise RuntimeError(
            "Failed to create keyboard event tap. Grant Accessibility and Input Monitoring permissions, then retry."
        )

    run_loop_source = Quartz.CFMachPortCreateRunLoopSource(None, tap, 0)
    run_loop = Quartz.CFRunLoopGetCurrent()
    Quartz.CFRunLoopAddSource(run_loop, run_loop_source, Quartz.kCFRunLoopCommonModes)
    Quartz.CGEventTapEnable(tap, True)

    try:
        while not bool(state["snap_pressed"]) and not bool(state["aborted"]):
            Quartz.CFRunLoopRunInMode(Quartz.kCFRunLoopDefaultMode, 0.2, False)
            if expected_pid and not _is_running(expected_pid):
                state["error"] = "AIOServer exited unexpectedly during user-assisted capture"
                break
    finally:
        Quartz.CGEventTapEnable(tap, False)
        Quartz.CFRunLoopRemoveSource(run_loop, run_loop_source, Quartz.kCFRunLoopCommonModes)

    state["ignored_keys"] = sorted(cast(set[str], state["ignored_keys"]))
    return state


def cmd_user_test_create(args: argparse.Namespace) -> None:
    rom = _resolve_optional_path(args.rom, "ROM")
    input_script_path = _resolve_optional_path(args.input_script, "Input script")
    app_name = _normalize_launch_app(getattr(args, "app", None))
    wait_ms = int(args.wait_ms)
    snap_key = _normalize_user_test_key_name(args.snap_key, _DEFAULT_USER_TEST_SNAP_KEY)
    abort_key = _normalize_user_test_key_name(args.abort_key, _DEFAULT_USER_TEST_ABORT_KEY)
    label = getattr(args, "label", None) or app_name or (rom.stem if rom else "user-test")
    capture_window_only = not bool(args.full_screen)

    if snap_key == abort_key:
        _err("snap-key and abort-key must be different")

    artifact_dir = _user_test_artifact_dir(label, getattr(args, "out_dir", None))
    capture_path = artifact_dir / "captured_state.png"
    replay_capture_path = artifact_dir / "replay_capture.png"
    replay_plan_path = artifact_dir / "replay_session.json"
    metadata_path = artifact_dir / "metadata.json"
    gba_input_script_path = artifact_dir / "replay.input"

    launch: Optional[dict[str, Any]] = None
    recording: Optional[dict[str, Any]] = None
    screenshot: Optional[dict[str, Any]] = None

    try:
        launch = _launch_aioserver(rom, input_script_path, wait_ms, app_name=app_name)
        if not _ensure_window_focus():
            _err("AIOServer window not found or could not be focused for user-assisted capture")

        print(
            (
                f"[user-test-create] Control AIOServer now. Press {snap_key.upper()} to capture and finish, "
                f"or {abort_key.upper()} to abort. Only supported keyboard inputs are recorded."
            ),
            file=sys.stderr,
            flush=True,
        )

        recording = _record_user_test_events(launch["pid"], snap_key, abort_key)
        if recording.get("error"):
            _err(str(recording["error"]))

        if recording.get("aborted"):
            metadata = {
                "aborted": True,
                "rom": str(rom) if rom else None,
                "app": app_name,
                "snap_key": snap_key,
                "abort_key": abort_key,
                "recorded_events": recording.get("events", []),
                "ignored_keys": recording.get("ignored_keys", []),
            }
            metadata_path.write_text(json.dumps(metadata, indent=2))
            _ok(
                aborted=True,
                artifact_dir=str(artifact_dir),
                metadata=str(metadata_path),
                event_count=len(cast(list[dict[str, Any]], recording.get("events", []))),
                ignored_keys=recording.get("ignored_keys", []),
            )
            return

        screenshot = _capture_screenshot(
            str(capture_path),
            window_only=capture_window_only,
            allow_fullscreen_fallback=True,
            label=f"{label}-capture",
        )

        recorded_events = cast(list[dict[str, Any]], recording.get("events", []))
        replay_plan = _build_user_test_replay_plan(
            rom_path=rom,
            app_name=app_name,
            input_script_path=input_script_path,
            wait_ms=wait_ms,
            events=recorded_events,
            replay_capture_path=replay_capture_path,
            capture_window_only=capture_window_only,
            label=_sanitize_artifact_label(label),
        )
        replay_plan_path.write_text(json.dumps(replay_plan, indent=2))

        gba_input_script_text, unsupported_gba_keys = _build_gba_input_script(recorded_events)
        exported_gba_script = None
        if rom and gba_input_script_text:
            gba_input_script_path.write_text(gba_input_script_text)
            exported_gba_script = str(gba_input_script_path)

        metadata = {
            "aborted": False,
            "rom": str(rom) if rom else None,
            "app": app_name,
            "launch_log": launch["log"] if launch else None,
            "artifact_dir": str(artifact_dir),
            "snap_key": snap_key,
            "abort_key": abort_key,
            "recorded_events": recorded_events,
            "ignored_keys": recording.get("ignored_keys", []),
            "replay_plan": str(replay_plan_path),
            "replay_capture": str(replay_capture_path),
            "captured_state": screenshot["path"] if screenshot else None,
            "gba_input_script": exported_gba_script,
            "unsupported_gba_keys": unsupported_gba_keys,
        }
        metadata_path.write_text(json.dumps(metadata, indent=2))

        _ok(
            aborted=False,
            artifact_dir=str(artifact_dir),
            capture=screenshot,
            replay_plan=str(replay_plan_path),
            replay_capture=str(replay_capture_path),
            metadata=str(metadata_path),
            gba_input_script=exported_gba_script,
            unsupported_gba_keys=unsupported_gba_keys,
            event_count=len(recorded_events),
            ignored_keys=recording.get("ignored_keys", []),
            log=launch["log"] if launch else None,
        )
    finally:
        _kill_aioserver()


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

            elif action in ("key", "key_down", "key_up"):
                key_name = step.get("name", "")
                key_sequence = step.get("keys", "")
                use_remote = _remote_available()
                if not use_remote and step.get("focus", True) and not _ensure_window_focus():
                    step_result["error"] = "AIOServer window not found or could not be focused for key input"
                    results.append(step_result)
                    continue
                if key_sequence:
                    sequence = [item for item in key_sequence.split() if item]
                    if use_remote:
                        result = _remote_send_key_sequence(sequence)
                        step_result["method"] = "remote"
                        step_result["mapped_to"] = sequence
                    else:
                        sent: list[str] = []
                        for item in sequence:
                            sent_event = _send_key_event(str(item), "press")
                            sent.append(str(sent_event["mapped_to"]))
                        step_result["method"] = "cliclick"
                        step_result["mapped_to"] = sent
                elif not key_name:
                    step_result["error"] = "Missing 'name'"
                else:
                    default_event = "press"
                    if action == "key_down":
                        default_event = "down"
                    elif action == "key_up":
                        default_event = "up"
                    event_type = str(step.get("event", default_event))
                    if use_remote:
                        result = _remote_send_key(str(key_name), event_type)
                        step_result["method"] = "remote"
                        step_result.update({k: v for k, v in result.items() if k != "ok"})
                    else:
                        key_result = _send_key_event(
                            str(key_name),
                            event_type,
                            str(step.get("modifiers", "")),
                        )
                        step_result["method"] = "cliclick"
                        step_result.update(key_result)

            elif action == "key_sequence":
                sequence = [item for item in str(step.get("keys", "")).split() if item]
                use_remote = _remote_available()
                if use_remote:
                    result = _remote_send_key_sequence(sequence)
                    step_result["method"] = "remote"
                    step_result["mapped_to"] = sequence
                else:
                    if step.get("focus", True) and not _ensure_window_focus():
                        step_result["error"] = "AIOServer window not found or could not be focused for key input"
                        results.append(step_result)
                        continue
                    sent: list[str] = []
                    for item in sequence:
                        mapped_name = str(_KEY_MAP.get(item.lower(), item.lower()))
                        subprocess.run(["cliclick", f"kp:{mapped_name}"], capture_output=True, timeout=5)
                        sent.append(mapped_name)
                    step_result["method"] = "cliclick"
                    step_result["mapped_to"] = sent

            elif action == "click":
                use_remote = _remote_available()
                image_x = step.get("image_x")
                image_y = step.get("image_y")

                if use_remote:
                    # Remote: try to compute window-relative coordinates
                    if image_x is not None and image_y is not None:
                        image_path = _resolve_image_target(step.get("image"), last_screenshot)
                        if not image_path:
                            step_result["error"] = "No screenshot available for image-based click"
                            results.append(step_result)
                            continue
                        metadata = _read_screenshot_metadata(image_path) or {}
                        capture_mode = str(metadata.get("capture_mode", ""))
                        if capture_mode == "window":
                            from PIL import Image
                            img = Image.open(image_path)
                            win_bounds = _find_window_bounds()
                            if win_bounds and img.width > 0 and img.height > 0:
                                scale_x = win_bounds["w"] / img.width
                                scale_y = win_bounds["h"] / img.height
                                cx = round(int(image_x) * scale_x)
                                cy = round(int(image_y) * scale_y)
                            else:
                                cx, cy = int(image_x), int(image_y)
                            result = _remote_send_click(cx, cy)
                            step_result["method"] = "remote"
                            step_result["clicked"] = [cx, cy]
                        else:
                            # Full-screen capture — fall through to cliclick path
                            use_remote = False
                    else:
                        cx, cy = int(step.get("x", 0)), int(step.get("y", 0))
                        # For remote, x/y should be window-relative by default
                        result = _remote_send_click(cx, cy)
                        step_result["method"] = "remote"
                        step_result["clicked"] = [cx, cy]

                if not use_remote:
                    bounds = None
                    if step.get("focus", True):
                        focused = _ensure_window_focus()
                        if not focused:
                            step_result["error"] = "AIOServer window not found or could not be focused for click"
                            results.append(step_result)
                            continue
                        bounds = _window_bounds(focused)
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
                    step_result["method"] = "cliclick"
                    step_result["clicked"] = [cx, cy]

            elif action == "type":
                text = step.get("text", "")
                if _remote_available():
                    result = _remote_send_type(text)
                    step_result["method"] = "remote"
                    step_result["typed"] = text
                else:
                    if step.get("focus", True) and not _ensure_window_focus():
                        step_result["error"] = "AIOServer window not found or could not be focused for text input"
                        results.append(step_result)
                        continue
                    subprocess.run(["cliclick", f"t:{text}"], capture_output=True, timeout=10)
                    step_result["method"] = "cliclick"
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

                use_remote = _remote_available()
                if use_remote:
                    metadata = _read_screenshot_metadata(image_path) or {}
                    capture_mode = str(metadata.get("capture_mode", ""))
                    if capture_mode == "window":
                        win_bounds = _find_window_bounds()
                        if win_bounds and img.width > 0 and img.height > 0:
                            scale_x = win_bounds["w"] / img.width
                            scale_y = win_bounds["h"] / img.height
                            cx = round(image_x * scale_x)
                            cy = round(image_y * scale_y)
                        else:
                            cx, cy = image_x, image_y
                        result = _remote_send_click(cx, cy)
                        step_result["method"] = "remote"
                        step_result["clicked"] = [cx, cy]
                    else:
                        use_remote = False

                if not use_remote:
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
                    step_result["method"] = "cliclick"
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

    # Clean up only probe captures.
    # Do NOT purge managed workspace screenshots or their metadata — multi-step
    # sessions need intermediate captures for comparison and evidence, and
    # metadata sidecars are needed for coordinate transforms on saved images.
    cleanup_deleted = _cleanup_probe_capture()
    cleanup_deleted.extend(_cleanup_orphaned_metadata())
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
    p.add_argument("--no-focus", action="store_true", help="Launch without stealing focus from current window")

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
    p.add_argument("--event", default="press", help="press, down, or up")
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

    p = sub.add_parser("poll-state", help="Poll full application state from remote control server")
    p.add_argument("--endpoint", default="state",
                   choices=["state", "navigation", "input", "emulator"],
                   help="Which state endpoint to query")

    sub.add_parser("kill", help="Kill AIOServer")

    sub.add_parser("clean", help="Remove all managed screenshots, probes, and metadata")

    p = sub.add_parser("session", help="Execute a structured plan")
    p.add_argument("--plan", required=True, help="JSON array, '-' for stdin, or a path to a JSON plan file")

    p = sub.add_parser(
        "user-test-create",
        help="Launch AIOServer, record user keyboard input, snap a capture point, and write replay artifacts",
    )
    p.add_argument("--rom", default=None, help="ROM to load; omit for direct app launch")
    p.add_argument("--app", default=None, help="Streaming app to launch directly (youtube, netflix, disneyplus, hulu)")
    p.add_argument("--wait-ms", default="3000")
    p.add_argument("--input-script", help="Optional existing emulator input script to preload before manual takeover")
    p.add_argument("--label", help="Human-readable label for the artifact directory")
    p.add_argument("--snap-key", default=_DEFAULT_USER_TEST_SNAP_KEY, help="Key that captures the current state and finishes recording")
    p.add_argument("--abort-key", default=_DEFAULT_USER_TEST_ABORT_KEY, help="Key that aborts the capture session")
    p.add_argument("--full-screen", action="store_true", help="Capture the final snapshot from the full screen instead of the app window")
    p.add_argument("--out-dir", help="Optional root directory for generated user-test artifacts")

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
        "analyze": cmd_analyze, "status": cmd_status, "poll-state": cmd_poll_state,
        "kill": cmd_kill,
        "clean": cmd_clean,
        "session": cmd_session,
        "user-test-create": cmd_user_test_create,
    }
    dispatch[args.command](args)


if __name__ == "__main__":
    main()
