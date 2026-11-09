# CS2 Open World

CS2 Open World is a Counter-Strike 2 server plugin built with C++, Metamod:Source, and hl2sdk-cs2. The plugin entry point and initial OW Core live in src/core/; gameplay and infrastructure components are organized under src/adapters/, src/modules/, and src/ui/; planned modules and services are listed in [docs/ROADMAP.md](./docs/ROADMAP.md).

The implementation is server-authoritative. Steam authorization must be confirmed through the engine interface before a player is considered authenticated; a non-zero SteamID alone is not sufficient.

## Architecture decisions

- Panorama is preferred for simple HUD elements that can be implemented through supported mechanisms, such as health and ammunition indicators.
- Particle UI is the planned subsystem for specialized menus. The current C++ particle menu is experimental and opt-in (`adapters.particle_menu = true`); it installs a KHook transmit filter and therefore defaults off unless explicitly enabled. Workshop-delivered particle resources are required.
- Maps and shared assets are delivered as separate Workshop packages.
- PostgreSQL is the source of truth for persistent player and world data. Cache backends are emergency-only fallback code and are excluded from the default plugin build.
- PostgreSQL-backed server affinity routes authenticated players to a configured lobby on first join and persists their assigned server across the cluster. `!go <server_id>` stores the new route before redirecting. This is a reconnect, not a seamless transfer, and still needs live multi-server validation.
- Automatic zone transitions use configured XY rectangles to assign and reconnect living players to the next server after crossing a chunk boundary. They require a shared world-coordinate layout and do not preserve position across the reconnect.

The architecture specification, including accepted decisions and open technical questions, is in [CS2 Open World.md](./CS2%20Open%20World.md).
Operator instructions for the cluster config, Redis, PostgreSQL schema, deployment, and map pipeline are in [SETUP.md](./SETUP.md).
Russian manual map-authoring suggestions and portal setup are in [РУКОВОДСТВО_ПО_РУЧНОЙ_КАРТЕ.md](./РУКОВОДСТВО_ПО_РУЧНОЙ_КАРТЕ.md).
Original project contributions are proprietary; third-party terms and commercial-distribution caveats are documented in [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md). This notice does not establish that commercial use of a CS2 modification is authorized.

## Quick start

The repository ships one interactive installer per platform. It checks every dependency, asks
before installing anything, and asks for a path when a tool exists but is not on `PATH`
(PostgreSQL, Redis, MSVC, Clang, CMake, Ninja).

```bash
scripts/install.sh --check     # report only, changes nothing
scripts/install.sh             # install what is missing, create .env, build, run tests
```

```powershell
.\scripts\install.ps1 -Check
.\scripts\install.ps1
```

Both scripts initialize the `hl2sdk-cs2` and `metamod-source` submodules, generate a `.env` with
strong random passwords, optionally bootstrap the PostgreSQL roles/database/migrations, and finish
by building the plugin and running `ctest`.

## Build

Requirements: CMake 3.19+, 64-bit MSVC on Windows or Clang on Linux, hl2sdk-cs2, and Metamod:Source.

```bash
git submodule update --init --recursive
export CC=clang CXX=clang++
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j
ctest --test-dir build-linux --output-on-failure
```

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

`hl2sdk-cs2` and `metamod-source` are tracked as *shallow* submodules to keep clones small. If a
pinned commit is no longer the tip of its branch, `git submodule update` cannot fetch it; run
`git config --unset submodule.hl2sdk-cs2.shallow` (and the same for `metamod-source`) and retry.
`scripts/install.sh` does this automatically.

The plugin is built with **exceptions disabled** (`-fno-exceptions` / `_HAS_EXCEPTIONS=0`). New code
must report failures through return values, `bool` flags, `std::optional`, or an `IsRunning()` /
`IsEnabled()` accessor. Never add `throw`, `try`, or `catch` to a target that links `ow_build_flags`;
the unit tests link the very same interface library, so a build break is caught by `ctest` before a
release. See `cmake/CompilerSettings.cmake` for the flag set shared by the plugin and the tests.

Gamedata (`gamedata/cs2-openworld.games.txt`) drives every engine offset and signature. Verify it
before shipping to a live server:

```bash
python tools\update_offsets.py --check
python tools\update_offsets.py --check --binary C:\cs2\game\bin\win64\server.dll
python tools\update_offsets.py --fetch
python tools\update_offsets.py --refresh-online
```

`--refresh-online` reads current values directly from CS2 SDK, Source2ZE/CS2Fixes, and KZGlobalTeam/
CS2KZ over HTTPS. It previews exact matches with their source; add `--write` to apply them. No
separate CS2AC checkout is required.

