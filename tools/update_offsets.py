#!/usr/bin/env python3
"""Verify and refresh the CS2 gamedata used by CS2 Open World.

The plugin resolves engine details through ``gamedata/cs2-openworld.games.txt``:
``GetOffset("...")`` reads the ``Offsets`` block and ``GetSignature("...")`` the
``Signatures`` block. After a CS2 update those values must be refreshed, otherwise the
server either refuses to load (signature scan fails) or, worse, uses a stale member
offset and corrupts memory.

This tool is deliberately honest about what it can do automatically:

* ``--check``    validates the file, cross-checks it against the keys the sources
                 actually request, and exits non-zero when something is missing.
* ``--binary``   scans a real ``server.dll`` / ``libserver.so`` for every signature and
                 reports how many times each pattern matches (0 matches == broken).
* ``--fetch``    reads the public a2x/cs2-dumper artifacts to report the current CS2
                 build and to refresh the "Target CS2 build" marker.
* ``--set``      applies a value obtained elsewhere (member offsets inside
                 ``CGameResourceService``/``IGameServer`` are not exposed by any schema
                 dump, so they stay manual on purpose).
* ``--write``    applies the accumulated changes in place, keeping the layout and
                 comments intact and saving a ``.bak`` copy first.

Examples
--------
    tools/update_offsets.py --check
    tools/update_offsets.py --check --binary ~/cs2/game/bin/linuxsteamrt64/libserver.so
    tools/update_offsets.py --fetch
    tools/update_offsets.py --set ClientOffset:linux=584 --write
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_GAMEDATA = REPO_ROOT / "gamedata" / "cs2-openworld.games.txt"
SOURCE_ROOT = REPO_ROOT / "src"
BUILD_MARKER = "// Target CS2 build:"

DUMPER_BASE = "https://raw.githubusercontent.com/a2x/cs2-dumper/main/output"
PLATFORMS = ("windows", "linux")


@dataclass
class Entry:
    """One gamedata entry (a signature or an offset) with its source line numbers."""

    section: str
    key: str
    values: dict[str, str] = field(default_factory=dict)
    lines: dict[str, int] = field(default_factory=dict)

    def describe(self) -> str:
        return f"{self.section}.{self.key}"


class GamedataError(Exception):
    """Raised when the gamedata file cannot be parsed."""


def strip_comment(line: str) -> str:
    """Removes a ``//`` comment that is not inside a quoted string."""

    in_string = False
    index = 0
    while index < len(line):
        char = line[index]
        if char == '"':
            in_string = not in_string
        elif not in_string and line.startswith("//", index):
            return line[:index]
        index += 1
    return line


def parse_gamedata(path: Path) -> tuple[list[str], dict[str, Entry]]:
    """Parses the Valve KeyValues subset used by gamedata files.

    Returns the original lines and a mapping of ``"<Section>.<Key>"`` to :class:`Entry`.
    Only the shape this repository uses is supported (nested blocks, quoted names, one
    quoted value per line), which is exactly what is needed to edit files in place.
    """

    lines = path.read_text(encoding="utf-8").splitlines()
    stack: list[str] = []
    pending: str | None = None
    entries: dict[str, Entry] = {}

    for number, raw in enumerate(lines, start=1):
        text = strip_comment(raw).strip()
        if not text:
            continue
        if text == "{":
            if pending is None:
                raise GamedataError(f"line {number}: block without a name")
            stack.append(pending)
            pending = None
            continue
        if text == "}":
            if not stack:
                raise GamedataError(f"line {number}: unbalanced closing brace")
            stack.pop()
            continue

        pair = re.fullmatch(r'"([^"]*)"\s+"([^"]*)"', text)
        if pair:
            if len(stack) < 2:
                raise GamedataError(f"line {number}: value outside of a section")
            section, key = stack[-2], stack[-1]
            entry = entries.setdefault(f"{section}.{key}", Entry(section=section, key=key))
            entry.values[pair.group(1)] = pair.group(2)
            entry.lines[pair.group(1)] = number
            continue

        single = re.fullmatch(r'"([^"]*)"', text)
        if single:
            pending = single.group(1)
            continue

        raise GamedataError(f"line {number}: cannot parse {text!r}")

    if stack:
        raise GamedataError(f"unbalanced braces at end of file: {'.'.join(stack)}")
    return lines, entries


