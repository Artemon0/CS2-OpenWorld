# CS2 Open World setup

This guide covers Windows deployment, PostgreSQL, plugin configuration, the
settings cache, and basic map preparation. The SQL steps are written for
pgAdmin Query Tool. Never put passwords in the plugin INI file or commit them
to Git.

## 1. Current functionality

- The plugin reads `addons/cs2-openworld/configs/openworld.ini` relative to
  the CS2 game directory (`game/csgo`). Relative `OW_CONFIG_PATH` values use
  that directory too; absolute paths are used as supplied.
- Configuration validates the server ID, cache settings, server endpoints,
  and rectangular XY map zones.
- In cluster mode, server affinity is stored in PostgreSQL. After Steam
  authentication, the server asynchronously looks up the player's assigned
  server. New players are assigned to `[routing].lobby_server_id`. `!go
  <server_id>` stores the new assignment before redirecting the client.
- PostgreSQL uses a bounded, process-local player-settings cache: at most 512
  entries, 4 MiB of JSON payload, and a 30-second TTL. Successful writes
  refresh the cache; failed writes invalidate the entry. Characters,
  inventory, balances, and transfer state do not use this cache.
- Generic Redis/LRU/warmup components are experimental fallback code excluded
  from the default DLL. They are not connected to the player lifecycle and
  are not needed for the normal settings cache.
- The PostgreSQL adapter dynamically loads libpq, checks the connection when
  the plugin loads, and provides parameterized character, settings, and
  economy operations. Inventory writes use revision-checked compare-and-swap.
  Settings load asynchronously after full Steam authentication; results are
  applied on the game thread only if the player session is still current.
- `!hud` (also `!hud on` and `!hud off`) toggles a multiline text HUD showing
  the server, Steam session, and settings-load status. It is not a graphical
  Panorama panel.
- `InventorySystem` and `EconomySystem` expose registered services. After
  Steam authentication, characters load in the background. A single character
  is selected automatically; use `!character list`, `!character select <id>`,
  or `!character create <name>` when needed. `!inventory` prints the stored
  JSON snapshot to the player's console; `!balance` shows the wallet.
  Item-acquisition commands and gameplay inventory rules are not implemented.

Settings loading and basic character selection/creation are implemented.
Saving gameplay state on disconnect and transferring runtime character state
are not. A redirect is a reconnect, not a seamless transfer.

## 2. Requirements

- A CS2 dedicated server with Metamod:Source. Build the plugin for the same
  platform as the server.
- PostgreSQL 14+ and the libpq runtime library available to the server.
  PostgreSQL must be reachable when the plugin loads.
- Redis is not required for the standard build. The local settings cache runs
  inside the plugin process and does not replace PostgreSQL or share data
  between servers.
- Docker Desktop is needed only for the Compose setup below. An existing
  PostgreSQL/Redis installation can be used instead by changing the addresses
  and applying the SQL scripts there.

## 3. Start PostgreSQL with pgAdmin

### 3.1 Start the database

Copy `.env.example` to `.env` in the repository root. Replace every
`replace-with-...` value with a distinct, long random password using Latin
letters and digits, without spaces or apostrophes. Never commit `.env`.

If the game server runs on this computer, leave
`OW_BIND_ADDRESS=127.0.0.1`. Open PowerShell in the project directory:

```powershell
docker compose up -d
docker compose ps
```

Wait for the containers to report `running`/`healthy`. In pgAdmin, create a
connection with:

- Host: `127.0.0.1`
- Port: `5432`
- Maintenance database: `postgres`
- Username: `postgres`
- Password: the `OW_PG_ADMIN_PASSWORD` value from `.env`

If PostgreSQL is already using port 5432, do not start a second instance on
the same port. Use the existing PostgreSQL server or change the host port in
`compose.yaml` and use that port in the plugin connection string.

An existing Compose volume retains its old users and databases; changing
`POSTGRES_USER` or `POSTGRES_DB` does not reinitialize it. Do not run
`docker compose down -v` to fix this. Back up the data and verify the volume
first. The steps below assume a new, empty database.

### 3.2 Create roles and the database

There are two roles: `ow_migrator` owns the schema and applies migrations;
`ow_app` is used by the plugin and has restricted permissions.

