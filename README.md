# CS2 Open World

CS2 Open World is a Counter-Strike 2 server plugin built with C++, Metamod:Source, and hl2sdk-cs2. The plugin entry point and initial OW Core live in src/core/; gameplay and infrastructure components are organized under src/adapters/, src/modules/, src/services/, and src/ui/.

The implementation is server-authoritative. Steam authorization must be confirmed through the engine interface before a player is considered authenticated; a non-zero SteamID alone is not sufficient.

## Architecture decisions

- Panorama is preferred for simple HUD elements that can be implemented through supported mechanisms, such as health and ammunition indicators.
- Particle UI is a required subsystem for specialized menus and interfaces. Its runtime is being prototyped in C++ without SwiftlyS2; Workshop-delivered particle resources are required.
- Maps and shared assets are delivered as separate Workshop packages.
- PostgreSQL is the source of truth for persistent player and world data. Cache backends are emergency-only fallback code and are excluded from the default plugin build.
- Cross-server transfers are owned by the project's Transfer Manager. Client redirection and seamless transfer remain unverified until tested in CS2.

The architecture specification, including accepted decisions and open technical questions, is in [CS2 Open World.md](./CS2%20Open%20World.md).
Operator instructions for the cluster config, Redis, PostgreSQL schema, deployment, and map pipeline are in [SETUP.md](./SETUP.md).
Russian manual map-authoring suggestions and portal setup are in [РУКОВОДСТВО_ПО_РУЧНОЙ_КАРТЕ.md](./РУКОВОДСТВО_ПО_РУЧНОЙ_КАРТЕ.md).

## Build

Requirements: CMake, 64-bit MSVC on Windows or Clang on Linux, hl2sdk-cs2, and Metamod:Source.

`powershell
cmake -S . -B build -A x64
cmake --build build --config Release
`

The standard build includes a small, process-local cache for player settings only (30-second TTL, up to 512 entries and 4 MiB of payload). Character state, inventory, money, and transfer data are never served from this cache; they require authoritative persistence and revision/idempotency checks. The optional generic LRU/Redis/warmup fallback sources are excluded by default; -DOW_ENABLE_CACHE_FALLBACK=ON only includes those experimental components and does not make them part of player login or transfer.

The packaged plugin is written to uild/package. It contains the binary, Metamod VDF file, server config, and ddons/cs2-openworld/configs/openworld.ini.

## Current implementation

- src/core/plugin.cpp implements the Metamod lifecycle, player lifecycle hooks, the ow_status command, Steam authorization checks, game event hooks, and module initialization.
- src/core/module_manager.*, event_bus.h, service_registry.h provide module management, typed events, and service registration.
- src/modules/core/, src/modules/character/, src/modules/transfer/ contain core gameplay modules integrated via ModuleManager.
- src/modules/transfer/ implements transfer validation, idempotency, state transitions, and expiry in-process.
- src/core/cluster_config.* loads and validates the server/zone topology.
- src/modules/persistence/ retains optional generic LRU, async-warmup, and RESP2 Redis fallback sources; CMake excludes these unless OW_ENABLE_CACHE_FALLBACK=ON.
- A player can request a basic server reconnect by writing !go <server_id> in chat.
- src/adapters/persistence/postgres_adapter.* dynamically loads libpq at runtime and provides parameterized database operations.
- src/ui/particle/ provides the C++ menu model and sessions.
- ow_core_enabled is the configuration toggle reserved for gameplay processing; Steam authorization checks continue when gameplay is disabled.

## Configuration

Copy configs/openworld.ini.example to ddons/cs2-openworld/configs/openworld.ini and configure server, persistence, cache, and cluster settings. See [SETUP.md](./SETUP.md) for detailed instructions.

