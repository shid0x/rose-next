//! Table text for *any* ROSE dump, not just ours.
//!
//! roselib decodes every string as EUC-KR (`rose-lib/src/io/reader.rs`) and
//! reads only the modern `NRST01` / `ITST01` string tables. Pointed at Jrose
//! that produced two failures that looked like one: its Shift-JIS bytes came
//! out as random Hangul (windows-949 accepts almost any lead byte, so there is
//! no decode error to notice), and its legacy `I_NUM` / `N_NUM` string tables
//! were rejected outright, so every name fell back to the raw table column --
//! also in the wrong codec. On screen that is a row of boxes with stray ASCII
//! punctuation.
//!
//! This module reads the bytes itself and decodes them with a codec chosen per
//! file: `Codec::Auto` scores a Japanese and a Korean reading of the non-ASCII
//! cells and keeps the one that produces that script. The scoring is on
//! *script*, not decode errors, because both codecs accept each other's bytes
//! nearly error-free: Korean read as Shift-JIS turns into half-width katakana
//! (0xB0-0xDF are single bytes there), Japanese read as EUC-KR into Hangul.
use std::collections::HashMap;

use anyhow::{bail, Context, Result};
use encoding_rs::{EUC_KR, SHIFT_JIS, WINDOWS_1252};

#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Codec {
    /// Pick per file from the text itself.
    #[default]
    Auto,
    /// windows-1252: our own data (English with the odd curly apostrophe).
    Western,
    /// windows-949 / EUC-KR: retail and the Korean private-server dumps.
    Korean,
    /// Shift-JIS: Jrose.
    Japanese,
}

impl Codec {
    pub const ALL: [Codec; 4] = [Codec::Auto, Codec::Western, Codec::Korean, Codec::Japanese];

    pub fn label(self) -> &'static str {
        match self {
            Codec::Auto => "Auto",
            Codec::Western => "Western",
            Codec::Korean => "Korean",
            Codec::Japanese => "Japanese",
        }
    }

    pub fn parse(s: &str) -> Option<Codec> {
        match s.to_ascii_lowercase().as_str() {
            "auto" => Some(Codec::Auto),
            "western" | "latin" | "en" => Some(Codec::Western),
            "korean" | "ko" | "euc-kr" | "cp949" => Some(Codec::Korean),
            "japanese" | "ja" | "sjis" | "shift-jis" | "cp932" => Some(Codec::Japanese),
            _ => None,
        }
    }

    /// Decode one string. `Auto` must be resolved first; it falls back to
    /// Western so a stray call never panics.
    pub fn decode(self, b: &[u8]) -> String {
        let enc = match self {
            Codec::Auto | Codec::Western => WINDOWS_1252,
            Codec::Korean => EUC_KR,
            Codec::Japanese => SHIFT_JIS,
        };
        enc.decode_without_bom_handling(b).0.into_owned()
    }

    /// `Auto` -> the codec the sample reads best in; anything else unchanged.
    pub fn resolve<'a>(self, sample: impl IntoIterator<Item = &'a [u8]>) -> Codec {
        match self {
            Codec::Auto => detect(sample),
            fixed => fixed,
        }
    }
}

