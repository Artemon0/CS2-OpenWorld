-- Step 5: connect to the openworld database as ow_migrator after both migrations.

GRANT CONNECT ON DATABASE openworld TO ow_app;
GRANT USAGE ON SCHEMA ow TO ow_app;

REVOKE ALL ON SCHEMA ow FROM PUBLIC;
GRANT SELECT ON ow.characters, ow.inventories, ow.economy_accounts, ow.player_settings TO ow_app;

REVOKE EXECUTE ON ALL FUNCTIONS IN SCHEMA ow FROM PUBLIC;
GRANT EXECUTE ON FUNCTION ow.create_character(BIGINT, TEXT) TO ow_app;
GRANT EXECUTE ON FUNCTION ow.save_character_state(UUID, BIGINT, JSONB) TO ow_app;
GRANT EXECUTE ON FUNCTION ow.save_player_settings(BIGINT, BIGINT, JSONB) TO ow_app;
GRANT EXECUTE ON FUNCTION ow.apply_money_transaction(UUID, TEXT, BIGINT, TEXT) TO ow_app;

ALTER DEFAULT PRIVILEGES FOR ROLE ow_migrator IN SCHEMA ow
    REVOKE EXECUTE ON FUNCTIONS FROM PUBLIC;
