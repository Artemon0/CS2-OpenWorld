BEGIN;

CREATE OR REPLACE FUNCTION ow.save_inventory(
    p_character_id UUID,
    p_expected_revision BIGINT,
    p_items JSONB
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
       p_items IS NULL OR jsonb_typeof(p_items) <> 'array' THEN
        RAISE EXCEPTION 'Invalid character revision or inventory';
    END IF;

    UPDATE inventories
    SET items = p_items,
        revision = revision + 1,
        updated_at = now()
    WHERE character_id = p_character_id
      AND revision = p_expected_revision
    RETURNING revision INTO new_revision;

    IF new_revision IS NULL THEN
        RAISE EXCEPTION 'Inventory is missing or its revision changed';
    END IF;
    RETURN new_revision;
END;
$$;

COMMIT;
