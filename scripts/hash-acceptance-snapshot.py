#!/usr/bin/env python3
"""Hash a dirty Markoff input tree and installed dependency/package prefixes."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess


def add_entry(digest: "hashlib._Hash", name: str, path: Path) -> int:
    name_bytes = name.encode("utf-8")
    is_link = path.is_symlink()
    payload = str(path.readlink()).encode("utf-8") if is_link else path.read_bytes()
    digest.update(len(name_bytes).to_bytes(8, "big"))
    digest.update(name_bytes)
    digest.update(b"L" if is_link else b"F")
    digest.update(len(payload).to_bytes(8, "big"))
    digest.update(hashlib.sha256(payload).digest())
    return len(payload)


def hash_worktree(root: Path, excluded: list[str]) -> tuple[str, int, int]:
    raw_paths = subprocess.check_output(
        ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    ).split(b"\0")
    paths = []
    for raw in raw_paths:
        if not raw:
            continue
        name = raw.decode("utf-8")
        if any(name == prefix or name.startswith(prefix.rstrip("/") + "/")
               for prefix in excluded):
            continue
        path = root / name
        # Git submodules appear as gitlinks and are fingerprinted separately
        # by recording their exact commit IDs in the acceptance report.
        if path.is_file() or path.is_symlink():
            paths.append((name, path))
    paths.sort(key=lambda item: item[0].encode("utf-8"))
    digest = hashlib.sha256()
    total = sum(add_entry(digest, name, path) for name, path in paths)
    return digest.hexdigest(), len(paths), total


def hash_tree(root: Path) -> tuple[str, int, int]:
    paths = sorted(
        (path for path in root.rglob("*") if path.is_file() or path.is_symlink()),
        key=lambda path: path.relative_to(root).as_posix().encode("utf-8"),
    )
    digest = hashlib.sha256()
    total = sum(add_entry(digest, path.relative_to(root).as_posix(), path)
                for path in paths)
    return digest.hexdigest(), len(paths), total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worktree", type=Path, default=Path.cwd())
    parser.add_argument("--exclude", action="append", default=[])
    parser.add_argument("--tree", action="append", default=[], metavar="NAME=PATH")
    args = parser.parse_args()

    worktree = args.worktree.resolve()
    digest, count, size = hash_worktree(worktree, args.exclude)
    print(f"worktree sha256={digest} files={count} bytes={size}")
    for item in args.tree:
        name, separator, raw_path = item.partition("=")
        if not separator or not name or not raw_path:
            parser.error(f"invalid --tree value: {item!r}; expected NAME=PATH")
        path = Path(raw_path).resolve()
        if not path.is_dir():
            parser.error(f"tree path is not a directory: {path}")
        digest, count, size = hash_tree(path)
        print(f"{name} sha256={digest} files={count} bytes={size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
