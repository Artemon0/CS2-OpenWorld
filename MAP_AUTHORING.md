# Manual map-authoring guide

This is a practical list of content that a map author can prepare in Hammer
and Workshop Tools without modifying the engine. It describes a future
OpenWorld map; it does not assume that such a map already exists.

## 1. Server portals

A transition can be marked with an arch, door, elevator, tunnel, transit
station, or sign. Put the destination name and a clear instruction nearby:

```text
Travel to the Eastern District
Type in chat: !go east
```

The plugin supports `!go <server_id>` for Steam-authenticated players. It
redirects to the endpoint configured in `configs/openworld.ini`; valid IDs
come from `[servers.<server_id>]`.

**Limitation:** portal geometry and `trigger_multiple` do not start a
transition. The player must enter the chat command; trigger detection and
automatic transfer are not implemented.

## 2. Configure `openworld.ini`

Set a distinct `core.server_id` for each game-server process. Add each server's
ID, client-reachable address and port, and map:

```ini
[core]
server_id = west
environment = development

[servers.west]
connect = 203.0.113.10:27015
map = ow_west
max_players = 32

[servers.east]
connect = 203.0.113.10:27016
map = ow_east
max_players = 32
```

Keep the required `[persistence]`, `[cache]`, and `[zones.*]` sections from
the complete configuration. Every server must have the same destination list,
but its `core.server_id` must identify that instance. `connect` is the public
game-server address clients use, not a PostgreSQL or Redis address. When
hosting multiple servers on one machine, use distinct game ports and verify
port forwarding and firewall rules.

After restarting the plugin, a player on `west` can test the transition with:

```text
!go east
```

This reconnects the client; it is not a seamless transfer. Position, weapons,
health, and character runtime state are not guaranteed to carry over, and the
plugin does not automatically save them before disconnecting.

## 3. Reduce runtime entity counts

- Build static walls, roads, buildings, and terrain into appropriate meshes
  and compile them into map geometry.
- Use `prop_static` for non-moving decoration. Consider `Force Bake To World`
  (`baketoworld`) when an object can be merged into world geometry and needs no
  animation, physics, or interaction.
- Keep `prop_dynamic`, physics props, and interactive entities only where
  their behavior is needed.
- Do not disable mesh merging without a reason. Check visual quality,
  lighting, collision, and performance after baking and compiling.
- Leave headroom in the runtime entity budget. The SDK's `MAX_TOTAL_ENTITIES`
  is index capacity, not a recommended per-map budget.
- Plan a large world as multiple map segments within Source 2 limits. Visual
  effects do not provide streaming or increase entity limits.

## 4. Prepare content in Hammer and Workshop

These are content-authoring tasks, not plugin features:

- **Spawn points:** choose safe spawn locations for every server or sector;
  keep them outside solid geometry.
- **Transitions:** mark portals with doors, markers, lighting, signs, and
  clear destination instructions.
- **Navigation:** add road signs, district names, map legends, and landmarks
  so players can orient themselves without automatic GPS.
- **Collision:** test passages, stairs, slopes, doorways, and boundaries so
  players do not get stuck or fall out of the map.
- **Visibility:** use level geometry, tunnels, turns, interiors, and natural
  sightlines to control long views. This is ordinary level design, not hiding
  network data from clients.
- **Lighting and environment:** configure lighting and sky using map tools.
  Visual effects do not hide network data or stream map sections.
- **Workshop packages:** separate the map and shared resources into clear
  packages. Test downloads on a clean client without the project sources.
- **Test map:** begin with a small vertical slice containing a spawn, short
  route, portal marker, and transition to a second server. Test both servers,
  the map, and real port reachability.

## 5. Features that require plugin work

Hammer entities alone cannot provide:

1. Triggering `!go` when a player enters a trigger, with repeat protection.
2. Checking destination availability and free slots before redirecting.
3. Saving a transfer snapshot in PostgreSQL, issuing a one-time ticket,
   reserving the player, and restoring state on the destination server.
4. Choosing a safe destination spawn and converting coordinates between
   sectors.
5. Automatically selecting a destination server from coordinates or a portal.
6. Seamless transitions without disconnecting and reconnecting.

Do not assume these features exist just because a map has a trigger, the
configuration has a zone, or PostgreSQL has a related table. The currently
working transition is the manual `!go <server_id>` command followed by a
normal client reconnect.
