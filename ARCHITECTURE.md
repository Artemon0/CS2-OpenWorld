# Architecture overview

CS2 Open World is a server-authoritative Counter-Strike 2 plugin built with C++,
Metamod:Source, and the CS2 HL2SDK. Its detailed design specification is still
available in [Russian](./CS2%20Open%20World.ru.md); this document summarizes
the current architecture and implementation boundaries in English.

## Design principles

- **Server authority:** the server owns gameplay state. Client input is a
  request, not proof that a player owns an item, balance, or completed action.
- **Modularity:** gameplay features are separate modules connected through
  explicit services and events.
- **Persistence boundaries:** modules use persistence interfaces rather than
  issuing SQL directly.
- **Safe engine lifetimes:** long-lived state should use stable identifiers
  and validated handles instead of retaining raw engine pointers.
- **UI independence:** gameplay logic should not depend on a particular
  rendering mechanism.

## Runtime layers

```text
Counter-Strike 2
  Metamod:Source
    OW Core
      module manager, event bus, service registry, configuration
      player lifecycle, engine adapters, diagnostics
    Gameplay modules
      character, transfer, inventory, economy
    Adapters
      PostgreSQL, text UI, input, optional particle menu
```

The core owns plugin lifecycle and wires adapters and modules together.
Modules publish and consume typed events and versioned service interfaces.
The persistence adapter centralizes parameterized database operations.

## Current implementation

- `src/core/` contains the Metamod lifecycle, module management, configuration,
  event bus, and service registry.
- `src/modules/character/` manages character listing, creation, and selection.
- `src/modules/transfer/` implements route assignment and reconnect-based
  server transitions. It does not preserve runtime player state across servers.
- `src/modules/inventory/` and `src/modules/economy/` expose versioned services
  for inventory snapshots and wallet operations. Gameplay acquisition and
  mutation flows remain incomplete.
- `src/adapters/persistence/` provides PostgreSQL operations. Player settings
  have a small process-local cache; PostgreSQL remains the source of truth.
- `src/ui/` and `src/adapters/panorama/` currently provide text-based
  notifications and HUD output. They do not create arbitrary Panorama panels.
- Particle UI is experimental, requires Workshop resources, and is disabled by
  default.

## Server clusters

Each server is configured with a server ID, a shared list of server endpoints,
and optional XY map zones. In cluster mode, PostgreSQL stores player-to-server
affinity. `!go <server_id>` saves the destination before sending a CS2
`connect` command. This causes a reconnect; it is not seamless transfer and
does not carry position, inventory mutations, or other runtime state.

All servers in a cluster must use the same environment, routing settings,
server list, and PostgreSQL database. The routing migration must be applied
before enabling affinity. Unknown or unavailable destinations fail closed.

## Persistence and threading

PostgreSQL is the source of truth for persistent player and world data.
Player-settings reads use a bounded, process-local cache only. Character,
inventory, balance, and transfer data are not served from that cache.
Database work that may block is performed outside the game thread; results
that affect engine state are applied on the game thread after validating the
player session.

The Redis/LRU/warmup fallback sources are experimental and excluded from the
default build. They are not required for normal login or routing.

## Known boundaries

The plugin does not yet implement seamless transfers, transfer snapshots,
position restoration, automatic portal triggers, NPCs, quests, vehicles, or a
complete gameplay inventory. See [SETUP.md](./SETUP.md) for deployment details
and the [Russian design specification](./CS2%20Open%20World.ru.md) for the full
planned architecture.
