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

## Text output

Send text to one player through `utils/chat.h` (`src/utils/chat.cpp`) instead of calling
`IVEngineServer2::ClientPrintf` directly: `ToPlayer` (chat), `ToConsole`, `ToCenter`,
`ToNotification`, `ToChannel(ow::ui::UITextChannel)`. `utils/chat.h` also has the transport-neutral
`Sanitize`; `utils/print_buffer.h` adds the optional printf-style `Format`. Every function fails
closed (returns `false`) when the slot or engine interface is missing and rejects messages over
`MaxMessageBytes` (512). A failed network send falls back to the engine console. The per-player
UI policy (Panorama adapter or engine fallback) stays in `OpenWorldPlugin::SendPlayerText`; use
that when a command reply should honour `adapters.panorama = false`.

## Known gaps in the current implementation

- `src/core/plugin.cpp` is a single large translation unit; the chat command router, route
  coordinator, character/settings coordinators, and hook binder are natural extraction points.
- The `Offsets` block in gamedata does not define `Touch`, `StartTouch`, `EndTouch` and
  `CollisionRulesChanged`; the corresponding `CBaseEntity` helpers are unused today and degrade to a
  no-op. `tools/update_offsets.py --check --strict` reports them.
- Character, inventory, and wallet data are never cached; every read goes to PostgreSQL. That is
  correct but may need an explicit read-through cache once player counts grow.
- Event delivery is single-threaded (game thread). `EventBus::Publish` is synchronous and rejects
  re-entrant cycles past `MaxPublishDepth`; `Enqueue`/`DispatchPendingEvents` (drained once per
  `OnGameFrame`) deliver owned copies for self-publishing listeners. A listener that unbinds itself
  from inside its own callback skips the remaining listeners of the same publish on purpose; a
  listener must not unbind another one mid-publish. Cross-thread publishing is unsupported.
- `TransferManager` is purely in-process with no locks; call it from the game thread only. It is the
  validation state machine, not the cross-server coordinator: reservation, tokens, ownership
  fencing, and the destination arrival handshake are still open (ADR-025).