def referenced_keys() -> tuple[dict[str, set[str]], dict[str, set[str]]]:
    """Collects the gamedata keys that the C++ sources actually ask for."""

    offsets: dict[str, set[str]] = {}
    signatures: dict[str, set[str]] = {}
    pattern = re.compile(r'Get(Offset|Signature)\(\s*"([^"]+)"\s*\)')
    for path in sorted(SOURCE_ROOT.rglob("*")):
        if path.suffix not in (".h", ".hpp", ".cpp", ".c"):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for kind, key in pattern.findall(text):
            bucket = offsets if kind == "Offset" else signatures
            bucket.setdefault(key, set()).add(str(path.relative_to(REPO_ROOT)))
    return offsets, signatures


def signature_bytes(value: str) -> list[int | None]:
    """Converts a gamedata signature into bytes where ``\\x2A`` is a wildcard."""

    result: list[int | None] = []
    for token in re.findall(r"\\x([0-9A-Fa-f]{2})", value):
        byte = int(token, 16)
        result.append(None if byte == 0x2A else byte)
    return result

def validate(entries: dict[str, Entry], strict: bool = False) -> tuple[list[str], list[str]]:
    """Returns (errors, warnings) for the parsed gamedata.

    A key that the sources ask for but gamedata does not define degrades to a no-op call at
    runtime, so it is reported as a warning by default and as an error with ``strict``.
    """

    errors: list[str] = []
    warnings: list[str] = []
    required_offsets, required_signatures = referenced_keys()
    missing = errors if strict else warnings

    for key, users in sorted(required_offsets.items()):
        entry = entries.get(f"Offsets.{key}")
        if entry is None:
            missing.append(
                f"Offsets.{key} is used by {', '.join(sorted(users))} but not defined; "
                "the call degrades to a no-op until the value is added"
            )
            continue
        for platform in PLATFORMS:
            if platform not in entry.values:
                errors.append(f"Offsets.{key} has no {platform} value (used by {', '.join(sorted(users))})")

    for key, users in sorted(required_signatures.items()):
        entry = entries.get(f"Signatures.{key}")
        if entry is None:
            errors.append(f"Signatures.{key} is used by {', '.join(sorted(users))} but not defined")
            continue
        for platform in PLATFORMS:
            if platform not in entry.values:
                errors.append(f"Signatures.{key} has no {platform} value (used by {', '.join(sorted(users))})")

    for name, entry in sorted(entries.items()):
        platform_values = {key: value for key, value in entry.values.items() if key in PLATFORMS}
        if entry.section == "Offsets":
            for platform, value in platform_values.items():
                try:
                    number = int(value, 0)
                except ValueError:
                    errors.append(f"{name}.{platform} = {value!r} is not an integer")
                    continue
                if number < 0 or number > 0x100000:
                    errors.append(f"{name}.{platform} = {number} is outside the sane 0..0x100000 range")
            continue

        for platform, value in platform_values.items():
            pattern = signature_bytes(value)
            if not pattern:
                errors.append(f"{name}.{platform} contains no \\xNN bytes")
            elif all(byte is None for byte in pattern):
                errors.append(f"{name}.{platform} is entirely wildcards")
            elif len(pattern) < 8:
                warnings.append(f"{name}.{platform} is only {len(pattern)} bytes; short patterns match too often")

    return errors, warnings


def scan_binary(path: Path, pattern: list[int | None]) -> list[int]:
    """Returns the offsets where `pattern` occurs in `path` (wildcards match any byte)."""

    data = path.read_bytes()
    first_concrete = next((index for index, byte in enumerate(pattern) if byte is not None), None)
    if first_concrete is None or not pattern:
        return []

    needle = pattern[first_concrete]
    span = len(pattern)
    matches: list[int] = []
    start = first_concrete
    while True:
        position = data.find(bytes([needle]), start)
        if position < 0:
            return matches
        base = position - first_concrete
        if base >= 0 and base + span <= len(data):
            if all(byte is None or data[base + index] == byte for index, byte in enumerate(pattern)):
                matches.append(base)
        start = position + 1