/// Byte classes first, decoded script second. Decoding alone cannot tell the
/// two apart: windows-949 (what encoding_rs calls EUC-KR) maps nearly every
/// Shift-JIS pair to *some* Hangul, so Japanese text scores as Korean too. The
/// lead bytes do separate them -- kana and level-1 kanji sit on leads
/// 0x81-0x9F, which proper Korean text never uses, while Hangul sits on
/// 0xB0-0xC8 with trails >= 0xA1, which Shift-JIS reads as single-byte
/// half-width katakana. Only 0xE0-0xEF leads with high trails are ambiguous
/// (level-2 kanji vs hanja), and those decide nothing.
fn detect<'a>(sample: impl IntoIterator<Item = &'a [u8]>) -> Codec {
    let (mut sj, mut kr, mut we) = (0i64, 0i64, 0i64);
    let (mut ja, mut ko) = (0i64, 0i64);
    let mut any = false;
    for cell in sample {
        if cell.is_ascii() {
            continue;
        }
        any = true;
        let mut i = 0;
        while i < cell.len() {
            let b = cell[i];
            if b < 0x80 {
                i += 1;
                continue;
            }
            let t = cell.get(i + 1).copied().unwrap_or(0);
            let after = cell.get(i + 2).copied().unwrap_or(0);
            // A lone high byte between ASCII is windows-1252 punctuation or an
            // accented letter ("Fafnir\x92s"); CJK text comes in runs.
            if t < 0x80 && after < 0x80 {
                we += 1;
                i += 1;
                continue;
            }
            let sjis_trail = (0x40..=0x7E).contains(&t) || (0x80..=0xFC).contains(&t);
            let euc_trail = (0xA1..=0xFE).contains(&t);
            if (0x81..=0x9F).contains(&b) && sjis_trail {
                sj += 1;
                i += 2;
            } else if (0xE0..=0xEF).contains(&b) && sjis_trail && !euc_trail {
                sj += 1;
                i += 2;
            } else if (0xA1..=0xDF).contains(&b) && euc_trail {
                kr += 1;
                i += 2;
            } else if b >= 0xF0 && euc_trail {
                kr += 1;
                i += 2;
            } else {
                i += 1; // ambiguous or a stray byte: no vote
            }
        }
        ja += score(SHIFT_JIS, cell, Script::Japanese);
        ko += score(EUC_KR, cell, Script::Korean);
    }
    if !any || we > sj + kr {
        return Codec::Western;
    }
    if sj != kr {
        return if sj > kr { Codec::Japanese } else { Codec::Korean };
    }
    if ja <= 0 && ko <= 0 {
        Codec::Western
    } else if ja >= ko {
        Codec::Japanese
    } else {
        Codec::Korean
    }
}

enum Script {
    Japanese,
    Korean,
}

fn score(enc: &'static encoding_rs::Encoding, b: &[u8], script: Script) -> i64 {
    let (text, _, had_errors) = enc.decode(b);
    let mut s = 0i64;
    for c in text.chars() {
        let u = c as u32;
        match script {
            Script::Japanese => match u {
                0x3041..=0x30FF => s += 2,         // hiragana, katakana
                0x4E00..=0x9FFF => s += 1,         // kanji
                0xFF61..=0xFF9F => s -= 2,         // half-width katakana: Korean bytes misread
                0xAC00..=0xD7A3 => s -= 2,         // Hangul cannot come out of Shift-JIS, but be safe
                0xFFFD => s -= 3,
                _ => {}
            },
            Script::Korean => match u {
                0xAC00..=0xD7A3 => s += 2,         // Hangul syllables
                0x4E00..=0x9FFF => s += 1,         // hanja
                0x3041..=0x30FF => s -= 2,         // kana out of EUC-KR: Japanese bytes misread
                0xFFFD => s -= 3,
                _ => {}
            },
        }
    }
    if had_errors {
        s -= 2;
    }
    s
}

// ------------------------------------------------------------------ STB

/// An STB in roselib's shape: `rows[i][0]` is the row label and
/// `rows[i][c + 1]` game column `c`, with `rows[i]` being game row `i`.
pub struct RawStb {
    pub rows: Vec<Vec<Vec<u8>>>,
}

