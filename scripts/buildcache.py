#!/usr/bin/env python3
"""A build cache shared by every checkout and worktree (bin/zx-make uses it).

A project's build is keyed by the content of what goes into it: the
project's files (not build/, screenshots/, docs, test.py, arcade.json),
the whole SDK (not its host tools or docs), bin/zx-make, and the toolchain
image's id. A plain `zx-make -C <project>` with the same key copies the
outputs from the cache instead of building: the files directly in build/
(.tap, .nex, .sna, .map, .bin...). Entries are written to a temporary
directory and renamed into place, so concurrent builds are safe; the newest
KEEP entries of each project are kept.

    buildcache.py restore <project_dir> <toolchain_id>   exit 0 + key on stdout: restored
                                                         exit 1 + key on stdout: build it
    buildcache.py store <project_dir> <key>              after a successful build
    buildcache.py hash-file <file>                       sha256 (arcade snapshots, launches)

The cache: $ZX_BUILD_CACHE, default <repo's parent>/.spectrum_dev-buildcache;
ZX_BUILD_CACHE=off turns it off.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEEP = 8
SKIP_DIRS = {"build", "screenshots", "__pycache__", ".git"}
SKIP_FILES = {"test.py", "arcade.json", ".DS_Store"}
SKIP_SUFFIX = {".md", ".gif"}


def cache_dir() -> Path | None:
    c = os.environ.get("ZX_BUILD_CACHE", "")
    if c == "off":
        return None
    return Path(c) if c else ROOT.parent / ".spectrum_dev-buildcache"


def _files(top: Path, skip_dirs=SKIP_DIRS):
    for d, dirs, files in os.walk(top):
        dirs[:] = sorted(x for x in dirs if x not in skip_dirs)
        for f in sorted(files):
            if f in SKIP_FILES or Path(f).suffix in SKIP_SUFFIX:
                continue
            yield Path(d) / f


def key(project: Path, toolchain: str) -> str:
    h = hashlib.sha256()
    h.update(b"zx-buildcache 1\0" + toolchain.encode() + b"\0")
    for base, top, skip in ((project, project, SKIP_DIRS),
                            (ROOT, ROOT / "sdk", SKIP_DIRS | {"tools", "template"})):
        for p in _files(top, skip):
            h.update(str(p.relative_to(base)).encode() + b"\0")
            h.update(p.read_bytes())
            h.update(b"\0")
    h.update((ROOT / "bin" / "zx-make").read_bytes())
    return h.hexdigest()[:32]


def hash_file(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()[:32]


def restore(project: Path, k: str) -> bool:
    c = cache_dir()
    entry = c / project.name / k if c else None
    if not entry or not (entry / "meta.json").exists():
        return False
    build = project / "build"
    build.mkdir(exist_ok=True)
    for f in entry.iterdir():
        if f.name != "meta.json":
            tmp = build / f".{f.name}.{os.getpid()}"
            shutil.copyfile(f, tmp)
            tmp.replace(build / f.name)             # atomic for a reader of build/
    os.utime(entry)                                 # recently used: kept longer
    return True


def store(project: Path, k: str):
    c = cache_dir()
    if not c:
        return
    proj_dir = c / project.name
    final = proj_dir / k
    if (final / "meta.json").exists():
        return
    proj_dir.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix=f".{k}.", dir=proj_dir))
    try:
        outs = [f for f in (project / "build").iterdir() if f.is_file() and not f.name.startswith(".")]
        for f in outs:
            shutil.copyfile(f, tmp / f.name)
        (tmp / "meta.json").write_text(json.dumps({"project": project.name, "files": [f.name for f in outs],
                                                   "built_in": str(ROOT), "time": time.time()}))
        try:
            tmp.rename(final)                       # atomic; someone else may have won
        except OSError:
            shutil.rmtree(tmp, ignore_errors=True)
    except Exception:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    entries = sorted((d for d in proj_dir.iterdir() if d.is_dir() and not d.name.startswith(".")),
                     key=lambda d: d.stat().st_mtime, reverse=True)
    for old in entries[KEEP:]:
        shutil.rmtree(old, ignore_errors=True)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "hash-file":
        print(hash_file(Path(argv[2])))
        return 0
    project = Path(argv[2]).resolve()
    if cmd == "restore":
        k = key(project, argv[3] if len(argv) > 3 else "")
        print(k)
        return 0 if restore(project, k) else 1
    if cmd == "store":
        store(project, argv[3])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
