-- Run this as a single query in pgAdmin Query Tool or psql after migrations 001 and 002.
-- Replace the Steam ID and character name. This creates a fresh character each time.

WITH created AS (
    SELECT ow.create_character(76561198000000000, 'Test Character') AS character_id
),
funded AS (
    SELECT created.character_id, transaction.new_balance
    FROM created
    CROSS JOIN LATERAL ow.apply_money_transaction(
        created.character_id,
        'quickstart:initial-cash:' || gen_random_uuid()::text,
        1000,
        'Initial test balance'
    ) AS transaction
)
SELECT character.character_id,
       character.steam_id,
       character.display_name,
       character.revision AS character_revision,
       account.balance,
       account.revision AS account_revision
FROM funded
JOIN ow.characters AS character USING (character_id)
JOIN ow.economy_accounts AS account USING (character_id);
