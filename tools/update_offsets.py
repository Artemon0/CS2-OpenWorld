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
* ``--refresh-online`` downloads the CS2 SDK, CS2Fixes, and CS2KZ data over HTTPS,
                       previews exact-name matches, and preserves unmatched values.
* ``--set``      applies one explicitly supplied value when trusted sources do not publish it.
* ``--write``    applies the accumulated changes in place, keeping the layout and
                 comments intact, with an atomic replacement and a ``.bak`` copy.

Examples
--------
    tools/update_offsets.py --check
    tools/update_offsets.py --check --binary ~/cs2/game/bin/linuxsteamrt64/libserver.so
    tools/update_offsets.py --fetch
    tools/update_offsets.py --refresh-online
    tools/update_offsets.py --set ClientOffset:linux=584 --write
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_GAMEDATA = REPO_ROOT / "gamedata" / "cs2-openworld.games.txt"
SOURCE_ROOT = REPO_ROOT / "src"
BUILD_MARKER = "// Target CS2 build:"

DUMPER_BASE = "https://raw.githubusercontent.com/a2x/cs2-dumper/main/output"
UPSTREAMS = {
    "CS2 SDK": "https://cs2-sdk.com/api/patterns",
    "CS2Fixes": "https://raw.githubusercontent.com/Source2ZE/CS2Fixes/main/gamedata/cs2fixes.jsonc",
    "CS2KZ": (
        "https://raw.githubusercontent.com/KZGlobalTeam/cs2kz-metamod/"
        "master/gamedata/cs2kz-core.games.txt"
    ),
}
PLATFORMS = ("windows", "linux")
SIGNATURE_PATTERN = re.compile(r"(?:\\x[0-9A-Fa-f]{2})+")
MAX_UPSTREAM_SIZE = 5 * 1024 * 1024
KEYVALUES_TOKEN = re.compile(
    r'"((?:[^"\\]|\\.)*)"|//[^\r\n]*|/\*.*?\*/|([{}])|([^\s{}"]+)', re.DOTALL
)


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
            if not SIGNATURE_PATTERN.fullmatch(value):
                errors.append(f"{name}.{platform} is not a sequence of \\xNN bytes")
                continue
            pattern = signature_bytes(value)
            if not pattern:
                errors.append(f"{name}.{platform} contains no \\xNN bytes")
            elif all(byte is None for byte in pattern):
                errors.append(f"{name}.{platform} is entirely wildcards")
            elif len(pattern) < 8:
                warnings.append(f"{name}.{platform} is only {len(pattern)} bytes; short patterns match too often")

    return errors, warnings


