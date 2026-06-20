//! QSD (quest data) structural codec.
//!
//! A `.QSD` file is a flat little-endian binary tree that the game loads in
//! `CQuestDATA::LoadDATA` / `CQuestTRIGGER::Load`
//! (`src/common/shared/io_quest.cpp`). Layout:
//!
//! ```text
//! u32  size_field        (read by the game but never used; preserved verbatim)
//! u32  pattern_count
//! str  description        (i16 length prefix + bytes; game fseeks over it)
//! repeat pattern_count times:
//!   u32 trigger_count
//!   str pattern_name      (i16 length prefix + bytes; game fseeks over it)
//!   repeat trigger_count times:
//!     u8  check_next
//!     u32 condition_count
//!     u32 reward_count
//!     str trigger_name    (i16 length prefix + bytes; INCLUDES trailing NUL)
//!     repeat condition_count times: entity
//!     repeat reward_count    times: entity
//!
//! entity:
//!   u32 size              (TOTAL size including this 8-byte header)
//!   i32 type
//!   u8[size - 8] payload  (raw struct bytes, INCLUDING C struct padding)
//! ```
//!
//! ## Fidelity rule
//!
//! The original retail `.QSD` files contain MSVC uninitialized-memory fill
//! bytes (`0xCC`/`0xCD`) baked into struct padding — i.e. some payload bytes
//! are non-deterministic garbage that happened to be serialized. To guarantee a
//! byte-exact round-trip we therefore keep every condition/reward payload as a
//! **raw blob** and re-emit it verbatim. Typed interpretation of those payloads
//! (for *generating* new quests) is a separate concern layered on top later; it
//! must never be required for round-trip fidelity.
//!
//! Counts and string-length prefixes are intentionally *re-derived* on write
//! (not stored) so the in-memory model can't drift out of sync with what it
//! serializes. The one field we can't derive — `size_field` — is preserved.

use std::fs;
use std::path::Path;

use anyhow::{bail, Context, Result};

/// A parsed `.QSD` file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct QsdFile {
    /// Leading u32 header. The game reads it into `ulSize` and never uses it;
    /// its meaning is unknown (it is *not* the file size). Preserved verbatim
    /// for round-trip fidelity. New files should copy a value observed in the
    /// retail data (see PROGRESS.md open questions).
    pub size_field: u32,
    /// File-level description string (raw bytes, no length prefix). The game
    /// skips over it; we keep it so we can rewrite it identically.
    pub description: Vec<u8>,
    pub patterns: Vec<Pattern>,
    /// Any bytes left over after the last pattern. Expected to be empty, but
    /// stored so a malformed/padded file still round-trips exactly.
    pub trailing: Vec<u8>,
}

/// A "pattern" — a named group of triggers. The game only uses it as a
/// container; trigger names (not pattern names) are the globally hashed keys.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Pattern {
    /// Pattern name (raw bytes, no length prefix). Skipped by the game.
    pub name: Vec<u8>,
    pub triggers: Vec<Trigger>,
}

/// A trigger: a list of conditions that must all pass, and the rewards/actions
/// applied when they do.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Trigger {
    pub check_next: u8,
    /// Trigger name as stored, **including the trailing NUL byte** that the
    /// retail files carry (the game allocs `len+1` and reads `len` bytes).
    pub name: Vec<u8>,
    pub conditions: Vec<Entity>,
    pub rewards: Vec<Entity>,
}

/// A single condition or reward block. `payload` is the struct body *after* the
/// 8-byte `{size, type}` header, kept raw (see module-level fidelity rule).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entity {
    pub etype: i32,
    pub payload: Vec<u8>,
}

impl Entity {
    /// The type id the game actually dispatches on (`iType & 0xffff`).
    pub fn type_id(&self) -> i32 {
        self.etype & 0xffff
    }

    /// Total on-disk size of this entity, including the 8-byte header.
    pub fn encoded_size(&self) -> usize {
        self.payload.len() + 8
    }
}

// --- reading -----------------------------------------------------------------

struct Reader<'a> {
    buf: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(buf: &'a [u8]) -> Self {
        Self { buf, pos: 0 }
    }

    fn remaining(&self) -> usize {
        self.buf.len() - self.pos
    }

    fn take(&mut self, n: usize) -> Result<&'a [u8]> {
        if self.pos + n > self.buf.len() {
            bail!(
                "unexpected end of file: needed {n} bytes at offset {} but only {} remain",
                self.pos,
                self.remaining()
            );
        }
        let s = &self.buf[self.pos..self.pos + n];
        self.pos += n;
        Ok(s)
    }

    fn u8(&mut self) -> Result<u8> {
        Ok(self.take(1)?[0])
    }

    fn u32(&mut self) -> Result<u32> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }

    fn i32(&mut self) -> Result<i32> {
        Ok(self.u32()? as i32)
    }

    fn i16(&mut self) -> Result<i16> {
        Ok(i16::from_le_bytes(self.take(2)?.try_into().unwrap()))
    }

    /// Reads an i16 length prefix followed by that many raw bytes.
    fn str_bytes(&mut self) -> Result<Vec<u8>> {
        let len = self.i16()?;
        if len < 0 {
            bail!("negative string length {len} at offset {}", self.pos - 2);
        }
        Ok(self.take(len as usize)?.to_vec())
    }
}

