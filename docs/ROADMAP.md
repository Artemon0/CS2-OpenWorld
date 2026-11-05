# Roadmap

This file replaces the placeholder headers that used to live under `src/modules/` and
`src/services/`. Each of those files contained nothing but `class X;`, was included by nothing, and
made the module layout look more complete than it is. The intent is recorded here instead.

## Planned modules (not implemented)

| Area | Planned responsibility |
| --- | --- |
| `administration` | Admin commands, bans, audit trail |
| `audio` | Server-side sound events and music zones |
| `dialog` | NPC dialogue trees and scripted conversations |
| `health` | Bleeding, limb damage, medical items |
| `interaction` | Use/E prompts on world entities |
| `npc` | NPC behaviour trees and spawn management |
| `permissions` | Role/permission model on top of Steam auth |
| `players` | Steam auth handshake and per-player session state (currently in `src/core/plugin.cpp`) |
| `quests` | Quest definitions, progress, rewards |
| `social` | Parties, friends, chat channels |
| `vehicles` | Vehicle entities, seats, fuel |
| `weapons` | Weapon economy extensions beyond CS2 defaults |
| `world` | Sector/streaming management and entity budgets |
| `services/coordinator` | Cross-server coordinator RPC |
| `services/messaging` | Message transport abstraction between cluster nodes |
| `core/config_manager` | Hot reload of `openworld.ini` (today the config is read once at load) |
| `core/lifecycle_manager` | Explicit startup/shutdown ordering (today `ModuleManager` does it) |
| `core/diagnostics` | Structured diagnostics dump for support tickets |

## Known gaps in the current implementation

- `src/core/plugin.cpp` is a single large translation unit; the chat command router, route
  coordinator, character/settings coordinators, and hook binder are natural extraction points.
- The `Offsets` block in gamedata does not define `Touch`, `StartTouch`, `EndTouch` and
  `CollisionRulesChanged`; the corresponding `CBaseEntity` helpers are unused today and degrade to a
  no-op. `tools/update_offsets.py --check --strict` reports them.
- Character, inventory, and wallet data are never cached; every read goes to PostgreSQL. That is
  correct but may need an explicit read-through cache once player counts grow.