def fetch_upstream(url: str, timeout: float) -> str:
    """Fetches a pinned HTTPS upstream without following redirects to another host."""

    request = urllib.request.Request(
        url, headers={"User-Agent": "CS2-OpenWorld-gamedata-updater/1.0"}
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            original_host = urllib.parse.urlsplit(url).hostname
            final_url = urllib.parse.urlsplit(response.geturl())
            if final_url.scheme != "https" or final_url.hostname != original_host:
                raise GamedataError(f"unexpected redirect while fetching {url}")
            payload = response.read(MAX_UPSTREAM_SIZE + 1)
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        raise GamedataError(f"cannot download {url}: {error}") from error
    if len(payload) > MAX_UPSTREAM_SIZE:
        raise GamedataError(f"upstream response exceeds {MAX_UPSTREAM_SIZE} bytes: {url}")
    try:
        return payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise GamedataError(f"upstream response is not UTF-8: {url}") from error


def parse_jsonc(text: str, source: str) -> dict[str, object]:
    """Parses JSONC while preserving comment-like text inside quoted strings."""

    without_comments: list[str] = []
    in_string = False
    escaped = False
    index = 0
    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""
        if in_string:
            without_comments.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
        elif char == '"':
            in_string = True
            without_comments.append(char)
            index += 1
        elif char == "/" and next_char == "/":
            while index < len(text) and text[index] not in "\r\n":
                index += 1
        elif char == "/" and next_char == "*":
            index += 2
            while index + 1 < len(text) and text[index:index + 2] != "*/":
                if text[index] in "\r\n":
                    without_comments.append(text[index])
                index += 1
            if index + 1 >= len(text):
                raise GamedataError(f"unterminated JSONC comment in {source}")
            index += 2
        else:
            without_comments.append(char)
            index += 1

    no_trailing_commas: list[str] = []
    in_string = False
    escaped = False
    text_without_comments = "".join(without_comments)
    index = 0
    while index < len(text_without_comments):
        char = text_without_comments[index]
        if in_string:
            no_trailing_commas.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
        elif char == '"':
            in_string = True
            no_trailing_commas.append(char)
        elif char == ",":
            next_index = index + 1
            while next_index < len(text_without_comments) and text_without_comments[next_index].isspace():
                next_index += 1
            if next_index == len(text_without_comments) or text_without_comments[next_index] not in "}]":
                no_trailing_commas.append(char)
        else:
            no_trailing_commas.append(char)
        index += 1

    try:
        result = json.loads("".join(no_trailing_commas))
    except json.JSONDecodeError as error:
        raise GamedataError(f"invalid JSONC from {source}: {error}") from error
    if not isinstance(result, dict):
        raise GamedataError(f"JSONC root from {source} must be an object")
    return result


def parse_keyvalues(text: str, source: str) -> dict[str, object]:
    """Parses the KeyValues structure used by CS2KZ gamedata."""

    tokens: list[str] = []
    for match in KEYVALUES_TOKEN.finditer(text):
        quoted, brace, unquoted = match.groups()
        if quoted is not None:
            tokens.append(quoted.replace(r'\"', '"'))
        elif brace is not None:
            tokens.append(brace)
        elif unquoted is not None and not unquoted.startswith(("//", "/*")):
            tokens.append(unquoted)

    def parse_object(position: int, nested: bool = False) -> tuple[dict[str, object], int]:
        result: dict[str, object] = {}
        while position < len(tokens):
            key = tokens[position]
            if key == "}":
                if not nested:
                    raise GamedataError(f"unexpected closing brace in {source}")
                return result, position + 1
            if key == "{":
                raise GamedataError(f"expected a key before an object in {source}")
            position += 1
            if position >= len(tokens):
                raise GamedataError(f"missing value for {key} in {source}")
            if tokens[position] == "{":
                value, position = parse_object(position + 1, nested=True)
            elif tokens[position] == "}":
                raise GamedataError(f"missing value for {key} in {source}")
            else:
                value = tokens[position]
                position += 1
            if key in result:
                raise GamedataError(f"duplicate key {key!r} in {source}")
            result[key] = value
        if nested:
            raise GamedataError(f"unterminated object in {source}")
        return result, position

    result, _ = parse_object(0)
    return result


def get_casefold(mapping: object, key: str, source: str) -> object | None:
    if not isinstance(mapping, dict):
        return None
    matches = [value for name, value in mapping.items() if isinstance(name, str) and name.casefold() == key.casefold()]
    if len(matches) > 1:
        raise GamedataError(f"ambiguous {key} entry in {source}")
    return matches[0] if matches else None


def encode_source_pattern(value: object, name: str, source: str) -> str:
    if not isinstance(value, str):
        raise GamedataError(f"{name} signature from {source} is not a string")
    tokens = value.split()
    if not tokens or any(not re.fullmatch(r"(?:[0-9A-Fa-f]{2}|\?{1,2})", token) for token in tokens):
        raise GamedataError(f"{name} signature from {source} contains an invalid pattern")
    encoded = "".join(
        "\\x2A" if token in {"?", "??"} else f"\\x{int(token, 16):02X}" for token in tokens
    )
    pattern = signature_bytes(encoded)
    if len(pattern) < 8 or sum(byte is not None for byte in pattern) < 6:
        raise GamedataError(f"{name} signature from {source} is too short or too wildcarded")
    return encoded


def source_entry(block: object, name: str, source: str) -> dict[str, object] | None:
    entry = get_casefold(block, name, source)
    if entry is None:
        return None
    if not isinstance(entry, dict):
        raise GamedataError(f"{name} entry from {source} must be an object")
    return entry


def check_source_library(entry: dict[str, object], library: str, name: str, source: str) -> None:
    source_library = entry.get("library", library)
    if not isinstance(source_library, str) or source_library.casefold() != library.casefold():
        raise GamedataError(f"{name} uses a different library in {source}")


def select_offset(
    fixes_offsets: dict[str, object],
    kz_offsets: dict[str, object],
    name: str,
    platform: str,
) -> tuple[str, str] | None:
    fixes_entry = source_entry(fixes_offsets, name, "CS2Fixes")
    kz_entry = source_entry(kz_offsets, name, "CS2KZ")
    for source, upstream_entry in (("CS2Fixes", fixes_entry), ("CS2KZ", kz_entry)):
        if upstream_entry is None or platform not in upstream_entry:
            continue
        raw_value = upstream_entry[platform]
        try:
            number = int(raw_value, 0) if isinstance(raw_value, str) else raw_value
        except ValueError as error:
            raise GamedataError(f"Offsets.{name}.{platform} from {source} is not an integer") from error
        if isinstance(number, bool) or not isinstance(number, int) or number < 0 or number > 0x100000:
            raise GamedataError(f"Offsets.{name}.{platform} from {source} is out of range")
        return str(number), source
    return None


def load_online_changes(
    entries: dict[str, Entry], timeout: float
) -> tuple[dict[str, dict[str, str]], list[str], dict[str, dict[str, str]]]:
    """Fetches trusted gamedata sources and selects exact-name platform values."""

    source_text = {name: fetch_upstream(url, timeout) for name, url in UPSTREAMS.items()}
    try:
        sdk_data = json.loads(source_text["CS2 SDK"])
    except json.JSONDecodeError as error:
        raise GamedataError(f"invalid JSON from CS2 SDK: {error}") from error
    if not isinstance(sdk_data, dict) or not isinstance(sdk_data.get("patterns"), list):
        raise GamedataError("CS2 SDK response does not contain a patterns list")
    fixes_data = parse_jsonc(source_text["CS2Fixes"], UPSTREAMS["CS2Fixes"])
    kz_data = parse_keyvalues(source_text["CS2KZ"], UPSTREAMS["CS2KZ"])
    kz_games = get_casefold(kz_data, "Games", UPSTREAMS["CS2KZ"])
    kz_game = get_casefold(kz_games, "csgo", UPSTREAMS["CS2KZ"])
    if not isinstance(kz_game, dict):
        raise GamedataError("CS2KZ gamedata does not contain a csgo section")

    fixes_signatures = fixes_data.get("Signatures")
    fixes_offsets = fixes_data.get("Offsets")
    kz_signatures = kz_game.get("Signatures")
    kz_offsets = kz_game.get("Offsets")
    if not isinstance(fixes_signatures, dict) or not isinstance(fixes_offsets, dict):
        raise GamedataError("CS2Fixes gamedata is missing Signatures or Offsets")
    if not isinstance(kz_signatures, dict) or not isinstance(kz_offsets, dict):
        raise GamedataError("CS2KZ gamedata is missing Signatures or Offsets")

    sdk_patterns: dict[tuple[str, str], list[dict[str, object]]] = {}
    for item in sdk_data["patterns"]:
        if not isinstance(item, dict) or not isinstance(item.get("name"), str) or not isinstance(item.get("module"), str):
            raise GamedataError("CS2 SDK response contains a malformed pattern entry")
        key = (item["name"].casefold(), item["module"].casefold())
        sdk_patterns.setdefault(key, []).append(item)

    changes: dict[str, dict[str, str]] = {}
    provenance: dict[str, dict[str, str]] = {}
    missing: list[str] = []
    library_modules = {"server": "server.dll", "engine": "engine2.dll"}
    for entry_name, local in sorted(entries.items()):
        if local.section == "Signatures":
            fixes_entry = source_entry(fixes_signatures, local.key, "CS2Fixes")
            kz_entry = source_entry(kz_signatures, local.key, "CS2KZ")
            for candidate, source in ((fixes_entry, "CS2Fixes"), (kz_entry, "CS2KZ")):
                if candidate is not None:
                    check_source_library(candidate, local.values.get("library", "server"), local.key, source)

            module = library_modules.get(local.values.get("library", "server").casefold())
            sdk_entries = sdk_patterns.get((local.key.casefold(), module.casefold()), []) if module else []
            if len(sdk_entries) > 1:
                raise GamedataError(f"CS2 SDK has ambiguous {local.key} patterns in {module}")
            sdk_entry = sdk_entries[0] if sdk_entries else None

            for platform in PLATFORMS:
                candidates: list[tuple[object, str]] = []
                if platform == "windows" and sdk_entry is not None and sdk_entry.get("pattern") is not None:
                    candidates.append((sdk_entry.get("pattern"), "CS2 SDK"))
                for source, upstream_entry in (
                    ("CS2Fixes", fixes_entry),
                    ("CS2KZ", kz_entry),
                ):
                    if upstream_entry is not None and upstream_entry.get(platform) is not None:
                        candidates.append((upstream_entry[platform], source))
                if not candidates:
                    missing.append(f"{entry_name}.{platform}")
                    continue
                raw_pattern, selected_source = candidates[0]
                value = encode_source_pattern(raw_pattern, local.key, selected_source)
                if local.values.get(platform) != value:
                    changes.setdefault(entry_name, {})[platform] = value
                    provenance.setdefault(entry_name, {})[platform] = selected_source
        elif local.section == "Offsets":
            for platform in PLATFORMS:
                selected = select_offset(fixes_offsets, kz_offsets, local.key, platform)
                if selected is None:
                    missing.append(f"{entry_name}.{platform}")
                    continue
                value, selected_source = selected
                if local.values.get(platform) != value:
                    changes.setdefault(entry_name, {})[platform] = value
                    provenance.setdefault(entry_name, {})[platform] = selected_source

    required_offsets, _ = referenced_keys()
    for key in sorted(required_offsets):
        entry_name = f"Offsets.{key}"
        if entry_name in entries:
            continue
        values = {
            platform: select_offset(fixes_offsets, kz_offsets, key, platform)
            for platform in PLATFORMS
        }
        for platform, selected in values.items():
            if selected is None:
                missing.append(f"{entry_name}.{platform}")
        if all(selected is not None for selected in values.values()):
            for platform, selected in values.items():
                if selected is None:
                    continue
                value, selected_source = selected
                changes.setdefault(entry_name, {})[platform] = value
                provenance.setdefault(entry_name, {})[platform] = selected_source

    return changes, missing, provenance


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
        except (urllib.error.URLError, UnicodeDecodeError, json.JSONDecodeError, TimeoutError) as error:
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


def write_gamedata(path: Path, lines: list[str]) -> Path:
    """Backs up and atomically replaces a gamedata file."""

    backup = path.with_suffix(path.suffix + ".bak")
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", newline="\n", dir=path.parent, delete=False
        ) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write("\n".join(lines) + "\n")
            temporary.flush()
            os.fsync(temporary.fileno())
        shutil.copy2(path, backup)
        shutil.copystat(path, temporary_path)
        temporary_path.replace(path)
    except OSError as error:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
        raise GamedataError(f"cannot write {path}: {error}") from error
    return backup