1. In pgAdmin, connect to the `postgres` database and open **Query Tool**.
   Open `database/bootstrap/001_create_roles_and_database.sql` and execute
   both `CREATE ROLE` statements. In **Login/Group Roles**, edit each role's
   **Definition** and set distinct passwords matching
   `OW_PG_MIGRATOR_PASSWORD` and `OW_PG_APP_PASSWORD` in `.env`. The SQL file
   intentionally contains no passwords.
2. In Query Tool, run the single command in
   `database/bootstrap/002_create_database.sql` separately. `CREATE DATABASE`
   cannot run inside a transaction.
3. Refresh the database list, connect to `openworld` as `ow_migrator`, then
   run these files in order:
   - `database/migrations/001_core.sql`
   - `database/migrations/002_application_functions.sql`
   - `database/migrations/003_inventory_functions.sql`
   - `database/migrations/004_player_server_routes.sql`
   - `database/bootstrap/003_app_permissions.sql`

The tables are created in the `ow` schema. If migrations 001-003 were already
applied, run only `004_player_server_routes.sql`, then rerun
`database/bootstrap/003_app_permissions.sql` to grant `ow_app` route
permissions.

To try the SQL manually, run `database/examples/quickstart.sql` in the
`openworld` database as `ow_app`. It creates a test character and credits
1,000 units; each run creates another test character.

Do not run the plugin as `postgres` or `ow_migrator`; use `ow_app`. These steps
also work with an existing PostgreSQL installation. Compose uses PostgreSQL
17; the schema supports PostgreSQL 14 and later.

## 4. Settings cache and optional Redis fallback

The standard DLL includes a small process-local cache only for
`LoadPlayerSettings`: up to 512 users, 4 MiB total JSON payload, and a
30-second TTL. It reduces repeated reads on one server. Settings are still
written to PostgreSQL with revision checks; a successful write refreshes the
local entry and a failed write invalidates it. Changes made on another server
may take up to 30 seconds to become visible.

This cache is not used for characters, inventory, balances, transactions,
authentication, or transfer state. PostgreSQL remains the source of truth.
The cache is local to one process; it does not transfer or preload data for
another server.

Generic Redis/LRU/warmup components are experimental and disconnected from
the player lifecycle. Enable them only for separate development or testing:

```powershell
cmake -S . -B build-windows-release -A x64 -DOW_ENABLE_CACHE_FALLBACK=ON
```

This is not required for normal gameplay. Enabling the build option does not
make Redis part of normal login or server transfers.

### Optional Redis test setup

On one computer, leave `OW_BIND_ADDRESS=127.0.0.1` in `.env` and run
`docker compose up -d`. In an experimental build, `provider = memory` does
not require Redis; to test Redis, set `provider = redis`,
`shared_provider = redis`, and `redis_host = 127.0.0.1`.

For two computers on a trusted home network:

1. Run `ipconfig` on the main computer and find its LAN IPv4 address, for
   example `192.168.1.50`.
2. Set `OW_BIND_ADDRESS` to that address in `.env`, then run
   `docker compose up -d` to recreate the port bindings.
3. In Windows Firewall, allow Redis only from the laptop's address. Run
   PowerShell as Administrator and replace the example address:

   ```powershell
   New-NetFirewallRule -DisplayName "OW Redis from laptop" -Direction Inbound -Protocol TCP -LocalPort 6379 -RemoteAddress 192.168.1.51 -Action Allow
   ```

   If the laptop needs direct PostgreSQL access, create a separate rule for
   TCP 5432 and the laptop's address. Do not allow `Any` and do not forward
   TCP 6379 or 5432 through the router.
4. In both `openworld.ini` files, configure the same Redis endpoint, database,
   and `redis_key_prefix`.
5. Before starting CS2 on each computer, set the server process environment
   variables with your own secrets:

   ```powershell
   $env:OW_REDIS_USERNAME = 'default'
   $env:OW_REDIS_PASSWORD = 'your-redis-password'
   $env:OW_PG_CONNINFO = 'host=127.0.0.1 port=5432 dbname=openworld user=ow_app ****** sslmode=prefer connect_timeout=3'
   ```

   On the laptop, use the main computer's IP in `OW_PG_CONNINFO` if PostgreSQL
   is accessible over the LAN. Use `OW_PG_APP_PASSWORD` for the PostgreSQL
   password. PowerShell environment variables are inherited by child processes
   started from that window; configure the environment of a Windows service
   separately.

