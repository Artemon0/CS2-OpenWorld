# PostgreSQL schema and migrations

PostgreSQL is the durable source of truth; an embedded database is not planned. Apply migrations in numeric order and never edit a migration that has already been applied. Add a new migration for each future schema change. SQL bootstrap instructions for pgAdmin are in the root [SETUP.md](../../SETUP.md).

Run the bootstrap scripts in this order:

1. `database/bootstrap/001_create_roles_and_database.sql`: while connected to `postgres` as an administrator, create `ow_migrator` and `ow_app`. Replace the sample passwords; keep them consistent with the server environment.
2. `database/bootstrap/002_create_database.sql`: execute the single `CREATE DATABASE` statement separately, because PostgreSQL does not permit it in a transaction block.
3. Connect to database `openworld` as `ow_migrator`, then run [001_core.sql](./001_core.sql), [002_application_functions.sql](./002_application_functions.sql), [003_inventory_functions.sql](./003_inventory_functions.sql), [004_player_server_routes.sql](./004_player_server_routes.sql), and `database/bootstrap/003_app_permissions.sql` in that order.

If migrations 001-003 were already applied, apply only migration 004 and rerun `database/bootstrap/003_app_permissions.sql` as `ow_migrator`.

The schema includes player, character, settings, inventory, economy, vehicle, transfer, route, and outbox tables. The runtime-loaded libpq adapter supports parameterized character/state/settings/economy operations and revision-checked inventory loads/saves; versioned `IInventoryService` and `IEconomyService` modules are registered by the plugin. Authenticated player sessions load character data on a worker thread; `!character` selects/creates characters, `!inventory` displays the stored JSON snapshot, and `!balance` displays the wallet. Server-route lookups and assignments also run on a worker thread and are persisted in PostgreSQL. Inventory saves use compare-and-swap revisions; callers must reload after a stale-write conflict. Service methods are synchronous, so the current plugin invokes them from workers rather than the game thread. Item pickup/use/crafting, inventory mutations from gameplay, character state saving on disconnect, and a migration runner are not implemented. The application role is deliberately not the owner: the SQL functions use `SECURITY DEFINER`, and the plugin receives only read access to required tables plus execute access to the functions.