def insert_offset_entries(lines: list[str], additions: dict[str, dict[str, str]]) -> list[str]:
    section_line = next(
        (index for index, line in enumerate(lines) if strip_comment(line).strip() == '"Offsets"'),
        None,
    )
    if section_line is None:
        raise GamedataError("cannot add offsets: gamedata has no Offsets section")
    open_line = next(
        (index for index in range(section_line + 1, len(lines)) if strip_comment(lines[index]).strip()),
        None,
    )
    if open_line is None or strip_comment(lines[open_line]).strip() != "{":
        raise GamedataError("cannot locate the opening brace of the Offsets section")

    depth = 1
    close_line: int | None = None
    for index in range(open_line + 1, len(lines)):
        text = strip_comment(lines[index])
        depth += text.count("{") - text.count("}")
        if depth == 0:
            close_line = index
            break
    if close_line is None:
        raise GamedataError("cannot locate the closing brace of the Offsets section")

    rendered: list[str] = []
    for name, values in sorted(additions.items()):
        rendered.extend(
            (
                f'\t\t\t"{name}"',
                "\t\t\t{",
                f'\t\t\t\t"windows"\t"{values["windows"]}"',
                f'\t\t\t\t"linux"\t\t"{values["linux"]}"',
                "\t\t\t}",
            )
        )
    lines[close_line:close_line] = rendered
    return lines


