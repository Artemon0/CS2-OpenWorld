BEGIN;

CREATE TABLE IF NOT EXISTS ow.economy_transactions (
    transaction_id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    idempotency_key TEXT NOT NULL UNIQUE,
    character_id UUID NOT NULL REFERENCES ow.characters (character_id),
    amount BIGINT NOT NULL CHECK (amount <> 0),
    balance_after BIGINT NOT NULL CHECK (balance_after >= 0),
    reason TEXT NOT NULL CHECK (length(reason) BETWEEN 1 AND 128),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS economy_transactions_by_character
    ON ow.economy_transactions (character_id, transaction_id DESC);

CREATE OR REPLACE FUNCTION ow.create_character(
    p_steam_id BIGINT,
    p_display_name TEXT
)
RETURNS UUID
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, ow, pg_temp
AS $$
DECLARE
    new_character_id UUID := gen_random_uuid();
BEGIN
    IF p_steam_id IS NULL OR p_steam_id <= 0 THEN
        RAISE EXCEPTION 'Steam ID must be positive';
    END IF;
    IF p_display_name IS NULL OR length(btrim(p_display_name)) NOT BETWEEN 1 AND 64 THEN
        RAISE EXCEPTION 'Character name must contain 1 to 64 characters';
    END IF;

    INSERT INTO players (steam_id)
    VALUES (p_steam_id)
    ON CONFLICT (steam_id) DO NOTHING;

    INSERT INTO characters (character_id, steam_id, display_name)
    VALUES (new_character_id, p_steam_id, btrim(p_display_name));

    INSERT INTO inventories (character_id) VALUES (new_character_id);
    INSERT INTO economy_accounts (character_id) VALUES (new_character_id);

    RETURN new_character_id;
END;
$$;

CREATE OR REPLACE FUNCTION ow.save_character_state(
    p_character_id UUID,
    p_expected_revision BIGINT,
    p_state JSONB
)
RETURNS BIGINT
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, ow, pg_temp
AS $$
DECLARE
    new_revision BIGINT;
BEGIN
    IF p_character_id IS NULL OR p_expected_revision IS NULL OR p_expected_revision <= 0 OR
       p_state IS NULL OR jsonb_typeof(p_state) <> 'object' THEN
        RAISE EXCEPTION 'Invalid character revision or state';
    END IF;

    UPDATE characters
    SET state = p_state,
        revision = revision + 1,
        updated_at = now()
    WHERE character_id = p_character_id
      AND revision = p_expected_revision
    RETURNING revision INTO new_revision;

    IF new_revision IS NULL THEN
        RAISE EXCEPTION 'Character is missing or its revision changed';
    END IF;
    RETURN new_revision;
END;
$$;

CREATE OR REPLACE FUNCTION ow.save_player_settings(
    p_steam_id BIGINT,
    p_expected_revision BIGINT,
    p_settings JSONB
)
RETURNS BIGINT
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, ow, pg_temp
AS $$
DECLARE
    new_revision BIGINT;
BEGIN
    IF p_steam_id IS NULL OR p_steam_id <= 0 OR p_expected_revision IS NULL OR p_expected_revision < 0 OR
       p_settings IS NULL OR
       jsonb_typeof(p_settings) <> 'object' THEN
        RAISE EXCEPTION 'Invalid Steam ID, revision, or settings';
    END IF;

    INSERT INTO players (steam_id)
    VALUES (p_steam_id)
    ON CONFLICT (steam_id) DO NOTHING;

    IF p_expected_revision = 0 THEN
        INSERT INTO player_settings (steam_id, revision, settings)
        VALUES (p_steam_id, 1, p_settings)
        ON CONFLICT (steam_id) DO NOTHING
        RETURNING revision INTO new_revision;
    ELSE
        UPDATE player_settings
        SET settings = p_settings,
            revision = revision + 1,
            updated_at = now()
        WHERE steam_id = p_steam_id
          AND revision = p_expected_revision
        RETURNING revision INTO new_revision;
    END IF;

    IF new_revision IS NULL THEN
        RAISE EXCEPTION 'Settings revision changed; reload settings before saving';
    END IF;
    RETURN new_revision;
END;
$$;

CREATE OR REPLACE FUNCTION ow.apply_money_transaction(
    p_character_id UUID,
    p_idempotency_key TEXT,
    p_amount BIGINT,
    p_reason TEXT
)
RETURNS TABLE (new_balance BIGINT, was_duplicate BOOLEAN)
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, ow, pg_temp
AS $$
DECLARE
    prior_transaction economy_transactions%ROWTYPE;
    resulting_balance BIGINT;
BEGIN
    IF p_character_id IS NULL OR p_idempotency_key IS NULL OR
       length(p_idempotency_key) NOT BETWEEN 1 AND 128 OR p_amount IS NULL OR p_amount = 0 OR
       p_reason IS NULL OR length(btrim(p_reason)) NOT BETWEEN 1 AND 128 THEN
        RAISE EXCEPTION 'Invalid idempotency key, amount, or transaction reason';
    END IF;

    PERFORM pg_advisory_xact_lock(hashtextextended(p_idempotency_key, 0));

    SELECT * INTO prior_transaction
    FROM economy_transactions
    WHERE idempotency_key = p_idempotency_key;

    IF FOUND THEN
        IF prior_transaction.character_id <> p_character_id OR
           prior_transaction.amount <> p_amount OR prior_transaction.reason <> btrim(p_reason) THEN
            RAISE EXCEPTION 'Idempotency key was reused for a different transaction';
        END IF;
        RETURN QUERY SELECT prior_transaction.balance_after, TRUE;
        RETURN;
    END IF;

    UPDATE economy_accounts
    SET balance = balance + p_amount,
        revision = revision + 1,
        updated_at = now()
    WHERE character_id = p_character_id
      AND balance + p_amount >= 0
    RETURNING balance INTO resulting_balance;

    IF resulting_balance IS NULL THEN
        RAISE EXCEPTION 'Account is missing or the transaction would make balance negative';
    END IF;

    INSERT INTO economy_transactions (idempotency_key, character_id, amount, balance_after, reason)
    VALUES (p_idempotency_key, p_character_id, p_amount, resulting_balance, btrim(p_reason));

    RETURN QUERY SELECT resulting_balance, FALSE;
END;
$$;

COMMIT;
