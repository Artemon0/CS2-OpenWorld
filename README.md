# CS2 Open World

CS2 Open World is a Counter-Strike 2 server plugin built with C++, Metamod:Source, and hl2sdk-cs2. The plugin entry point and initial OW Core live in `src/core/`; planned gameplay and infrastructure components have dedicated header placeholders under `src/adapters/`, `src/modules/`, `src/services/`, and `src/ui/`.

The implementation is server-authoritative. Steam authorization must be confirmed through the engine interface before a player is considered authenticated; a non-zero SteamID alone is not sufficient.

## Architecture decisions

- Panorama is preferred for simple HUD elements that can be implemented through supported mechanisms, such as health and ammunition indicators.
- Particle UI is a required subsystem for specialized menus and interfaces. Its runtime is being prototyped in C++ without SwiftlyS2; Workshop-delivered particle resources are required.
- Maps and shared assets are delivered as separate Workshop packages.
- PostgreSQL is the source of truth for persistent player and world data. Redis is the preferred shared cache for multi-process deployments; single-server local development can use an in-process cache.
- Cross-server transfers are owned by the project's Transfer Manager. Client redirection and seamless transfer remain unverified until tested in CS2.

The architecture specification, including accepted decisions and open technical questions, is in [CS2 Open World.md](./CS2%20Open%20World.md).

## Build

Requirements: CMake, 64-bit MSVC on Windows or Clang on Linux, hl2sdk-cs2, and Metamod:Source.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The packaged plugin is written to `build/package`. It contains the binary and Metamod VDF file; configuration is copied to `game/csgo/cfg/cs2-openworld.cfg`.

## Current implementation

- `src/core/plugin.cpp` implements the Metamod lifecycle, player lifecycle hooks, the `ow_status` command, and Steam authorization checks.
- `src/core/module_manager.*` provides dependency ordering, startup, rollback, and reverse-order shutdown; `event_bus.h` and `service_registry.h` implement typed local event and service registration.
- `src/modules/transfer/` implements transfer validation, idempotency, state transitions, and expiry in-process. `ClientTransferHandoff` verifies the destination transfer state and the connected player's fully authenticated SteamID before asking `IVEngineServer2` to send a validated `connect host:port` command; actual client/server behavior still requires in-game verification.
- `src/modules/persistence/cache_manager.*` provides a revision- and TTL-aware local cache behind a backend interface. A Redis backend and PostgreSQL persistence adapter still need implementation.
- `src/ui/particle/` provides the C++ menu model, six-slot hit-testing, virtual-cursor input, and SteamID-owned sessions. Particle message/entity creation and Workshop VPCF resources are not yet wired to rendering.
- `ow_core_enabled` is the configuration toggle reserved for gameplay processing; Steam authorization checks continue when gameplay is disabled.

The particle-menu prototype is based on the behavior documented by [SwiftParticleMenuDemo](https://github.com/nicedayzhu/SwiftParticleMenuDemo): player view-angle deltas drive a server-side virtual cursor, and particle control points carry cursor and selection state. Its SwiftlyS2 runtime is not used.
