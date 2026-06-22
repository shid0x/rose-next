//! Phase 0 of the NPC dialog system — a codec for `.CON` conversation files.
//!
//! A `.CON` (e.g. `3DDATA/EVENT/EM01-001.CON`) drives an NPC conversation. Layout
//! (from the client loader `src/client/event/cevent.cpp`):
//!
//! ```text
//! [0]            SSC_FILE_HEADER (524 bytes, MSVC-packed)
//!                  u16 event_mask
//!                  char func_name[16][32]      // event-slot Lua function names
//!                  (2 pad bytes)               // align ConvOff to 4
//!                  u32 conv_off                 // base of the conversation section
//!                  u32 script_off               // absolute offset of the Lua blob
//! [524]          SSC_CONV_HEADER (16 bytes)
//!                  i32 msg_num;  u32 msg_start_off   // both MMTs are conv_off-relative
//!                  i32 menu_num; u32 menu_start_off
//! [conv_off + msg_start_off]   u32 msg_mmt[msg_num]    -> each SSC_msg (80 bytes)
//! [conv_off + menu_start_off]  u32 menu_mmt[menu_num]  -> each SSC_MENU_COLL
//! [script_off]   i32 lua_len; u8 lua[lua_len]          // XOR-encoded Lua
//! ```
//!
//! Two regions are XOR-obfuscated by [`decode`]: each menu collection's body
//! (everything after its first 8 bytes) and the Lua blob. The XOR is a single
//! repeating byte; encode == decode.
//!
//! This module only *parses* for now (dump + parse-all). The byte-exact
//! round-trip serializer is the next step.

use anyhow::{bail, Context, Result};
use std::path::Path;

const FILE_HEADER_LEN: usize = 524;
const CONV_HEADER_OFF: usize = FILE_HEADER_LEN;
const NUM_EVENT: usize = 16;
const FUNC_NAME_LEN: usize = 32;

/// One conversation message (the `m_pScrMSG` table; slot 0 is the entry check).
#[derive(Debug, Clone)]
pub struct ConMsg {
    pub sn: i32,
    pub mtype: i32,
    pub value: i32,
    pub check_func: String,
    pub click_func: String,
    pub str_id: i32,
}

/// One selectable line inside a menu collection.
#[derive(Debug, Clone)]
pub struct ConMenuItem {
    pub mtype: i32,
    pub child_menu: i32,
    pub check_func: String,
    pub click_func: String,
    pub str_id: i32,
}

/// A menu collection = the set of lines shown together (a dialog node's children).
#[derive(Debug, Clone)]
pub struct ConMenu {
    pub items: Vec<ConMenuItem>,
}

/// What the embedded script blob looks like.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LuaKind {
    /// Lua 4 precompiled chunk (starts with the `\x1bLua` signature).
    Bytecode,
    /// Readable Lua source.
    Source,
    Empty,
}

#[derive(Debug, Clone)]
pub struct ConFile {
    pub event_mask: u16,
    /// (slot, function name) for every event slot whose mask bit is set.
    pub event_funcs: Vec<(usize, String)>,
    pub conv_off: u32,
    pub script_off: u32,
    pub messages: Vec<ConMsg>,
    pub menus: Vec<ConMenu>,
    /// XOR-decoded script blob (Lua source or bytecode).
    pub lua: Vec<u8>,
    pub file_size: usize,
    /// The original file bytes. Everything up to `script_off` (header + message +
    /// menu sections) is preserved verbatim on write; only the trailing Lua
    /// section is rebuilt from `lua`. Node-tree edits patch `raw` in place.
    pub raw: Vec<u8>,
}

/// The single repeating XOR byte the client uses: `iValue1` when odd, else `iValue2`.
fn xor_key(v1: i32, v2: i32) -> u8 {
    if v1 & 1 != 0 {
        v1 as u8
    } else {
        v2 as u8
    }
}

/// In-place XOR (encode == decode).
fn decode(buf: &mut [u8], key: u8) {
    for b in buf.iter_mut() {
        *b ^= key;
    }
}

fn rd_u16(b: &[u8], o: usize) -> Result<u16> {
    b.get(o..o + 2)
        .map(|s| u16::from_le_bytes([s[0], s[1]]))
        .with_context(|| format!("u16 read past EOF at {o}"))
}
fn rd_i32(b: &[u8], o: usize) -> Result<i32> {
    b.get(o..o + 4)
        .map(|s| i32::from_le_bytes([s[0], s[1], s[2], s[3]]))
        .with_context(|| format!("i32 read past EOF at {o}"))
}
fn rd_u32(b: &[u8], o: usize) -> Result<u32> {
    Ok(rd_i32(b, o)? as u32)
}

