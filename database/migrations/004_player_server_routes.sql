BEGIN;

CREATE TABLE IF NOT EXISTS ow.player_server_routes (
    environment TEXT NOT NULL CHECK (length(environment) BETWEEN 1 AND 64),
    steam_id BIGINT NOT NULL REFERENCES ow.players (steam_id) ON DELETE CASCADE,
    server_id TEXT NOT NULL CHECK (length(server_id) BETWEEN 1 AND 64),
    last_seen_server_id TEXT CHECK (
        last_seen_server_id IS NULL OR length(last_seen_server_id) BETWEEN 1 AND 64
    ),
    last_seen_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (environment, steam_id)
);

CREATE OR REPLACE FUNCTION ow.resolve_player_server_route(
    p_environment TEXT,
    p_steam_id BIGINT,
    p_current_server_id TEXT,
    p_lobby_server_id TEXT
)
RETURNS TEXT
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, ow, pg_temp
AS $$
DECLARE
    assigned_server_id TEXT;
BEGIN
    IF p_environment IS NULL OR length(p_environment) NOT BETWEEN 1 AND 64 OR
       p_steam_id IS NULL OR p_steam_id <= 0 OR
       p_current_server_id IS NULL OR length(p_current_server_id) NOT BETWEEN 1 AND 64 OR
       p_lobby_server_id IS NULL OR length(p_lobby_server_id) NOT BETWEEN 1 AND 64 THEN
        RAISE EXCEPTION 'Invalid player route parameters';
    END IF;

    INSERT INTO players (steam_id)
    VALUES (p_steam_id)
    ON CONFLICT (steam_id) DO NOTHING;

    PERFORM 1
    FROM players
    WHERE steam_id = p_steam_id
    FOR UPDATE;

    INSERT INTO player_server_routes (environment, steam_id, server_id)
    VALUES (p_environment, p_steam_id, p_lobby_server_id)
    ON CONFLICT (environment, steam_id) DO NOTHING;

    UPDATE player_server_routes
    SET last_seen_server_id = p_current_server_id,
        last_seen_at = now(),
        updated_at = now()
    WHERE environment = p_environment
      AND steam_id = p_steam_id
      AND server_id = p_current_server_id;

    SELECT server_id INTO assigned_server_id
    FROM player_server_routes
    WHERE environment = p_environment
      AND steam_id = p_steam_id;

    RETURN assigned_server_id;
END;
$$;

CREATE OR REPLACE FUNCTION ow.assign_player_server_route(
    p_environment TEXT,
    p_steam_id BIGINT,
    p_destination_server_id TEXT
)
RETURNS TEXT
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, ow, pg_temp
AS $$
DECLARE
    assigned_server_id TEXT;
BEGIN
    IF p_environment IS NULL OR length(p_environment) NOT BETWEEN 1 AND 64 OR
       p_steam_id IS NULL OR p_steam_id <= 0 OR
       p_destination_server_id IS NULL OR length(p_destination_server_id) NOT BETWEEN 1 AND 64 THEN
        RAISE EXCEPTION 'Invalid player route assignment parameters';
    END IF;

    PERFORM 1
    FROM players
    WHERE steam_id = p_steam_id
    FOR UPDATE;

    UPDATE player_server_routes
    SET server_id = p_destination_server_id,
        updated_at = now()
    WHERE environment = p_environment
      AND steam_id = p_steam_id
    RETURNING server_id INTO assigned_server_id;

    IF assigned_server_id IS NULL THEN
        RAISE EXCEPTION 'Player route does not exist; resolve it before assigning a server';
    END IF;

    RETURN assigned_server_id;
END;
$$;

REVOKE EXECUTE ON FUNCTION ow.resolve_player_server_route(TEXT, BIGINT, TEXT, TEXT) FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION ow.assign_player_server_route(TEXT, BIGINT, TEXT) FROM PUBLIC;

COMMIT;