Only expose the game-server port needed by players (usually UDP 27015) through
the router. Redis and PostgreSQL must not be reachable from the Internet.

## 5. Connect PostgreSQL to the plugin

The example `configs/openworld.ini` sets
`persistence.provider = postgresql` and `connection_env = OW_PG_CONNINFO`.
At load time, the plugin requires that environment variable, loads libpq,
connects, and runs `SELECT 1`. If the variable, library, or database is
unavailable, plugin loading fails; it does not silently fall back to memory.

`[persistence].connect_timeout` is a connection timeout in seconds (1-60),
passed to libpq separately from the connection string.

- **Windows:** install the x64 PostgreSQL client/runtime and add its `bin`
  directory to the CS2 process `PATH`. It must contain `libpq.dll` and its
  runtime dependencies.
- **Linux:** install the libpq runtime package (for example `libpq5` on
  Debian/Ubuntu). PostgreSQL development headers are not needed to build the
  adapter.
- A connection string looks like
  `host=127.0.0.1 port=5432 dbname=openworld user=ow_app ****** sslmode=prefer connect_timeout=3`.
  Do not store it in an INI file, Workshop content, or Git.
- `ow_status` reports PostgreSQL, the local settings cache, and the optional
  generic fallback. Other modules can obtain the adapter through
  `GetPersistenceAdapter()`.
- `OW_TEST_PG_CONNINFO` enables an optional integration test that writes test
  rows. Use it only with a separate test database.

Unit tests do not require a running PostgreSQL server or load libpq. The
integration test runs only when `OW_TEST_PG_CONNINFO` is explicitly set.

## 6. Server and zone configuration

The source example is [`configs/openworld.ini`](./configs/openworld.ini). It
is packaged at `addons/cs2-openworld/configs/openworld.ini`, relative to
`game/csgo`. Override the path with `OW_CONFIG_PATH`; relative values are
resolved from the game directory. Install the entire `addons/cs2-openworld`
directory, including `gamedata/cs2-openworld.games.txt`, not just the DLL.
Unknown keys/sections and overlapping zones are configuration errors.

```ini
[core]
server_id = west
environment = production

[persistence]
provider = postgresql
connection_env = OW_PG_CONNINFO

[cache]
provider = memory
shared_provider = none
redis_host = 127.0.0.1
redis_port = 6379
redis_database = 0
redis_username_env = OW_REDIS_USERNAME
redis_password_env = OW_REDIS_PASSWORD
redis_key_prefix = openworld:production:
redis_timeout_ms = 250
redis_max_value_bytes = 4194304
redis_tls = false
warmup_workers = 2
warmup_queue_capacity = 4096

[servers.west]
connect = west.example.net:27015
map = ow_west
max_players = 64
workshop_collection_id = 1234567890

[zones.harbor]
server_id = west
map = ow_west
min_x = -4096
min_y = -2048
max_x = 0
max_y = 2048
```

The `[cache]` section configures only the optional generic fallback, not the
local settings cache. Leave it unchanged for a normal server. Redis settings
are relevant only to an experimental build with
`OW_ENABLE_CACHE_FALLBACK=ON`. Use a distinct `server_id` for each CS2 process;
`connect` must be reachable by clients and must not be `localhost` for remote
players. Zones are rectangular XY coordinates in the CS2 world; use measured
coordinates from your map. Zone-based movement transitions are described
below.

In an experimental Redis build, `provider = redis` makes the plugin issue a
`PING` at load time. If Redis is unavailable, the plugin refuses to load;
there is no automatic memory fallback. The Redis client does not support TLS
and opens a TCP connection for each operation. Do not call it synchronously
from a frame hook.

## 7. Experimental shared-cache behavior

When explicitly enabled, both processes connect to the same Redis instance
and namespace. Keys have the form `redis_key_prefix + logical_key`, such as
`player:<steamid>:settings`. Values store a revision, newline, and payload and
expire after a TTL. Writes use an atomic Redis Lua script so an older revision
does not replace a newer one. `AsyncCacheWarmer` runs outside the game frame
hook and must not call engine APIs.

This cache is neither the source of truth nor a session-ownership mechanism.
Do not use it to authorize balances, permissions, or character ownership. It
is not required for the map, player login, or normal server operation.