/// Read a NUL-terminated string from a fixed `len`-byte field.
fn rd_cstr(b: &[u8], o: usize, len: usize) -> Result<String> {
    let field = b
        .get(o..o + len)
        .with_context(|| format!("cstr field past EOF at {o}"))?;
    let end = field.iter().position(|&c| c == 0).unwrap_or(len);
    Ok(String::from_utf8_lossy(&field[..end]).into_owned())
}

/// Parse one 80-byte SSC_msg / SSC_MenuItem at `o`.
fn rd_msg(b: &[u8], o: usize) -> Result<(i32, i32, i32, String, String, i32)> {
    Ok((
        rd_i32(b, o)?,           // sn / unused
        rd_i32(b, o + 4)?,       // type
        rd_i32(b, o + 8)?,       // value / child
        rd_cstr(b, o + 12, 32)?, // func1
        rd_cstr(b, o + 44, 32)?, // func2
        rd_i32(b, o + 76)?,      // str id
    ))
}

impl ConFile {
    pub fn parse(b: &[u8]) -> Result<ConFile> {
        if b.len() < CONV_HEADER_OFF + 16 {
            bail!("file too small ({} bytes) for a .CON header", b.len());
        }
        let event_mask = rd_u16(b, 0)?;
        let mut event_funcs = Vec::new();
        for slot in 0..NUM_EVENT {
            if event_mask & (1 << slot) != 0 {
                let name = rd_cstr(b, 2 + slot * FUNC_NAME_LEN, FUNC_NAME_LEN)?;
                event_funcs.push((slot, name));
            }
        }
        let conv_off = rd_u32(b, 516)?;
        let script_off = rd_u32(b, 520)?;

        // SSC_CONV_HEADER @ 524
        let msg_num = rd_i32(b, CONV_HEADER_OFF)?;
        let msg_start_off = rd_u32(b, CONV_HEADER_OFF + 4)?;
        let menu_num = rd_i32(b, CONV_HEADER_OFF + 8)?;
        let menu_start_off = rd_u32(b, CONV_HEADER_OFF + 12)?;

        // --- messages ---
        let msg_base = (conv_off + msg_start_off) as usize;
        let mut messages = Vec::with_capacity(msg_num.max(0) as usize);
        for i in 0..msg_num.max(0) as usize {
            let mmt = rd_u32(b, msg_base + i * 4)? as usize;
            let (sn, mtype, value, f1, f2, str_id) = rd_msg(b, msg_base + mmt)?;
            messages.push(ConMsg {
                sn,
                mtype,
                value,
                check_func: f1,
                click_func: f2,
                str_id,
            });
        }

        // --- menus (each collection's body is XOR-encoded) ---
        let menu_base = (conv_off + menu_start_off) as usize;
        let mut menus = Vec::with_capacity(menu_num.max(0) as usize);
        for i in 0..menu_num.max(0) as usize {
            let mmt = rd_u32(b, menu_base + i * 4)? as usize;
            let coll_off = menu_base + mmt;
            let length = rd_i32(b, coll_off)? as usize;
            let num_sub = rd_i32(b, coll_off + 4)?;
            if length < 8 || coll_off + length > b.len() {
                bail!("menu {i}: bad collection length {length} at {coll_off}");
            }
            // Copy the collection and decode everything after the two header ints.
            let mut coll = b[coll_off..coll_off + length].to_vec();
            let key = xor_key(num_sub, length as i32);
            decode(&mut coll[8..], key);

            let mut items = Vec::with_capacity(num_sub.max(0) as usize);
            for j in 0..num_sub.max(0) as usize {
                let sub_mmt = rd_u32(&coll, 8 + j * 4)? as usize; // offset within coll
                let (_, mtype, child, f1, f2, str_id) = rd_msg(&coll, sub_mmt)?;
                items.push(ConMenuItem {
                    mtype,
                    child_menu: child,
                    check_func: f1,
                    click_func: f2,
                    str_id,
                });
            }
            menus.push(ConMenu { items });
        }

        // --- Lua script blob ---
        let script_off = script_off as usize;
        let lua_len = rd_i32(b, script_off)?.max(0) as usize;
        let lua_start = script_off + 4;
        let mut lua = b
            .get(lua_start..lua_start + lua_len)
            .with_context(|| format!("lua blob past EOF (off {lua_start}, len {lua_len})"))?
            .to_vec();
        decode(&mut lua, xor_key(lua_len as i32, b.len() as i32));

        Ok(ConFile {
            event_mask,
            event_funcs,
            conv_off,
            script_off: script_off as u32,
            messages,
            menus,
            lua,
            file_size: b.len(),
            raw: b.to_vec(),
        })
    }

