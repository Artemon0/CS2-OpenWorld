-- Step 1: connect to the PostgreSQL "postgres" database as an administrator.
-- Create the roles, then set separate strong passwords in pgAdmin's role properties.
-- These roles intentionally have no password until you set one; do not commit secrets.

CREATE ROLE ow_migrator LOGIN;
CREATE ROLE ow_app LOGIN;