## 8. Server transitions and routing

The plugin supports server affinity and manual chat transitions:
`!go <server_id>` (also `!transfer <server_id>`), for example `!go server-02`.
Add to the cluster configuration:

```ini
[routing]
enforce_server_affinity = true
lobby_server_id = lobby
```

`lobby_server_id` must match a `[servers.<id>]` section. Every cluster server
must use the same `[routing]`, `environment`, full `[servers.*]` list, and
PostgreSQL database, with migration `004_player_server_routes.sql` applied.

After Steam authentication, the server looks up the route asynchronously.
First-time players are assigned to the lobby. If a player connects to a
different managed server, the destination is selected only from the local
`[servers.*]` list. On the assigned server, PostgreSQL updates
`last_seen_server_id` and `last_seen_at`. `!go <server_id>` atomically updates
the assignment and sends a CS2 `connect` command only after PostgreSQL
confirms it. This reconnect does not transfer position or runtime character
state.

Routing applies only to servers running this plugin with affinity enabled.
The plugin cannot intercept client commands on unrelated servers. If
PostgreSQL is disabled or the migration is missing, routing fails closed.

Automatic zone transitions use the same rectangular `[zones.*]` areas. When
a living, Steam-authenticated player crosses into another server's configured
zone, the plugin stores the destination in PostgreSQL and reconnects the
client. Checks run on the game thread about four times per second. This
requires `routing.enforce_server_affinity = true`,
`modules.transfer = true`, and a configured zone for the current map. All
zones must share one world coordinate system. The destination map chooses the
spawn point; player position and velocity do not carry over. No transition
occurs if the player is outside all zones or inside ambiguous overlapping
zones assigned to different servers.

For each server, configure a unique ID, client-reachable address and port,
and the correct map. Keep the same destination list on all instances while
setting a different `core.server_id` on each one. The experimental
`adapters.particle_menu` option must be explicitly set to `true` and tested
with your CS2 build; it installs a KHook transmit filter. If unset, the menu
and hook remain disabled.

To manually move a player's assignment back to the lobby, connect to the
database as `ow_migrator` and substitute the SteamID and environment:

```sql
UPDATE ow.player_server_routes
SET server_id = 'lobby', updated_at = now()
WHERE environment = 'production' AND steam_id = 76561198000000000;
```

Only assign a `server_id` present in every server's configuration. If a saved
destination is missing, the plugin logs an error and does not choose another
address.

## 9. Maps and Workshop

Create and validate maps using Counter-Strike 2 Workshop Tools / Hammer. Use
your own map, a community map whose terms permit your use, or properly
licensed materials. Publish maps and shared resources as separate Workshop
packages.

There is no production tool that automatically converts a complete map from
another commercial engine to Source 2 while preserving geometry, materials,
collision, lighting, logic, and gameplay. AI may help create new concepts,
meshes, or textures, but it does not replace scale/material/collision/light
preparation and validation in Hammer. For your own source assets, see
[Sollumz](https://github.com/Sollumz/Sollumz) and
[Blender](https://www.blender.org/), subject to format limitations. Do not
extract or distribute commercial-game maps or assets without the required
rights. See [MAP_AUTHORING.md](./MAP_AUTHORING.md) for practical map guidance.

## 10. Implementation boundaries

The PostgreSQL adapter loads settings in a background worker after Steam
authentication, so login is not blocked. Automatic character selection is
implemented, but saving position/game state on disconnect is not. A complete
transfer coordinator is not implemented; database tables do not imply
gameplay features. Generic LRU/Redis/warmup fallback is excluded from the
standard build. Inventory, economy, health, weapons, vehicles, particle
rendering, and the Workshop pipeline are incomplete. NPCs, audio, dialogue,
quests, and social features are outside the current implementation.

## 11. Build and install the package

From the repository root, run `.\build.ps1 -Reconfigure -RunTests` in
PowerShell. The package is written to
`build-windows-release/package`; normal builds also refresh it.

The package root contains `addons/` and `cfg/`, matching a server's
`game/csgo` directory. Copy and merge these folders into the server's
`game/csgo`. See [`PACKAGE_INSTALL.txt`](./PACKAGE_INSTALL.txt) for Metamod,
PostgreSQL, `OW_PG_CONNINFO`, and enabled-configuration requirements.
