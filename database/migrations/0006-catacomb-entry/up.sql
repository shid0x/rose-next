-- The Karkia catacombs' daily registration (2026-11-09,
-- doc/karkia-catacombs-implementation-brief.md, section 4).
--
-- The toll is payable once a day and buys one opening. A character's row is the
-- last payment: the day (the server's local calendar day number) and the opening
-- of that day (0-11, one every two hours). The game server writes it at the
-- payment, not at logout, so a crash or a restart can neither allow a second
-- payment that day nor lock out someone who has paid; it reads it at login.
-- Without this table the server still runs the catacombs, from memory: the
-- records are then lost at a restart.
CREATE TABLE IF NOT EXISTS "catacomb_entry" (
    "character_id" integer PRIMARY KEY REFERENCES "character" ("id") ON DELETE CASCADE,
    "day" integer NOT NULL,
    "opening" integer NOT NULL
);