def fetch_dumper_info() -> dict[str, object]:
    """Reads the public cs2-dumper artifacts: current build number and module offsets."""

    info: dict[str, object] = {}
    for name in ("offsets.json", "info.json"):
        url = f"{DUMPER_BASE}/{name}"
        try:
            with urllib.request.urlopen(url, timeout=20) as response:
                info[name] = json.loads(response.read().decode("utf-8"))
        except (urllib.error.URLError, json.JSONDecodeError, TimeoutError) as error:
            info[name] = {"error": str(error)}
    return info


def detect_build(info: dict[str, object]) -> int | None:
    offsets = info.get("offsets.json")
    if not isinstance(offsets, dict):
        return None
    engine2 = offsets.get("engine2.dll")
    if not isinstance(engine2, dict):
        return None
    build = engine2.get("dwBuildNumber")
    return build if isinstance(build, int) else None


def read_build_marker(lines: list[str]) -> int | None:
    for line in lines:
        if line.startswith(BUILD_MARKER):
            match = re.search(r"(\d+)", line)
            return int(match.group(1)) if match else None
    return None


def write_build_marker(lines: list[str], build: int) -> list[str]:
    marker = f"{BUILD_MARKER} {build} (source: a2x/cs2-dumper)"
    for index, line in enumerate(lines):
        if line.startswith(BUILD_MARKER):
            lines[index] = marker
            return lines
    lines.insert(0, marker)
    return lines


def apply_changes(lines: list[str], entries: dict[str, Entry], changes: dict[str, dict[str, str]]) -> list[str]:
    """Rewrites only the changed values, keeping indentation and comments untouched."""

    for entry_name, platforms in changes.items():
        entry = entries.get(entry_name)
        if entry is None:
            raise GamedataError(f"unknown gamedata entry {entry_name}")
        for platform, value in platforms.items():
            line_index = entry.lines.get(platform)
            if line_index is None:
                raise GamedataError(f"{entry_name} has no {platform} line to update")
            original = lines[line_index - 1]
            pattern = re.compile(r'("' + re.escape(platform) + r'")(\s+)"([^"]*)"')
            if not pattern.search(original):
                raise GamedataError(f"cannot rewrite line {line_index}: {original!r}")
            lines[line_index - 1] = pattern.sub(
                lambda match: f'{match.group(1)}{match.group(2)}"{value}"', original, count=1
            )
    return lines


def parse_set_arguments(values: list[str]) -> dict[str, dict[str, str]]:
    """Parses ``Key:platform=value`` pairs into the change map used by apply_changes."""

    changes: dict[str, dict[str, str]] = {}
    for raw in values:
        try:
            target, value = raw.split("=", 1)
            key, platform = target.split(":", 1)
        except ValueError:
            raise SystemExit(f"--set expects Key:platform=value, got {raw!r}")
        if platform not in PLATFORMS:
            raise SystemExit(f"--set platform must be one of {', '.join(PLATFORMS)}, got {platform!r}")
        section = "Signatures" if "\\x" in value else "Offsets"
        changes.setdefault(f"{section}.{key}", {})[platform] = value
    return changes

def print_inventory(entries: dict[str, Entry]) -> None:
    print("Gamedata inventory")
    print("-" * 62)
    for name, entry in sorted(entries.items()):
        values = "  ".join(f"{platform}={entry.values.get(platform, '?')}" for platform in PLATFORMS)
        if entry.section == "Signatures":
            sizes = "  ".join(
                f"{platform}:{len(signature_bytes(entry.values.get(platform, '')))}b" for platform in PLATFORMS
            )
            print(f"  {name:<34} {sizes}")
        else:
            print(f"  {name:<34} {values}")
    print()


def detect_binary_platform(binary: Path) -> str | None:
    """Guesses the gamedata platform group from the file header."""

    with binary.open("rb") as handle:
        head = handle.read(4)
    if head[:2] == b"MZ":
        return "windows"
    if head == b"\x7fELF":
        return "linux"
    return None


