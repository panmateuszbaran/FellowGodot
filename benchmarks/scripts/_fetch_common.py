"""Shared helpers for FellowEngine benchmark asset acquisition.

Stdlib only (runs with any Python >= 3.8, including the portable venv in .tools/).
Large assets are never committed to git: archives are cached in
benchmarks/assets/_downloads/ and extracted into benchmarks/project/external/,
both of which are git-ignored.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zipfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DOWNLOAD_DIR = os.path.join(REPO_ROOT, "benchmarks", "assets", "_downloads")
EXTERNAL_DIR = os.path.join(REPO_ROOT, "benchmarks", "project", "external")

USER_AGENT = "FellowEngine-benchmark-fetch/1.0 (+https://github.com/godotengine/godot)"


def log(msg: str) -> None:
    print(f"[fetch {datetime.datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)


def _resolve(url: str) -> tuple[str, int]:
    req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.geturl(), int(r.headers.get("Content-Length", "0"))


def _download_curl(url: str, dest: str, size: int | None) -> str:
    """Fallback using the system curl (Windows curl.exe uses the OS certificate store / schannel)."""
    curl = shutil.which("curl")
    if not curl:
        raise RuntimeError("urllib TLS verification failed and no curl executable is available")
    part = dest + ".part"
    log(f"using system curl for {os.path.basename(dest)}")
    subprocess.run([curl, "-L", "--fail", "--retry", "5", "-C", "-", "-sS", "-o", part, url], check=True)
    if size and os.path.getsize(part) != size:
        raise RuntimeError(f"incomplete download: {os.path.getsize(part)} != {size}")
    os.replace(part, dest)
    return dest


def download(url: str, dest_name: str, expected_size: int | None = None) -> str:
    """Download url into DOWNLOAD_DIR/dest_name with resume support. Returns path."""
    os.makedirs(DOWNLOAD_DIR, exist_ok=True)
    dest = os.path.join(DOWNLOAD_DIR, dest_name)
    if os.path.exists(dest) and expected_size and os.path.getsize(dest) == expected_size:
        log(f"cached: {dest_name} ({expected_size / 1e9:.2f} GB)")
        return dest
    try:
        final_url, size = _resolve(url)
    except urllib.error.URLError as e:
        if "CERTIFICATE_VERIFY_FAILED" not in str(e):
            raise
        log(f"urllib TLS verification failed ({e.reason}); falling back to system curl")
        return _download_curl(url, dest, expected_size)
    if expected_size and size and size != expected_size:
        log(f"WARNING: server reports {size} bytes, expected {expected_size} (asset may have been updated upstream)")
    if os.path.exists(dest) and size and os.path.getsize(dest) == size:
        log(f"cached: {dest_name} ({size / 1e9:.2f} GB)")
        return dest
    part = dest + ".part"
    have = os.path.getsize(part) if os.path.exists(part) else 0
    headers = {"User-Agent": USER_AGENT}
    if have:
        headers["Range"] = f"bytes={have}-"
        log(f"resuming {dest_name} at {have / 1e9:.2f} GB")
    req = urllib.request.Request(final_url, headers=headers)
    t0 = time.time()
    last = t0
    with urllib.request.urlopen(req, timeout=120) as r, open(part, "ab" if have else "wb") as f:
        if have and r.status != 206:
            f.seek(0)
            f.truncate()
            have = 0
        done = have
        while True:
            chunk = r.read(4 * 1024 * 1024)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            now = time.time()
            if now - last > 10:
                rate = (done - have) / max(now - t0, 1e-6) / 1e6
                log(f"{dest_name}: {done / 1e9:.2f}/{size / 1e9:.2f} GB ({rate:.1f} MB/s)")
                last = now
    if size and os.path.getsize(part) != size:
        raise RuntimeError(f"incomplete download of {dest_name}: {os.path.getsize(part)} != {size}")
    os.replace(part, dest)
    log(f"downloaded {dest_name} in {time.time() - t0:.0f} s")
    return dest


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def extract(zip_path: str, out_dir: str, predicate, strip_prefix: str = "") -> list[str]:
    """Extract members for which predicate(name) is True. Returns extracted relative names."""
    os.makedirs(out_dir, exist_ok=True)
    extracted = []
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            name = info.filename
            if name.endswith("/") or not predicate(name):
                continue
            rel = name[len(strip_prefix):] if strip_prefix and name.startswith(strip_prefix) else name
            target = os.path.join(out_dir, rel)
            if os.path.exists(target) and os.path.getsize(target) == info.file_size:
                extracted.append(rel)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with z.open(info) as src, open(target, "wb") as dst:
                shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
            extracted.append(rel)
    return extracted


def write_manifest(out_dir: str, manifest: dict) -> None:
    manifest = dict(manifest)
    manifest["fetched_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    manifest["python"] = sys.version.split()[0]
    with open(os.path.join(out_dir, "SOURCE.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
