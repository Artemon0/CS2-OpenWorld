-- Step 2: while still connected to "postgres", execute this statement by itself.
-- CREATE DATABASE cannot run inside a transaction block.

CREATE DATABASE openworld OWNER ow_migrator;