    pub fn read_file(path: &Path) -> Result<ConFile> {
        let bytes = std::fs::read(path).with_context(|| format!("reading {}", path.display()))?;
        Self::parse(&bytes)
    }

    /// Serialize back to `.CON` bytes: the header / message / menu sections are
    /// taken verbatim from `raw` (so node-tree edits, which patch `raw` in place,
    /// are preserved), and the trailing Lua section is rebuilt from `lua` with the
    /// XOR re-applied. For an unmodified file this reproduces the original bytes
    /// exactly — the round-trip check that proves the Lua codec + offsets.
    pub fn to_bytes(&self) -> Vec<u8> {
        let so = self.script_off as usize;
        let mut out = self.raw[..so].to_vec();
        let lua_len = self.lua.len();
        out.extend_from_slice(&(lua_len as i32).to_le_bytes());
        let total = so + 4 + lua_len;
        let mut enc = self.lua.clone();
        decode(&mut enc, xor_key(lua_len as i32, total as i32)); // encode == decode
        out.extend_from_slice(&enc);
        out
    }

    /// Replace the embedded script with Lua **source** (the client's `lua_dobuffer`
    /// compiles it at load — no bytecode step needed).
    pub fn set_lua_source(&mut self, src: &str) {
        self.lua = src.as_bytes().to_vec();
    }

    pub fn lua_kind(&self) -> LuaKind {
        if self.lua.is_empty() {
            LuaKind::Empty
        } else if self.lua.starts_with(b"\x1bLua") {
            LuaKind::Bytecode
        } else {
            LuaKind::Source
        }
    }
}

// --- menu/message node types (cevent.cpp SC_MSG_*) ---
pub const SC_MSG_CLOSE: i32 = 0; // a selectable line that closes the dialog
pub const SC_MSG_NEXTMSG: i32 = 1;
pub const SC_MSG_NPCSAY: i32 = 2; // NPC speech bubble, then jump to child menu
pub const SC_MSG_PLAYERSELECT: i32 = 3; // a selectable menu line (-> child menu)
pub const SC_MSG_JUMPSELECT: i32 = 4;

fn wr_cstr(buf: &mut Vec<u8>, s: &str, len: usize) {
    let bytes = s.as_bytes();
    let n = bytes.len().min(len.saturating_sub(1)); // keep at least one NUL
    buf.extend_from_slice(&bytes[..n]);
    buf.resize(buf.len() + (len - n), 0);
}

/// Write one 80-byte SSC_msg / SSC_MenuItem.
fn wr_node(buf: &mut Vec<u8>, sn: i32, mtype: i32, value: i32, f1: &str, f2: &str, str_id: i32) {
    buf.extend_from_slice(&sn.to_le_bytes());
    buf.extend_from_slice(&mtype.to_le_bytes());
    buf.extend_from_slice(&value.to_le_bytes());
    wr_cstr(buf, f1, 32);
    wr_cstr(buf, f2, 32);
    buf.extend_from_slice(&str_id.to_le_bytes());
}

