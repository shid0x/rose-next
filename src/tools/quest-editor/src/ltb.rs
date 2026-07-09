//! Phase 3 — `.LTB` language string table (`AStringTable`, `lib_util/classstb2.h`).
//!
//! Conversation node `str_id`s index `3Ddata\Event\ulngtb_con.ltb` via
//! `GetEventString(id) = GetMbcsString(lang+1, id)` — i.e. **row = string id**,
//! col 0 = a key, cols 1..N = per-language display text. Strings are stored as
//! **UTF-16LE** (the engine converts wide→MBCS for display). Layout:
//!
//! ```text
//!   i32 col_cnt
//!   i32 row_cnt
//!   index[row_cnt][col_cnt] (row-major): i32 file_pos, i16 str_len  (6 bytes each)
//!   <string pool> (each cell = str_len **wide chars** = str_len*2 bytes UTF-16LE,
//!                  null-terminated; e.g. "Root" stores as 5 wide chars "Root\0")
//! ```
//!
//! (The header's `Open()` reads the length as a 1-byte `char` and the bytes as
//! MBCS — both bugs; the real files use a 2-byte `short` length and UTF-16LE
//! payloads, verified empirically.) We keep cell bytes **raw** so existing
//! (Korean) strings round-trip byte-for-byte; new strings are encoded UTF-16LE.

use anyhow::{bail, Context, Result};
use std::path::Path;

const INDEX_ENTRY: usize = 6; // i32 file_pos + i16 str_len

#[derive(Debug, Clone)]
pub struct LtbTable {
    pub col_cnt: usize,
    /// `rows[id][col]` raw cell bytes (UTF-16LE); empty cell = no string.
    pub rows: Vec<Vec<Vec<u8>>>,
}

fn rd_i32(b: &[u8], o: usize) -> Result<i32> {
    b.get(o..o + 4)
        .map(|s| i32::from_le_bytes([s[0], s[1], s[2], s[3]]))
        .with_context(|| format!("i32 past EOF at {o}"))
}
fn rd_i16(b: &[u8], o: usize) -> Result<i16> {
    b.get(o..o + 2)
        .map(|s| i16::from_le_bytes([s[0], s[1]]))
        .with_context(|| format!("i16 past EOF at {o}"))
}

/// Encode a `str` as UTF-16LE bytes.
pub fn utf16le(s: &str) -> Vec<u8> {
    s.encode_utf16().flat_map(|u| u.to_le_bytes()).collect()
}

/// Decode UTF-16LE bytes to a `String` (for display/inspection).
pub fn decode_utf16le(b: &[u8]) -> String {
    let units: Vec<u16> = b
        .chunks_exact(2)
        .map(|c| u16::from_le_bytes([c[0], c[1]]))
        .collect();
    String::from_utf16_lossy(&units)
        .trim_end_matches('\0')
        .to_string()
}

impl LtbTable {
    pub fn parse(b: &[u8]) -> Result<LtbTable> {
        let col = rd_i32(b, 0)?;
        let row = rd_i32(b, 4)?;
        if col < 0 || row < 0 || col > 64 || row > 1_000_000 {
            bail!("implausible .LTB dims col={col} row={row}");
        }
        let (col, row) = (col as usize, row as usize);

        let mut index = Vec::with_capacity(row * col);
        let mut pos = 8;
        for _ in 0..row * col {
            let file_pos = rd_i32(b, pos)? as usize;
            let str_len = rd_i16(b, pos + 4)?.max(0) as usize;
            index.push((file_pos, str_len));
            pos += INDEX_ENTRY;
        }

        let mut rows = Vec::with_capacity(row);
        let mut k = 0;
        for _ in 0..row {
            let mut r = Vec::with_capacity(col);
            for _ in 0..col {
                let (fp, len) = index[k];
                k += 1;
                let byte_len = len * 2; // str_len counts wide chars
                let cell = if fp == 0 || len == 0 {
                    Vec::new()
                } else {
                    b.get(fp..fp + byte_len)
                        .with_context(|| format!("string past EOF (pos {fp}, len {byte_len})"))?
                        .to_vec()
                };
                r.push(cell);
            }
            rows.push(r);
        }
        Ok(LtbTable { col_cnt: col, rows })
    }

    pub fn read_file(path: &Path) -> Result<LtbTable> {
        let bytes = std::fs::read(path).with_context(|| format!("reading {}", path.display()))?;
        Self::parse(&bytes)
    }

