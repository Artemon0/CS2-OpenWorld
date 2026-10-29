# PostgreSQL schema and migrations

PostgreSQL is the durable source of truth; an embedded database is not planned. Apply migrations in numeric order and never edit a migration that has already been applied. Add a new migration for each future schema change. SQL bootstrap instructions for pgAdmin are in the root [SETUP.md](../../SETUP.md).

Run the bootstrap scripts in this order:

1. `database/bootstrap/001_create_roles_and_database.sql`: while connected to `postgres` as an administrator, create `ow_migrator` and `ow_app`. Replace the sample passwords; keep them consistent with the server environment.
2. `database/bootstrap/002_create_database.sql`: execute the single `CREATE DATABASE` statement separately, because PostgreSQL does not permit it in a transaction block.
3. Connect to database `openworld` as `ow_migrator`, then run [001_core.sql](./001_core.sql), [002_application_functions.sql](./002_application_functions.sql), and `database/bootstrap/003_app_permissions.sql` in that order.

The schema includes player, character, settings, inventory, economy, vehicle, transfer, and outbox tables. The plugin has a runtime-loaded libpq adapter with parameterized character/state/settings/economy operations. Player join/leave persistence, a migration runner, and live gameplay wiring are not implemented. The application role is deliberately not the owner: the SQL functions use `SECURITY DEFINER`, and the plugin receives only read access to required tables plus execute access to the functions.
