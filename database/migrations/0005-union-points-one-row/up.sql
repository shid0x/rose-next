-- One union_points row per character (2026-11-07).
--
-- Nothing created these rows: character creation never inserted one, and the
-- game server's save only ran an UPDATE, which changes nothing when the row is
-- missing -- so union points were never saved. The save now updates the row or
-- inserts it (and does not rely on this constraint, so a database without it
-- still saves). A row made by hand in the meantime may have been duplicated;
-- the old UPDATE wrote every copy alike, so the newest is kept.
DELETE FROM "union_points" a
    USING "union_points" b
    WHERE a."character_id" = b."character_id" AND a."id" < b."id";

ALTER TABLE "union_points"
    ADD CONSTRAINT "union_points_character_id_key" UNIQUE ("character_id");
