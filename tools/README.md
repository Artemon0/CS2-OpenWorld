# Development tools

## update_offsets.py

Verifies and refreshes `gamedata/cs2-openworld.games.txt`, which drives engine offsets and
signatures used by the plugin.

```powershell
python tools\update_offsets.py --check
python tools\update_offsets.py --check --strict
python tools\update_offsets.py --check --binary C:\cs2\game\bin\win64\server.dll
python tools\update_offsets.py --refresh-online
python tools\update_offsets.py --refresh-online --write
```

`--refresh-online` fetches live data directly from these fixed HTTPS sources:

- [CS2 SDK patterns API](https://cs2-sdk.com/api/patterns)
- [Source2ZE/CS2Fixes gamedata](https://github.com/Source2ZE/CS2Fixes/blob/main/gamedata/cs2fixes.jsonc)
- [KZGlobalTeam/cs2kz-metamod gamedata](https://github.com/KZGlobalTeam/cs2kz-metamod/blob/master/gamedata/cs2kz-core.games.txt)

Windows signatures prefer an exact name/module match from CS2 SDK, then CS2Fixes, then CS2KZ.
Linux signatures prefer CS2Fixes, then CS2KZ. Offsets prefer CS2Fixes, then CS2KZ. A source must
match the gamedata key (and library, for signatures); the tool validates every selected pattern
and offset before proposing changes. Each proposed value reports its source. Existing values
without an upstream match stay unchanged. Missing offsets used by the C++ sources are added only
when both platform values are available from an upstream.

All three sources must download and parse successfully before values are proposed. A failed
request, invalid response, or invalid matched value stops the refresh without writing. Refresh is a
dry run unless `--write` is specified. Writes preserve comments and formatting, create a `.bak`
copy, and atomically replace the gamedata file.

`--binary` checks that every signature matches exactly once in the target server binary.
`--fetch` reads the current build number from [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper);
combine it with `--write` to update the build marker.

Member offsets absent from these upstream gamedata files (such as `GameEntitySystem`,
`ClientOffset`, and `ClientSlotOffset`) still require a separately verified value and can be
entered explicitly with `--set`.

## Other helpers

Keep resource validation, Workshop packaging, migration, and local-server helper tools here.