    /// Serialize (6-byte index entries; strings packed after the index, raw). The
    /// game reads by `file_pos`, so existing cells survive byte-for-byte.
    pub fn to_bytes(&self) -> Vec<u8> {
        let col = self.col_cnt;
        let row = self.rows.len();
        let string_base = 8 + row * col * INDEX_ENTRY;

        let mut strings = Vec::new();
        let mut index: Vec<(u32, i16)> = Vec::with_capacity(row * col);
        for r in &self.rows {
            for c in 0..col {
                let cell = r.get(c).map(Vec::as_slice).unwrap_or(&[]);
                if cell.is_empty() {
                    index.push((0, 0));
                } else {
                    // str_len = wide-char count = bytes / 2.
                    let chars = (cell.len() / 2).min(i16::MAX as usize);
                    let byte_len = chars * 2;
                    let fp = string_base + strings.len();
                    strings.extend_from_slice(&cell[..byte_len]);
                    index.push((fp as u32, chars as i16));
                }
            }
        }

        let mut out = Vec::with_capacity(string_base + strings.len());
        out.extend_from_slice(&(col as i32).to_le_bytes());
        out.extend_from_slice(&(row as i32).to_le_bytes());
        for (fp, len) in &index {
            out.extend_from_slice(&fp.to_le_bytes());
            out.extend_from_slice(&len.to_le_bytes());
        }
        out.extend_from_slice(&strings);
        out
    }

    /// Build a row: `key` in col 0, `text` (null-terminated UTF-16LE) in every
    /// language column (cols 1..N).
    fn fill_row(&self, key: &str, text: &str) -> Vec<Vec<u8>> {
        let term = |s: &str| {
            let mut v = utf16le(s);
            v.extend_from_slice(&[0, 0]); // strings are null-terminated wide
            v
        };
        let mut row = vec![Vec::new(); self.col_cnt];
        if self.col_cnt > 0 {
            row[0] = term(key);
        }
        let encoded = term(text);
        for c in 1..self.col_cnt {
            row[c] = encoded.clone();
        }
        row
    }

    /// Append a string (its own row) and return its id (the row index).
    pub fn append_string(&mut self, key: &str, text: &str) -> usize {
        let id = self.rows.len();
        let row = self.fill_row(key, text);
        self.rows.push(row);
        id
    }

    /// The display text (col 1) of the row whose col 0 == `key`, if present.
    pub fn get(&self, key: &str) -> Option<String> {
        self.rows
            .iter()
            .find(|r| r.first().map(|c| decode_utf16le(c)).as_deref() == Some(key))
            .and_then(|r| r.get(1))
            .map(|c| decode_utf16le(c))
    }

    /// Set the text of the row whose col 0 == `key`, or append a new one.
    /// Returns the row id. Makes re-wiring idempotent (no table growth).
    pub fn set_or_append(&mut self, key: &str, text: &str) -> usize {
        if let Some(id) = self
            .rows
            .iter()
            .position(|r| r.first().map(|c| decode_utf16le(c)).as_deref() == Some(key))
        {
            self.rows[id] = self.fill_row(key, text);
            return id;
        }
        self.append_string(key, text)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ltb_preserves_cells_and_appends_utf16() {
        let mut t = LtbTable {
            col_cnt: 6,
            rows: vec![
                vec![Vec::new(); 6],
                vec![
                    utf16le("EM01"),
                    utf16le("Root"),
                    utf16le("Root"),
                    Vec::new(),
                    Vec::new(),
                    Vec::new(),
                ],
            ],
        };
        let id = t.append_string("QG", "Accept this quest");
        assert_eq!(id, 2);

        let p = LtbTable::parse(&t.to_bytes()).unwrap();
        assert_eq!(p.col_cnt, 6);
        assert_eq!(p.rows.len(), 3);
        // Existing cell preserved byte-exact.
        assert_eq!(decode_utf16le(&p.rows[1][1]), "Root");
        // New row: col 0 key, cols 1..5 the text.
        assert_eq!(decode_utf16le(&p.rows[2][0]), "QG");
        assert_eq!(decode_utf16le(&p.rows[2][1]), "Accept this quest");
        assert_eq!(decode_utf16le(&p.rows[2][5]), "Accept this quest");
        assert!(p.rows[2][/* unused middle stays set */ 3].len() > 0);
    }
}
