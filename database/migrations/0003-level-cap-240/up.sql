-- Level cap raised from 180 to 240 (Rose::GameStaticConfig::MAX_LEVEL) so the
-- imported Oro planet content, whose monsters run to level 250, is reachable.
--
-- Raising the cap past 189 activates the previously dead 5-term branch of
-- CCal::Get_NeedRawEXP -- (L-90)(L-120)(L-60)(L-170)(L-188) -- which crosses
-- int32 at level 215:
--
--     level 214  ->  2,053,507,456   (fits)
--     level 215  ->  2,236,359,375   (does not)
--     level 240  -> 11,793,600,000
--
-- A character's in-progress EXP is bounded by their current level's requirement,
-- so from level 215 the save UPDATE would be rejected by Postgres. The in-memory
-- field (tagGrowAbility::m_lEXP) has always been __int64; only the column and the
-- load in gs_threadsql.cpp were narrower.
--
-- "level" stays smallint -- 240 fits, and the only constraint on it is
-- character_level_positive CHECK (level > 0).
ALTER TABLE "character"
    ALTER COLUMN "exp" TYPE bigint;