The standard build includes a small, process-local cache for player settings only (30-second TTL, up to 512 entries and 4 MiB of payload). Character state, inventory, money, and transfer data are never served from this cache; they require authoritative persistence and revision/idempotency checks. The optional generic LRU/Redis/warmup fallback sources are excluded by default; -DOW_ENABLE_CACHE_FALLBACK=ON only includes those experimental components and does not make them part of player login or transfer.

The server plugin package is written to `build-windows-release/package`. Its `addons/` and `cfg/` folders mirror a CS2 server's `game/csgo/` and should be copied there. An optional client-side bind file is staged separately under `client/cfg/`; install it only on clients that want E/TAB shortcuts. See `PACKAGE_INSTALL.txt` for instructions and runtime prerequisites.

## Current implementation

- src/core/plugin.cpp implements the Metamod lifecycle, player lifecycle hooks, the ow_status command, Steam authorization checks, game event hooks, and module initialization.
- src/core/module_manager.*, event_bus.h, service_registry.h provide module management, typed events, and service registration.
- src/modules/core/, src/modules/character/, src/modules/transfer/ contain core gameplay modules integrated via ModuleManager.
- src/modules/transfer/ implements transfer validation, idempotency, state transitions, and expiry in-process; TransferModule exposes the state machine as version 1 of ITransferService while running.
- src/core/cluster_config.* loads and validates the server/zone topology.
- src/modules/persistence/ retains optional generic LRU, async-warmup, and RESP2 Redis fallback sources; CMake excludes these unless OW_ENABLE_CACHE_FALLBACK=ON.
- src/modules/persistence/player_route_worker.* performs asynchronous route lookups and assignments. All managed servers need the same `[routing]`/`[servers.*]` configuration and migration 004; unknown or unavailable routes fail closed.
- src/adapters/persistence/postgres_adapter.* dynamically loads libpq at runtime and provides parameterized database operations, including owner-checked character loads and revision-checked inventory saves.
- src/modules/persistence/player_character_worker.* lists, creates, and loads characters off the game thread. `!character list|create <name>|select <id>` selects the current character; a sole character is selected automatically.
- src/modules/inventory/ and src/modules/economy/ expose registered services. `!inventory` and the optional E bind print the stored JSON snapshot to the client console; `!balance` shows the selected character's wallet. Item acquisition, mutation commands, and gameplay rules are not implemented.
- `!map` and the optional TAB bind report the current server map name. A graphical map, player position marker, and map-view data are not implemented because the server-only Panorama adapter cannot open custom panels.
- src/ui/ui_manager.* and src/ui/hud/ provide per-player UI routing plus profile-based HUD text elements; the current renderer uses transient center-screen TextMsg messages, not custom graphical panels.
- src/ui/input/ provides per-player input contexts, edge-triggered snapshot handling, and logical action dispatch. The optional package client config forwards E to inventory and TAB to the map action while preserving stock +use and scoreboard behavior; it does not replace the scoreboard or expose arbitrary keyboard events to the server. The example bind forwarding must still be verified in a live CS2 client/server.
- src/adapters/panorama/ sends per-player notifications, console, chat, and center text through CS2 TextMsg. The text HUD is toggled with `!hud [on|off|toggle]`. Arbitrary Panorama views, JS, and client actions are unsupported by a server-only adapter; map display therefore currently falls back to the current server map name.
- src/ui/particle/ provides the C++ menu model and sessions; its particle-based runtime still depends on Workshop resources and in-game validation.
- ow_core_enabled is the configuration toggle reserved for gameplay processing; Steam authorization checks continue when gameplay is disabled.

## Configuration

The package includes `addons/cs2-openworld/configs/openworld.ini`; edit it for the target server. `configs/openworld.ini` shows the multi-server cluster mode. For a standalone test on a stock map, copy `configs/openworld.single-map.ini.example` and set `[world] mode = single_map` plus `[world] map = de_dust2` (the map must also match the sole `[servers.*]` entry; zones are not used in this mode). The `[adapters]` section independently controls PostgreSQL, Panorama text, HUD, input actions, and map-overview caching. `[modules]` independently controls the implemented character, economy, inventory, and transfer modules. For standalone testing, set `adapters.postgresql = false`, `persistence.provider = none`, and leave `connection_env` empty; PostgreSQL-backed modules default off and cannot be enabled without PostgreSQL. Particle UI, NPC, and dialog runtimes are not wired into plugin startup, so they are not exposed as pretend toggles. See [SETUP.md](./SETUP.md) for deployment instructions.