impl RawStb {
    pub fn parse(b: &[u8]) -> Result<RawStb> {
        if b.len() < 16 || &b[..4] != b"STB1" {
            bail!("not an STB1 file");
        }
        let u32_at = |o: usize| -> Result<usize> {
            Ok(u32::from_le_bytes(b.get(o..o + 4).context("truncated STB header")?.try_into()?)
                as usize)
        };
        let offset = u32_at(4)?;
        let raw_rows = u32_at(8)?;
        let raw_cols = u32_at(12)?;
        if raw_rows == 0 || raw_cols == 0 {
            bail!("STB declares no rows or columns");
        }
        let mut p = 16 + 4 + 2 * (raw_cols + 1); // row height, (cols + 1) widths
        let pstr = |p: &mut usize| -> Result<Vec<u8>> {
            let n = u16::from_le_bytes(b.get(*p..*p + 2).context("truncated STB")?.try_into()?)
                as usize;
            *p += 2;
            let s = b.get(*p..*p + n).context("truncated STB")?.to_vec();
            *p += n;
            Ok(s)
        };
        for _ in 0..raw_cols {
            pstr(&mut p)?; // column names
        }
        pstr(&mut p)?; // the column-title line's row name
        let mut rows = Vec::with_capacity(raw_rows - 1);
        for _ in 0..raw_rows - 1 {
            rows.push(vec![pstr(&mut p)?]);
        }
        let mut p = offset;
        for row in rows.iter_mut() {
            for _ in 0..raw_cols - 1 {
                row.push(pstr(&mut p)?);
            }
        }
        Ok(RawStb { rows })
    }

    pub fn cells(&self) -> impl Iterator<Item = &[u8]> {
        self.rows.iter().flatten().map(Vec::as_slice)
    }

    pub fn decode(&self, codec: Codec) -> Vec<Vec<String>> {
        let codec = codec.resolve(self.cells());
        self.rows
            .iter()
            .map(|row| row.iter().map(|c| codec.decode(c)).collect())
            .collect()
    }
}

pub fn read_stb(bytes: &[u8], codec: Codec) -> Result<Vec<Vec<String>>> {
    Ok(RawStb::parse(bytes)?.decode(codec))
}

// ------------------------------------------------------------------ STL

/// key -> (name, description), empty names dropped. Reads the modern
/// `NRST01` / `ITST01` / `QEST01` tables (language block 1, English, when
/// there is more than one block) and the legacy `N_NUM` / `I_NUM` / `Q_NUM`
/// dialect, which has neither a language table nor an offset table: the
/// entries simply follow the keys.
pub type Names = HashMap<String, (String, String)>;

