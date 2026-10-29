BEGIN;

CREATE SCHEMA IF NOT EXISTS ow;

CREATE TABLE IF NOT EXISTS ow.players (
    steam_id BIGINT PRIMARY KEY CHECK (steam_id > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    data_revision BIGINT NOT NULL DEFAULT 1 CHECK (data_revision > 0)
);

CREATE TABLE IF NOT EXISTS ow.player_settings (
    steam_id BIGINT PRIMARY KEY REFERENCES ow.players (steam_id) ON DELETE CASCADE,
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    settings JSONB NOT NULL DEFAULT '{}'::jsonb,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS ow.characters (
    character_id UUID PRIMARY KEY,
    steam_id BIGINT NOT NULL REFERENCES ow.players (steam_id) ON DELETE CASCADE,
    display_name TEXT NOT NULL CHECK (length(display_name) BETWEEN 1 AND 64),
    state JSONB NOT NULL DEFAULT '{}'::jsonb,
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (character_id, steam_id)
);

CREATE INDEX IF NOT EXISTS characters_by_player
    ON ow.characters (steam_id, created_at);

CREATE TABLE IF NOT EXISTS ow.inventories (
    character_id UUID PRIMARY KEY REFERENCES ow.characters (character_id) ON DELETE CASCADE,
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    items JSONB NOT NULL DEFAULT '[]'::jsonb,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS ow.economy_accounts (
    character_id UUID PRIMARY KEY REFERENCES ow.characters (character_id) ON DELETE CASCADE,
    balance BIGINT NOT NULL DEFAULT 0 CHECK (balance >= 0),
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS ow.vehicles (
    vehicle_id UUID PRIMARY KEY,
    owner_character_id UUID NOT NULL REFERENCES ow.characters (character_id),
    state JSONB NOT NULL DEFAULT '{}'::jsonb,
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS ow.transfers (
    transfer_id UUID PRIMARY KEY,
    idempotency_key TEXT NOT NULL UNIQUE,
    steam_id BIGINT NOT NULL REFERENCES ow.players (steam_id),
    character_id UUID NOT NULL,
    source_server_id TEXT NOT NULL,
    destination_server_id TEXT NOT NULL,
    state TEXT NOT NULL CHECK (state IN (
        'requested', 'validating', 'destination_reserved', 'state_saved',
        'redirecting', 'arriving', 'completed', 'failed', 'expired', 'cancelled'
    )),
    token_hash BYTEA NOT NULL,
    snapshot JSONB NOT NULL,
    data_revision BIGINT NOT NULL CHECK (data_revision > 0),
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    CHECK (source_server_id <> destination_server_id),
    FOREIGN KEY (character_id, steam_id) REFERENCES ow.characters (character_id, steam_id)
);

CREATE UNIQUE INDEX IF NOT EXISTS one_active_transfer_per_player
    ON ow.transfers (steam_id)
    WHERE state IN ('requested', 'validating', 'destination_reserved', 'state_saved', 'redirecting', 'arriving');

CREATE INDEX IF NOT EXISTS transfers_expiry
    ON ow.transfers (expires_at)
    WHERE state NOT IN ('completed', 'failed', 'expired', 'cancelled');

CREATE TABLE IF NOT EXISTS ow.outbox_events (
    event_id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    aggregate_type TEXT NOT NULL,
    aggregate_id TEXT NOT NULL,
    event_type TEXT NOT NULL,
    payload JSONB NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    processed_at TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS outbox_unprocessed
    ON ow.outbox_events (event_id)
    WHERE processed_at IS NULL;

COMMIT;