def apply_changes(
    lines: list[str],
    entries: dict[str, Entry],
    changes: dict[str, dict[str, str]],
    allow_new_offsets: bool = False,
) -> list[str]:
    """Rewrites only the changed values, keeping indentation and comments untouched."""

    new_offsets: dict[str, dict[str, str]] = {}
    for entry_name, platforms in changes.items():
        entry = entries.get(entry_name)
        if entry is None:
            section, separator, key = entry_name.partition(".")
            if not allow_new_offsets or section != "Offsets" or not separator or not key:
                raise GamedataError(f"unknown gamedata entry {entry_name}")
            if set(platforms) != set(PLATFORMS):
                raise GamedataError(f"new {entry_name} must include windows and linux values")
            values: dict[str, str] = {}
            for platform, value in platforms.items():
                try:
                    number = int(value, 0)
                except ValueError as error:
                    raise GamedataError(f"{entry_name}.{platform} is not an integer: {value!r}") from error
                if number < 0 or number > 0x100000:
                    raise GamedataError(f"{entry_name}.{platform} is outside the sane 0..0x100000 range")
                values[platform] = value
            new_offsets[key] = values
            continue
        for platform, value in platforms.items():
            line_index = entry.lines.get(platform)
            if line_index is None:
                raise GamedataError(f"{entry_name} has no {platform} line to update")
            if entry.section == "Offsets":
                try:
                    number = int(value, 0)
                except ValueError as error:
                    raise GamedataError(f"{entry_name}.{platform} is not an integer: {value!r}") from error
                if number < 0 or number > 0x100000:
                    raise GamedataError(f"{entry_name}.{platform} is outside the sane 0..0x100000 range")
            elif not SIGNATURE_PATTERN.fullmatch(value):
                raise GamedataError(f"{entry_name}.{platform} is not a sequence of \\xNN bytes")
            else:
                pattern_bytes = signature_bytes(value)
                if len(pattern_bytes) < 8 or sum(byte is not None for byte in pattern_bytes) < 6:
                    raise GamedataError(f"{entry_name}.{platform} is too short or too wildcarded")
            original = lines[line_index - 1]
            pattern = re.compile(r'("' + re.escape(platform) + r'")(\s+)"([^"]*)"')
            if not pattern.search(original):
                raise GamedataError(f"cannot rewrite line {line_index}: {original!r}")
            lines[line_index - 1] = pattern.sub(
                lambda match: f'{match.group(1)}{match.group(2)}"{value}"', original, count=1
            )
    if new_offsets:
        insert_offset_entries(lines, new_offsets)
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
                errors.append(f"{name}.{platform} is ambiguous in {binary.name}; found {len(matches)} matches")
                print(
                    f"  FAIL {name:<28} {platform:<8} {len(matches)} matches"
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
    parser.add_argument(
        "--refresh-online",
        action="store_true",
        help="fetch trusted online sources and preview matching values",
    )
    parser.add_argument("--timeout", type=float, default=30.0, help="timeout per upstream request in seconds")
    parser.add_argument("--set", action="append", default=[], metavar="KEY:PLATFORM=VALUE", help="apply a value")
    parser.add_argument("--write", action="store_true", help="write the accumulated changes back to disk")
    parser.add_argument("--quiet", action="store_true", help="only print problems")
    args = parser.parse_args(argv)

    if args.timeout <= 0:
        parser.error("--timeout must be greater than zero")
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

    if args.refresh_online and args.set:
        parser.error("--refresh-online and --set cannot be used together")
    if args.write and not (args.refresh_online or args.set or args.fetch):
        parser.error("--write requires --refresh-online, --set, or --fetch")

    if args.binary and not args.binary.is_file():
        print(f"error: binary not found: {args.binary}", file=sys.stderr)
        return 2
    if args.binary and not args.write:
        errors.extend(report_signatures(args.binary, entries))

    try:
        if args.refresh_online:
            changes, missing, provenance = load_online_changes(entries, args.timeout)
            print("Online sources: " + ", ".join(UPSTREAMS))
            if missing:
                print("warning: trusted sources do not cover " + ", ".join(missing))
            if not changes:
                print("online sources are already in sync")
        else:
            changes = parse_set_arguments(args.set)
            provenance = {}
    except GamedataError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    if changes:
        for entry_name, platforms in sorted(changes.items()):
            current = entries.get(entry_name)
            for platform, value in sorted(platforms.items()):
                old = current.values.get(platform, "<missing>") if current else "<missing>"
                source = provenance.get(entry_name, {}).get(platform)
                source_label = f" (from {source})" if source else ""
                print(f"  change {entry_name}.{platform}: {old} -> {value}{source_label}")

    info = fetch_dumper_info() if args.fetch else {}
    build = detect_build(info) if args.fetch else None
    if args.fetch and not args.write:
        marker = read_build_marker(lines)
        if build is None:
            print("CS2 build: unknown (cs2-dumper artifacts unavailable)")
        else:
            state = "up to date" if build == marker else f"gamedata targets build {marker}"
            print(f"CS2 build: {build} ({state})")

    if args.write:
        try:
            updated = (
                apply_changes(list(lines), entries, changes, allow_new_offsets=args.refresh_online)
                if changes
                else list(lines)
            )
        except GamedataError as error:
            print(f"error: {error}", file=sys.stderr)
            return 2
        if args.fetch and build is not None and build != read_build_marker(updated):
            updated = write_build_marker(updated, build)
            print(f"  change {BUILD_MARKER} {build}")
        if updated == lines:
            print("nothing to write; gamedata is already up to date")
        else:
            try:
                backup = write_gamedata(args.gamedata, updated)
            except GamedataError as error:
                print(f"error: {error}", file=sys.stderr)
                return 2
            print(f"\nwrote {args.gamedata} (backup: {backup.name})")
        try:
            lines, entries = parse_gamedata(args.gamedata)
        except (GamedataError, OSError, UnicodeDecodeError) as error:
            print(f"error: cannot validate written gamedata: {error}", file=sys.stderr)
            return 2
        errors, warnings = validate(entries, strict=args.strict)
        if args.binary:
            errors.extend(report_signatures(args.binary, entries))
    elif args.fetch:
        if build is not None and build != read_build_marker(lines):
            print(f"  run with --fetch --write to refresh {BUILD_MARKER}")
    elif args.refresh_online and changes:
        print("\ndry run: no files were written; pass --write to apply these values")

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