pub fn read_stl_names(b: &[u8], codec: Codec) -> Result<Names> {
    let mut p = 0usize;
    let byte = |p: &mut usize| -> Result<usize> {
        let v = *b.get(*p).context("truncated STL")?;
        *p += 1;
        Ok(v as usize)
    };
    let u32_at = |p: &mut usize| -> Result<usize> {
        let v = u32::from_le_bytes(b.get(*p..*p + 4).context("truncated STL")?.try_into()?);
        *p += 4;
        Ok(v as usize)
    };
    let varint = |p: &mut usize| -> Result<usize> {
        let (mut n, mut shift) = (0usize, 0u32);
        loop {
            let c = *b.get(*p).context("truncated STL")?;
            *p += 1;
            n |= ((c & 0x7F) as usize) << shift;
            if c & 0x80 == 0 {
                return Ok(n);
            }
            shift += 7;
        }
    };
    let vstr = |p: &mut usize| -> Result<&[u8]> {
        let n = varint(p)?;
        let s = b.get(*p..*p + n).context("truncated STL")?;
        *p += n;
        Ok(s)
    };

    let n = byte(&mut p)?;
    let tag = b.get(p..p + n).context("truncated STL")?;
    p += n;
    let (fields, legacy) = match tag {
        b"NRST01" => (1, false),
        b"ITST01" => (2, false),
        b"QEST01" => (4, false),
        b"N_NUM" => (1, true),
        b"I_NUM" => (2, true),
        b"Q_NUM" => (4, true),
        other => bail!("unknown STL tag {:?}", String::from_utf8_lossy(other)),
    };
    let count = u32_at(&mut p)?;
    let mut keys = Vec::with_capacity(count);
    for _ in 0..count {
        let n = byte(&mut p)?;
        let key = b.get(p..p + n).context("truncated STL")?;
        p += n;
        let _id = u32_at(&mut p)?;
        keys.push(String::from_utf8_lossy(key).into_owned());
    }

    let mut raw: Vec<[&[u8]; 2]> = Vec::with_capacity(count);
    if legacy {
        for _ in 0..count {
            let name = vstr(&mut p)?;
            let desc = if fields >= 2 { vstr(&mut p)? } else { &[][..] };
            for _ in 2..fields {
                vstr(&mut p)?;
            }
            raw.push([name, desc]);
        }
    } else {
        let nlang = u32_at(&mut p)?;
        if nlang == 0 {
            bail!("STL declares no language block");
        }
        let mut offsets = Vec::with_capacity(nlang);
        for _ in 0..nlang {
            offsets.push(u32_at(&mut p)?);
        }
        let block = offsets[if nlang > 1 { 1 } else { 0 }];
        for i in 0..count {
            let mut q = block + 4 * i;
            let at = u32_at(&mut q)?;
            let mut q = at;
            let name = vstr(&mut q)?;
            let desc = if fields >= 2 { vstr(&mut q)? } else { &[][..] };
            raw.push([name, desc]);
        }
    }

    let codec = codec.resolve(raw.iter().flat_map(|r| r.iter().copied()));
    Ok(keys
        .into_iter()
        .zip(raw)
        .filter_map(|(key, [name, desc])| {
            let name = codec.decode(name);
            if name.trim().is_empty() {
                None
            } else {
                Some((key, (name, codec.decode(desc))))
            }
        })
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detects_scripts_and_falls_back_to_western() {
        let ja = "鏡の盾".as_bytes();
        let (ja_bytes, _, _) = SHIFT_JIS.encode(std::str::from_utf8(ja).unwrap());
        assert_eq!(Codec::Auto.resolve([ja_bytes.as_ref()]), Codec::Japanese);
        let (ko_bytes, _, _) = EUC_KR.encode("나무 방패");
        assert_eq!(Codec::Auto.resolve([ko_bytes.as_ref()]), Codec::Korean);
        assert_eq!(Codec::Auto.resolve([b"Wooden Shield".as_ref()]), Codec::Western);
        assert_eq!(Codec::Auto.resolve([b"Fafnir\x92s Scale".as_ref()]), Codec::Western);
        assert_eq!(Codec::Western.decode(b"Fafnir\x92s"), "Fafnir\u{2019}s");
    }

    #[test]
    fn legacy_and_modern_stl_agree() {
        // one key, two fields, legacy I_NUM: entries follow the keys directly
        let mut legacy = vec![5u8];
        legacy.extend(b"I_NUM");
        legacy.extend(1u32.to_le_bytes());
        legacy.push(7);
        legacy.extend(b"LSUB001");
        legacy.extend(1u32.to_le_bytes());
        legacy.extend([4u8]);
        legacy.extend(b"Name");
        legacy.extend([4u8]);
        legacy.extend(b"Desc");
        let names = read_stl_names(&legacy, Codec::Auto).unwrap();
        assert_eq!(names["LSUB001"], ("Name".to_string(), "Desc".to_string()));

        // the same as ITST01 with two language blocks; block 1 is read
        let mut modern = vec![6u8];
        modern.extend(b"ITST01");
        modern.extend(1u32.to_le_bytes());
        modern.push(7);
        modern.extend(b"LSUB001");
        modern.extend(1u32.to_le_bytes());
        modern.extend(2u32.to_le_bytes());
        let lang_pos = modern.len();
        modern.extend([0u8; 8]);
        let mut blocks = Vec::new();
        for text in ["Nom", "Name"] {
            let start = modern.len() + blocks.len();
            let entry = start + 4;
            blocks.extend((entry as u32).to_le_bytes());
            blocks.push(text.len() as u8);
            blocks.extend(text.as_bytes());
            blocks.push(4);
            blocks.extend(b"Desc");
        }
        let first = modern.len();
        let second = first + 4 + 1 + 3 + 1 + 4;
        modern[lang_pos..lang_pos + 4].copy_from_slice(&(first as u32).to_le_bytes());
        modern[lang_pos + 4..lang_pos + 8].copy_from_slice(&(second as u32).to_le_bytes());
        modern.extend(blocks);
        let names = read_stl_names(&modern, Codec::Auto).unwrap();
        assert_eq!(names["LSUB001"].0, "Name");
    }
}