/// Build a complete `.CON` from a node model + Lua **source**. Offsets are laid
/// out canonically (`conv_off = 524`, message section then menu section then the
/// Lua tail); the parser reads everything by explicit offset, so no incidental
/// padding is needed. Verified by re-parsing the output (see tests + `con-build`).
pub fn build_con(
    event_funcs: &[(usize, String)],
    messages: &[ConMsg],
    menus: &[ConMenu],
    lua: &[u8],
) -> Vec<u8> {
    const CONV_OFF: u32 = 524;
    let m = messages.len();
    let n = menus.len();

    let msg_mmt_size = m * 4;
    let msg_bodies_size = m * 80;
    let msg_start_off: u32 = 16; // right after the 16-byte CONV_HEADER
    let menu_start_off: u32 = msg_start_off + (msg_mmt_size + msg_bodies_size) as u32;

    let coll_sizes: Vec<usize> = menus
        .iter()
        .map(|menu| 8 + menu.items.len() * 4 + menu.items.len() * 80)
        .collect();
    let menu_mmt_size = n * 4;
    let total_coll: usize = coll_sizes.iter().sum();

    let conv_section = 16 + msg_mmt_size + msg_bodies_size + menu_mmt_size + total_coll;
    let script_off = CONV_OFF as usize + conv_section;

    let mut event_mask: u16 = 0;
    for (slot, _) in event_funcs {
        if *slot < NUM_EVENT {
            event_mask |= 1 << slot;
        }
    }

    let mut out: Vec<u8> = Vec::with_capacity(script_off + 4 + lua.len());

    // FILE_HEADER (524).
    out.extend_from_slice(&event_mask.to_le_bytes());
    let mut funcs: Vec<&str> = vec![""; NUM_EVENT];
    for (slot, name) in event_funcs {
        if *slot < NUM_EVENT {
            funcs[*slot] = name;
        }
    }
    for f in &funcs {
        wr_cstr(&mut out, f, FUNC_NAME_LEN);
    }
    out.extend_from_slice(&[0u8, 0]); // 2 pad
    out.extend_from_slice(&CONV_OFF.to_le_bytes());
    out.extend_from_slice(&(script_off as u32).to_le_bytes());
    debug_assert_eq!(out.len(), FILE_HEADER_LEN);

    // CONV_HEADER (16).
    out.extend_from_slice(&(m as i32).to_le_bytes());
    out.extend_from_slice(&msg_start_off.to_le_bytes());
    out.extend_from_slice(&(n as i32).to_le_bytes());
    out.extend_from_slice(&menu_start_off.to_le_bytes());

    // Message MMT + bodies.
    for i in 0..m {
        out.extend_from_slice(&((msg_mmt_size + i * 80) as u32).to_le_bytes());
    }
    for msg in messages {
        wr_node(&mut out, msg.sn, msg.mtype, msg.value, &msg.check_func, &msg.click_func, msg.str_id);
    }

    // Menu MMT + collections.
    let mut acc = menu_mmt_size;
    for size in &coll_sizes {
        out.extend_from_slice(&(acc as u32).to_le_bytes());
        acc += size;
    }
    for (mi, menu) in menus.iter().enumerate() {
        let k = menu.items.len();
        let len = coll_sizes[mi];
        let num_sub = k as i32;
        let mut coll: Vec<u8> = Vec::with_capacity(len);
        coll.extend_from_slice(&(len as i32).to_le_bytes());
        coll.extend_from_slice(&num_sub.to_le_bytes());
        for j in 0..k {
            coll.extend_from_slice(&((8 + k * 4 + j * 80) as u32).to_le_bytes());
        }
        for it in &menu.items {
            wr_node(&mut coll, 0, it.mtype, it.child_menu, &it.check_func, &it.click_func, it.str_id);
        }
        debug_assert_eq!(coll.len(), len);
        decode(&mut coll[8..], xor_key(num_sub, len as i32)); // encode == decode
        out.extend_from_slice(&coll);
    }
    debug_assert_eq!(out.len(), script_off);

    // Lua tail.
    out.extend_from_slice(&(lua.len() as i32).to_le_bytes());
    let total = script_off + 4 + lua.len();
    let mut enc = lua.to_vec();
    decode(&mut enc, xor_key(lua.len() as i32, total as i32));
    out.extend_from_slice(&enc);

    out
}

/// Construct a menu item (free helper so callers don't repeat the struct).
pub fn menu_item(mtype: i32, child: i32, check: &str, click: &str, str_id: i32) -> ConMenuItem {
    ConMenuItem {
        mtype,
        child_menu: child,
        check_func: check.into(),
        click_func: click.into(),
        str_id,
    }
}

/// Text-string ids (into the event string table) for a quest-giver conversation.
/// Until we author our own event strings (Phase 2b) these reuse existing ids so
/// *some* text shows and the flow can be validated.
#[derive(Debug, Clone, Copy)]
pub struct GiverStrings {
    pub greeting: i32,
    pub accept_option: i32,
    pub complete_option: i32,
    pub progress_option: i32,
    pub bye_option: i32,
    pub after_accept: i32,
    pub after_complete: i32,
    pub in_progress: i32,
}

impl Default for GiverStrings {
    fn default() -> Self {
        // Low ids that exist in the global event string table (reused for now).
        GiverStrings {
            greeting: 1,
            accept_option: 2,
            complete_option: 3,
            progress_option: 4,
            bye_option: 5,
            after_accept: 6,
            after_complete: 7,
            in_progress: 8,
        }
    }
}