impl QsdFile {
    pub fn parse(buf: &[u8]) -> Result<Self> {
        let mut r = Reader::new(buf);

        let size_field = r.u32()?;
        let pattern_count = r.u32()?;
        let description = r.str_bytes()?;

        let mut patterns = Vec::with_capacity(pattern_count as usize);
        for p in 0..pattern_count {
            let trigger_count = r.u32()?;
            let name = r.str_bytes()?;
            let mut triggers = Vec::with_capacity(trigger_count as usize);
            for t in 0..trigger_count {
                triggers.push(
                    Trigger::parse(&mut r)
                        .with_context(|| format!("pattern {p}, trigger {t}"))?,
                );
            }
            patterns.push(Pattern { name, triggers });
        }

        let trailing = r.take(r.remaining())?.to_vec();

        Ok(QsdFile {
            size_field,
            description,
            patterns,
            trailing,
        })
    }

    pub fn read_file(path: &Path) -> Result<Self> {
        let buf = fs::read(path).with_context(|| format!("reading {}", path.display()))?;
        Self::parse(&buf).with_context(|| format!("parsing {}", path.display()))
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut out = Vec::new();
        out.extend_from_slice(&self.size_field.to_le_bytes());
        out.extend_from_slice(&(self.patterns.len() as u32).to_le_bytes());
        write_str(&mut out, &self.description);
        for p in &self.patterns {
            out.extend_from_slice(&(p.triggers.len() as u32).to_le_bytes());
            write_str(&mut out, &p.name);
            for t in &p.triggers {
                t.write(&mut out);
            }
        }
        out.extend_from_slice(&self.trailing);
        out
    }

    pub fn write_file(&self, path: &Path) -> Result<()> {
        fs::write(path, self.to_bytes()).with_context(|| format!("writing {}", path.display()))
    }
}

impl Trigger {
    fn parse(r: &mut Reader) -> Result<Self> {
        let check_next = r.u8()?;
        let cond_count = r.u32()?;
        let rewd_count = r.u32()?;
        let name = r.str_bytes()?;

        let mut conditions = Vec::with_capacity(cond_count as usize);
        for c in 0..cond_count {
            conditions
                .push(Entity::parse(r).with_context(|| format!("condition {c}"))?);
        }
        let mut rewards = Vec::with_capacity(rewd_count as usize);
        for rw in 0..rewd_count {
            rewards.push(Entity::parse(r).with_context(|| format!("reward {rw}"))?);
        }

        Ok(Trigger {
            check_next,
            name,
            conditions,
            rewards,
        })
    }

    fn write(&self, out: &mut Vec<u8>) {
        out.push(self.check_next);
        out.extend_from_slice(&(self.conditions.len() as u32).to_le_bytes());
        out.extend_from_slice(&(self.rewards.len() as u32).to_le_bytes());
        write_str(out, &self.name);
        for c in &self.conditions {
            c.write(out);
        }
        for rw in &self.rewards {
            rw.write(out);
        }
    }
}

impl Entity {
    fn parse(r: &mut Reader) -> Result<Self> {
        let size = r.u32()?;
        let etype = r.i32()?;
        if size < 8 {
            bail!("entity size {size} is smaller than the 8-byte header");
        }
        let payload = r.take((size - 8) as usize)?.to_vec();
        Ok(Entity { etype, payload })
    }

    fn write(&self, out: &mut Vec<u8>) {
        out.extend_from_slice(&(self.encoded_size() as u32).to_le_bytes());
        out.extend_from_slice(&self.etype.to_le_bytes());
        out.extend_from_slice(&self.payload);
    }
}

fn write_str(out: &mut Vec<u8>, s: &[u8]) {
    out.extend_from_slice(&(s.len() as i16).to_le_bytes());
    out.extend_from_slice(s);
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A hand-built minimal file exercises every structural element and proves
    /// parse(to_bytes()) is the identity.
    #[test]
    fn synthetic_round_trip() {
        let file = QsdFile {
            size_field: 12,
            description: b"desc".to_vec(),
            patterns: vec![Pattern {
                name: b"pat".to_vec(),
                triggers: vec![Trigger {
                    check_next: 1,
                    name: b"trig\0".to_vec(),
                    conditions: vec![Entity {
                        etype: 0,
                        // payload includes deliberate "garbage" padding bytes
                        payload: vec![0xA9, 0x13, 0x00, 0x00],
                    }],
                    rewards: vec![
                        Entity {
                            etype: 31,
                            payload: vec![0xCC, 0xCD, 0x01, 0x02],
                        },
                        Entity {
                            etype: 5,
                            payload: vec![],
                        },
                    ],
                }],
            }],
            trailing: vec![],
        };

        let bytes = file.to_bytes();
        let reparsed = QsdFile::parse(&bytes).expect("parse");
        assert_eq!(file, reparsed);
        assert_eq!(bytes, reparsed.to_bytes());
    }

    #[test]
    fn rejects_truncated_entity() {
        // size says 100 bytes but the buffer ends — must error, not panic.
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&12u32.to_le_bytes()); // size_field
        bytes.extend_from_slice(&1u32.to_le_bytes()); // pattern_count
        bytes.extend_from_slice(&0i16.to_le_bytes()); // description len
        bytes.extend_from_slice(&1u32.to_le_bytes()); // trigger_count
        bytes.extend_from_slice(&0i16.to_le_bytes()); // pattern name len
        bytes.push(0); // check_next
        bytes.extend_from_slice(&1u32.to_le_bytes()); // cond_count
        bytes.extend_from_slice(&0u32.to_le_bytes()); // rewd_count
        bytes.extend_from_slice(&0i16.to_le_bytes()); // trigger name len
        bytes.extend_from_slice(&100u32.to_le_bytes()); // entity size = 100
        bytes.extend_from_slice(&0i32.to_le_bytes()); // entity type
        assert!(QsdFile::parse(&bytes).is_err());
    }
}