def report_signatures(binary: Path, entries: dict[str, Entry]) -> list[str]:
    errors: list[str] = []
    platform_hint = detect_binary_platform(binary)
    scanned = (platform_hint,) if platform_hint else PLATFORMS
    print(f"Signature scan: {binary} ({platform_hint or 'unknown'} binary)")
    print("-" * 62)
    for name, entry in sorted(entries.items()):
        if entry.section != "Signatures":
            continue
        for platform in scanned:
            value = entry.values.get(platform)
            if not value:
                continue
            matches = scan_binary(binary, signature_bytes(value))
            if not matches:
                errors.append(f"{name}.{platform} does not match {binary.name}; the plugin cannot bootstrap")
                print(f"  FAIL {name:<28} {platform:<8} 0 matches")
            elif len(matches) > 1:
                print(
                    f"  WARN {name:<28} {platform:<8} {len(matches)} matches, first at 0x{matches[0]:X}"
                )
            else:
                print(f"  ok   {name:<28} {platform:<8} 1 match at 0x{matches[0]:X}")
    print()
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify and refresh gamedata/cs2-openworld.games.txt",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--gamedata", type=Path, default=DEFAULT_GAMEDATA, help="path to the gamedata file")
    parser.add_argument("--check", action="store_true", help="validate only; this is also the default")
    parser.add_argument("--strict", action="store_true", help="treat keys missing for the sources as errors")
    parser.add_argument("--binary", type=Path, help="scan this server binary (server.dll / libserver.so)")
    parser.add_argument("--fetch", action="store_true", help="report the current CS2 build from a2x/cs2-dumper")
    parser.add_argument("--set", action="append", default=[], metavar="KEY:PLATFORM=VALUE", help="apply a value")
    parser.add_argument("--write", action="store_true", help="write the accumulated changes back to disk")
    parser.add_argument("--quiet", action="store_true", help="only print problems")
    args = parser.parse_args(argv)

    if not args.gamedata.is_file():
        print(f"error: gamedata file not found: {args.gamedata}", file=sys.stderr)
        return 2

    try:
        lines, entries = parse_gamedata(args.gamedata)
    except GamedataError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    errors, warnings = validate(entries, strict=args.strict)
    if not args.quiet:
        print_inventory(entries)

    if args.binary:
        if not args.binary.is_file():
            print(f"error: binary not found: {args.binary}", file=sys.stderr)
            return 2
        errors.extend(report_signatures(args.binary, entries))

    changes = parse_set_arguments(args.set)
    if changes:
        for entry_name, platforms in sorted(changes.items()):
            current = entries.get(entry_name)
            for platform, value in sorted(platforms.items()):
                old = current.values.get(platform, "<missing>") if current else "<missing>"
                print(f"  change {entry_name}.{platform}: {old} -> {value}")

    if args.write:
        if not changes:
            print("nothing to write; pass --set values first", file=sys.stderr)
            return 1
        updated = apply_changes(list(lines), entries, changes)
        if args.fetch:
            info = fetch_dumper_info()
            build = detect_build(info)
            if build is not None and build != read_build_marker(updated):
                updated = write_build_marker(updated, build)
                print(f"  change {BUILD_MARKER} {build}")
        backup = args.gamedata.with_suffix(args.gamedata.suffix + ".bak")
        shutil.copy2(args.gamedata, backup)
        args.gamedata.write_text("\n".join(updated) + "\n", encoding="utf-8")
        print(f"\nwrote {args.gamedata} (backup: {backup.name})")
    elif args.fetch:
        info = fetch_dumper_info()
        build = detect_build(info)
        marker = read_build_marker(lines)
        if build is None:
            print("CS2 build: unknown (cs2-dumper artifacts unavailable)")
        else:
            state = "up to date" if build == marker else f"gamedata targets build {marker}"
            print(f"CS2 build: {build} ({state})")
            if build != marker:
                print(f"  run with --fetch --write and a --set value to refresh the marker")

    for warning in warnings:
        print(f"warning: {warning}")
    for error in errors:
        print(f"error: {error}", file=sys.stderr)

    if errors:
        print(f"\n{len(errors)} problem(s) found", file=sys.stderr)
        return 1
    print("\ngamedata is consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
