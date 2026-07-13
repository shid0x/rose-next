-- Monster Inspector (skill 7001, client-only window skill) becomes a default
-- basic skill for newly created characters.
--
-- The skills jsonb is a positional array mapped 1:1 to skill slots
-- (tagSkillAbility::m_nSkillINDEX); the appended entry lands in slot 6, which
-- is on skill page 0 ("basic" page, slots 0-29) and therefore survives
-- Reward_InitSKILL skill resets (those only clear pages 1-3).
--
-- Existing characters are NOT modified here; grant manually with the GM
-- command "/add skill 7001" if wanted.
ALTER TABLE "character"
    ALTER COLUMN skills SET DEFAULT '[11, 12, 16, 19, 20, 21, 7001]'::jsonb;