/// Build a quest-giver `.CON` for an existing QSD quest: an NPC greeting then a
/// menu that offers **Accept** (when not yet taken), **Turn in** (when the
/// complete trigger's conditions pass), or **In progress** — all gated by the
/// embedded Lua, which drives the quest via the engine `QF_*` API. `complete_trig`
/// is the QSD trigger to fire on turn-in (`<qid>-3` Hunt / `<qid>-2` Fetch).
pub fn build_quest_giver(qid: i32, complete_trig: &str, s: GiverStrings) -> Vec<u8> {
    let lua = format!(
        "-- generated quest-giver for quest {qid}\n\
         QID = {qid}\n\
         REG = \"{qid}-1\"\n\
         TRG = \"{complete_trig}\"\n\
         function CHK_accept(E)\n\
         \tif QF_findQuest(QID) >= 0 then return 0 end\n\
         \treturn 1\n\
         end\n\
         function ACT_accept(E)\n\
         \tQF_doQuestTrigger(REG)\n\
         \treturn 1\n\
         end\n\
         function CHK_complete(E)\n\
         \tif QF_findQuest(QID) < 0 then return 0 end\n\
         \tif QF_checkQuestCondition(TRG) < 1 then return 0 end\n\
         \treturn 1\n\
         end\n\
         function ACT_complete(E)\n\
         \tQF_doQuestTrigger(TRG)\n\
         \treturn 1\n\
         end\n\
         function CHK_progress(E)\n\
         \tif QF_findQuest(QID) < 0 then return 0 end\n\
         \tif QF_checkQuestCondition(TRG) >= 1 then return 0 end\n\
         \treturn 1\n\
         end\n"
    );

    let messages = vec![ConMsg {
        sn: 0,
        mtype: SC_MSG_PLAYERSELECT,
        value: 0,
        check_func: String::new(),
        click_func: String::new(),
        str_id: 0,
    }];
    let menus = vec![
        // [0] greeting -> options
        ConMenu {
            items: vec![menu_item(SC_MSG_NPCSAY, 1, "", "", s.greeting)],
        },
        // [1] the options (gated by the Lua checks)
        ConMenu {
            items: vec![
                menu_item(SC_MSG_PLAYERSELECT, 2, "CHK_accept", "ACT_accept", s.accept_option),
                menu_item(SC_MSG_PLAYERSELECT, 3, "CHK_complete", "ACT_complete", s.complete_option),
                menu_item(SC_MSG_PLAYERSELECT, 4, "CHK_progress", "", s.progress_option),
                menu_item(SC_MSG_CLOSE, -1, "", "", s.bye_option),
            ],
        },
        // [2] after accept
        ConMenu {
            items: vec![menu_item(SC_MSG_NPCSAY, -1, "", "", s.after_accept)],
        },
        // [3] after complete
        ConMenu {
            items: vec![menu_item(SC_MSG_NPCSAY, -1, "", "", s.after_complete)],
        },
        // [4] in progress
        ConMenu {
            items: vec![menu_item(SC_MSG_NPCSAY, -1, "", "", s.in_progress)],
        },
    ];

    build_con(&[], &messages, &menus, lua.as_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn item(mtype: i32, child: i32, check: &str, click: &str, str_id: i32) -> ConMenuItem {
        ConMenuItem {
            mtype,
            child_menu: child,
            check_func: check.into(),
            click_func: click.into(),
            str_id,
        }
    }

    #[test]
    fn built_con_round_trips_through_parser() {
        let messages = vec![ConMsg {
            sn: 0,
            mtype: SC_MSG_NPCSAY,
            value: 0,
            check_func: String::new(),
            click_func: String::new(),
            str_id: 1,
        }];
        let menus = vec![
            ConMenu {
                items: vec![item(SC_MSG_NPCSAY, 1, "", "", 5)],
            },
            ConMenu {
                items: vec![
                    item(SC_MSG_PLAYERSELECT, -1, "CHECK_accept", "CLICK_accept", 7),
                    item(SC_MSG_CLOSE, -1, "", "", 8),
                ],
            },
        ];
        let lua = b"-- giver\nfunction CLICK_accept(EVENT) return 1 end\n";
        let bytes = build_con(&[], &messages, &menus, lua);

        let p = ConFile::parse(&bytes).expect("parse built .CON");
        assert_eq!(p.messages.len(), 1);
        assert_eq!(p.menus.len(), 2);
        assert_eq!(p.menus[0].items[0].child_menu, 1);
        assert_eq!(p.menus[1].items.len(), 2);
        assert_eq!(p.menus[1].items[0].check_func, "CHECK_accept");
        assert_eq!(p.menus[1].items[0].click_func, "CLICK_accept");
        assert_eq!(p.menus[1].items[0].str_id, 7);
        assert_eq!(p.menus[1].items[1].mtype, SC_MSG_CLOSE);
        assert_eq!(p.lua, lua);
        // The parsed file (raw == built bytes) re-serializes identically.
        assert_eq!(p.to_bytes(), bytes);
    }
}
