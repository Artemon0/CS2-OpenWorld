# Development tools

## update_offsets.py

Verifies and refreshes `gamedata/cs2-openworld.games.txt`, which drives every engine offset and
signature the plugin uses at runtime.

```bash
./update_offsets.py --check                        # validate structure + keys used by src/
./update_offsets.py --check --strict               # same, but failing on keys missing from gamedata
./update_offsets.py --check --binary libserver.so  # every signature must match exactly once
./update_offsets.py --fetch                       # current CS2 build (a2x/cs2-dumper)
./update_offsets.py --set ClientOffset:linux=584 --write
```

What is automated and what is not:

| Data | Source |
| --- | --- |
| Signature presence in a specific binary | `--binary` scans the file directly (wildcards `\x2A` supported) |
| CS2 build tracking | `--fetch` reads `a2x/cs2-dumper` (`engine2.dll.dwBuildNumber`) |
| Member offsets (`GameEntitySystem`, `ClientOffset`, `ClientSlotOffset`) | manual `--set`, they are plain C++ layout constants that no schema dump exposes |
| Virtual-table indices (`Teleport`, `IsEntityPawn`, `IsEntityController`) | manual `--set` |

`--write` edits only the affected values, keeps tabs/comments intact and saves a `.bak` copy.

Workflow after a CS2 update:

1. `./update_offsets.py --check --binary <server binary>` – see which signatures broke.
2. Re-dump with [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper) (or take the public output).
3. Apply the new values with `--set ... --write`.
4. Re-run step 1 until every signature reports exactly one match.

## Other helpers

Keep resource validation, Workshop packaging, migration, and local-server helper tools here.
