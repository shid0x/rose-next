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
//! ## Appendix chunk (our extension)
//!
//! Retail conversation Lua is *bytecode*, so extra quest options can't be merged
//! into the blob. Instead an optional appendix sits after the Lua tail:
//!
//! ```text
//! [script_off + 4 + lua_len]   b"QEX1"; i32 appendix_len; XOR'd Lua *source*
//! ```
//!
//! The appendix-aware client (`cevent.cpp`, `QEX_APPENDIX_MAGIC`) executes it into
//! the same `lua_State` after the main blob, so appended menu nodes can name
//! `QE<qid>_*` functions defined here. The XOR key is the usual
//! `xor_key(len, file_size)` — note the *main* blob's key also involves the file
//! size, so appending re-encodes it (handled by the serializers below).

use anyhow::{bail, Context, Result};
use std::collections::BTreeSet;
use std::path::Path;

const FILE_HEADER_LEN: usize = 524;
const CONV_HEADER_OFF: usize = FILE_HEADER_LEN;
const NUM_EVENT: usize = 16;
const FUNC_NAME_LEN: usize = 32;

/// Appendix magic — keep in sync with `QEX_APPENDIX_MAGIC` in `cevent.cpp`.
pub const APPENDIX_MAGIC: &[u8; 4] = b"QEX1";

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
    /// XOR-decoded appendix Lua source (our `QEX1` extension; empty = none).
    pub appendix: Vec<u8>,
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

        // --- optional appendix chunk (our extension) ---
        let lua_end = lua_start + lua_len;
        let mut appendix = Vec::new();
        if b.len() >= lua_end + 8 && &b[lua_end..lua_end + 4] == APPENDIX_MAGIC {
            let alen = rd_i32(b, lua_end + 4)?.max(0) as usize;
            let mut a = b
                .get(lua_end + 8..lua_end + 8 + alen)
                .with_context(|| format!("appendix past EOF (off {}, len {alen})", lua_end + 8))?
                .to_vec();
            decode(&mut a, xor_key(alen as i32, b.len() as i32));
            appendix = a;
        }

        Ok(ConFile {
            event_mask,
            event_funcs,
            conv_off,
            script_off: script_off as u32,
            messages,
            menus,
            lua,
            appendix,
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
    /// are preserved), and the trailing Lua section (+ appendix, if any) is
    /// rebuilt from `lua`/`appendix` with the XOR re-applied. For an unmodified
    /// file this reproduces the original bytes exactly — the round-trip check
    /// that proves the Lua codec + offsets.
    pub fn to_bytes(&self) -> Vec<u8> {
        let so = self.script_off as usize;
        let mut out = self.raw[..so].to_vec();
        wr_lua_tail(&mut out, so, &self.lua, &self.appendix);
        out
    }

    /// Serialize from the parsed *model* (canonical layout), instead of `raw`.
    /// Use after structural node-tree edits (inserting/removing menu items);
    /// semantically identical to the source file — the client reads everything by
    /// explicit offsets, so the incidental retail layout doesn't matter.
    pub fn rebuild(&self) -> Vec<u8> {
        build_con_full(
            &self.event_funcs,
            &self.messages,
            &self.menus,
            &self.lua,
            &self.appendix,
        )
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

/// Write the Lua tail (+ optional appendix block). The main blob's XOR key uses
/// the **final** file size, which includes the appendix block — the client
/// decodes with the actual file size, so both must be encoded against the same
/// total.
fn wr_lua_tail(out: &mut Vec<u8>, script_off: usize, lua: &[u8], appendix: &[u8]) {
    let appendix_block = if appendix.is_empty() {
        0
    } else {
        8 + appendix.len()
    };
    let total = script_off + 4 + lua.len() + appendix_block;
    out.extend_from_slice(&(lua.len() as i32).to_le_bytes());
    let mut enc = lua.to_vec();
    decode(&mut enc, xor_key(lua.len() as i32, total as i32)); // encode == decode
    out.extend_from_slice(&enc);
    if !appendix.is_empty() {
        out.extend_from_slice(APPENDIX_MAGIC);
        out.extend_from_slice(&(appendix.len() as i32).to_le_bytes());
        let mut enc = appendix.to_vec();
        decode(&mut enc, xor_key(appendix.len() as i32, total as i32));
        out.extend_from_slice(&enc);
    }
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
    build_con_full(event_funcs, messages, menus, lua, &[])
}

/// [`build_con`] plus an optional appendix chunk (see module docs).
pub fn build_con_full(
    event_funcs: &[(usize, String)],
    messages: &[ConMsg],
    menus: &[ConMenu],
    lua: &[u8],
    appendix: &[u8],
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
        wr_node(
            &mut out,
            msg.sn,
            msg.mtype,
            msg.value,
            &msg.check_func,
            &msg.click_func,
            msg.str_id,
        );
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
            wr_node(
                &mut coll,
                0,
                it.mtype,
                it.child_menu,
                &it.check_func,
                &it.click_func,
                it.str_id,
            );
        }
        debug_assert_eq!(coll.len(), len);
        decode(&mut coll[8..], xor_key(num_sub, len as i32)); // encode == decode
        out.extend_from_slice(&coll);
    }
    debug_assert_eq!(out.len(), script_off);

    // Lua tail (+ optional appendix).
    wr_lua_tail(&mut out, script_off, lua, appendix);

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
    /// The "[Close]" button shown under each NPC response message.
    pub response_close: i32,
    /// Append mode: the option line added to the NPC's existing root menu
    /// ("I heard you need some help..."). Clicking it opens the start message.
    pub hook_option: i32,
    /// Append mode: the decline choice under the start message. Separate from
    /// `bye_option`, which is the dedicated giver's always-visible close line.
    pub decline_option: i32,
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
            response_close: 5,
            hook_option: 2,
            decline_option: 5,
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
         \tif QF_checkQuestCondition(REG) < 1 then return 0 end\n\
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
                menu_item(
                    SC_MSG_PLAYERSELECT,
                    2,
                    "CHK_accept",
                    "ACT_accept",
                    s.accept_option,
                ),
                menu_item(
                    SC_MSG_PLAYERSELECT,
                    3,
                    "CHK_complete",
                    "ACT_complete",
                    s.complete_option,
                ),
                menu_item(
                    SC_MSG_PLAYERSELECT,
                    4,
                    "CHK_progress",
                    "",
                    s.progress_option,
                ),
                menu_item(SC_MSG_CLOSE, -1, "", "", s.bye_option),
            ],
        },
        // [2] after accept -> close
        ConMenu {
            items: vec![menu_item(SC_MSG_NPCSAY, 5, "", "", s.after_accept)],
        },
        // [3] after complete -> close
        ConMenu {
            items: vec![menu_item(SC_MSG_NPCSAY, 5, "", "", s.after_complete)],
        },
        // [4] in progress -> close
        ConMenu {
            items: vec![menu_item(SC_MSG_NPCSAY, 5, "", "", s.in_progress)],
        },
        // [5] a "[Close]" button shown under each response message
        ConMenu {
            items: vec![menu_item(SC_MSG_CLOSE, -1, "", "", s.response_close)],
        },
    ];

    build_con(&[], &messages, &menus, lua.as_bytes())
}

// --------------------------------------------------------------------------
// Append mode: add a quest option to an *existing* (retail) conversation.
//
// The retail node tree is kept; we append namespaced `QE<qid>_*` menu items to
// the root menu (menu 0). `Conversation(0)` walks menu 0 in order and NPCSAY
// items recurse into their child menus *before* later siblings, so items
// appended at the end of menu 0 show up after all the NPC's original options in
// the same dialog. The functions live in the appendix chunk (see module docs),
// which the appendix-aware client executes into the same lua_State as the
// retail bytecode.
// --------------------------------------------------------------------------

fn appendix_begin_marker_named(key: &str) -> String {
    format!("-- QE:BEGIN {key}\n")
}
fn appendix_end_marker_named(key: &str) -> String {
    format!("-- QE:END {key}\n")
}

/// Insert (or replace) quest `qid`'s section in the appendix source.
pub fn appendix_upsert(appendix: &mut Vec<u8>, qid: i32, body: &str) {
    appendix_upsert_named(appendix, &qid.to_string(), body);
}

/// Insert (or replace) an arbitrarily-keyed section. Sections are independent,
/// so several features can share one appendix — a warp option and a quest option
/// on the same NPC do not disturb each other.
pub fn appendix_upsert_named(appendix: &mut Vec<u8>, key: &str, body: &str) {
    appendix_remove_named(appendix, key);
    let mut s = String::from_utf8_lossy(appendix).into_owned();
    if !s.is_empty() && !s.ends_with('\n') {
        s.push('\n');
    }
    s.push_str(&appendix_begin_marker_named(key));
    s.push_str(body);
    if !body.ends_with('\n') {
        s.push('\n');
    }
    s.push_str(&appendix_end_marker_named(key));
    *appendix = s.into_bytes();
}

/// Remove quest `qid`'s section from the appendix source. Returns whether a
/// section was found. An appendix left empty serializes to no appendix block.
pub fn appendix_remove(appendix: &mut Vec<u8>, qid: i32) -> bool {
    appendix_remove_named(appendix, &qid.to_string())
}

pub fn appendix_remove_named(appendix: &mut Vec<u8>, key: &str) -> bool {
    let s = String::from_utf8_lossy(appendix).into_owned();
    let begin = appendix_begin_marker_named(key);
    let end = appendix_end_marker_named(key);
    let Some(start) = s.find(&begin) else {
        return false;
    };
    let Some(end_rel) = s[start..].find(&end) else {
        return false;
    };
    let mut out = String::with_capacity(s.len());
    out.push_str(&s[..start]);
    out.push_str(&s[start + end_rel + end.len()..]);
    *appendix = if out.trim().is_empty() {
        Vec::new()
    } else {
        out.into_bytes()
    };
    true
}

/// The Lua source for one appended quest option. Everything is namespaced by
/// `QE<qid>_` and values are inlined as literals — no shared globals, so several
/// quests can coexist in one appendix (and with the retail blob's globals).
fn quest_option_lua(qid: i32, complete_trig: &str) -> String {
    let p = format!("QE{qid}_");
    format!(
        "function {p}CHK_accept(E)\n\
         \tif QF_findQuest({qid}) >= 0 then return 0 end\n\
         \tif QF_checkQuestCondition(\"{qid}-1\") < 1 then return 0 end\n\
         \treturn 1\n\
         end\n\
         function {p}ACT_accept(E)\n\
         \tQF_doQuestTrigger(\"{qid}-1\")\n\
         \treturn 1\n\
         end\n\
         function {p}CHK_complete(E)\n\
         \tif QF_findQuest({qid}) < 0 then return 0 end\n\
         \tif QF_checkQuestCondition(\"{complete_trig}\") < 1 then return 0 end\n\
         \treturn 1\n\
         end\n\
         function {p}ACT_complete(E)\n\
         \tQF_doQuestTrigger(\"{complete_trig}\")\n\
         \treturn 1\n\
         end\n\
         function {p}CHK_progress(E)\n\
         \tif QF_findQuest({qid}) < 0 then return 0 end\n\
         \tif QF_checkQuestCondition(\"{complete_trig}\") >= 1 then return 0 end\n\
         \treturn 1\n\
         end\n"
    )
}

/// Quest SNs that have an appended option in this conversation (from the
/// `QE<qid>_*` function names on menu items).
pub fn quest_option_qids(con: &ConFile) -> Vec<i32> {
    let mut out = BTreeSet::new();
    for menu in &con.menus {
        for it in &menu.items {
            for f in [&it.check_func, &it.click_func] {
                if let Some(rest) = f.strip_prefix("QE") {
                    if let Some(us) = rest.find('_') {
                        if let Ok(q) = rest[..us].parse::<i32>() {
                            out.insert(q);
                        }
                    }
                }
            }
        }
    }
    out.into_iter().collect()
}

/// Append quest `qid`'s dialog option to an existing conversation: three gated
/// `QE<qid>_*` option lines at the end of the root menu (offer / turn-in /
/// in-progress — at most one passes its check at a time). The offer line does
/// **not** accept by itself: clicking it shows the quest's start message with
/// explicit Accept / Decline choices, same flow as the dedicated giver. Plus
/// the response menus and the matching Lua in the appendix. Idempotent
/// (re-appending replaces the previous wiring). Serialize with
/// [`ConFile::rebuild`].
pub fn append_quest_option(
    con: &mut ConFile,
    qid: i32,
    complete_trig: &str,
    s: &GiverStrings,
) -> Result<()> {
    if con.menus.is_empty() {
        bail!(".CON has no menus — not an NPC conversation?");
    }
    remove_quest_option(con, qid);

    let p = format!("QE{qid}_");
    let base = con.menus.len() as i32;
    let close = base + 5;
    // [base] start message -> accept/decline, [base+1] the Accept / Decline
    // choices, [base+2] after-accept, [base+3] after-complete, [base+4]
    // in-progress; NPC responses all lead to [base+5], the "[Close]" menu.
    con.menus.push(ConMenu {
        items: vec![menu_item(SC_MSG_NPCSAY, base + 1, "", "", s.greeting)],
    });
    con.menus.push(ConMenu {
        items: vec![
            menu_item(
                SC_MSG_PLAYERSELECT,
                base + 2,
                &format!("{p}CHK_accept"),
                &format!("{p}ACT_accept"),
                s.accept_option,
            ),
            menu_item(SC_MSG_CLOSE, -1, "", "", s.decline_option),
        ],
    });
    con.menus.push(ConMenu {
        items: vec![menu_item(SC_MSG_NPCSAY, close, "", "", s.after_accept)],
    });
    con.menus.push(ConMenu {
        items: vec![menu_item(SC_MSG_NPCSAY, close, "", "", s.after_complete)],
    });
    con.menus.push(ConMenu {
        items: vec![menu_item(SC_MSG_NPCSAY, close, "", "", s.in_progress)],
    });
    con.menus.push(ConMenu {
        items: vec![menu_item(SC_MSG_CLOSE, -1, "", "", s.response_close)],
    });

    let root = &mut con.menus[0].items;
    root.push(menu_item(
        SC_MSG_PLAYERSELECT,
        base,
        &format!("{p}CHK_accept"),
        "",
        s.hook_option,
    ));
    root.push(menu_item(
        SC_MSG_PLAYERSELECT,
        base + 3,
        &format!("{p}CHK_complete"),
        &format!("{p}ACT_complete"),
        s.complete_option,
    ));
    root.push(menu_item(
        SC_MSG_PLAYERSELECT,
        base + 4,
        &format!("{p}CHK_progress"),
        "",
        s.progress_option,
    ));

    appendix_upsert(
        &mut con.appendix,
        qid,
        &quest_option_lua(qid, complete_trig),
    );
    Ok(())
}

/// Remove quest `qid`'s appended option: drop its `QE<qid>_*` menu items, the
/// menus reachable only from them (our response/close menus), remap the child
/// indexes of everything that stays, and strip its appendix section. Returns
/// whether anything was removed. Retail nodes are untouched.
pub fn remove_quest_option(con: &mut ConFile, qid: i32) -> bool {
    let p = format!("QE{qid}_");
    let is_ours = |it: &ConMenuItem| it.check_func.starts_with(&p) || it.click_func.starts_with(&p);

    // Menus reachable from our items' children (our appended subtree).
    let mut doomed: BTreeSet<usize> = BTreeSet::new();
    let mut stack: Vec<usize> = Vec::new();
    let mut found_items = false;
    for menu in &con.menus {
        for it in &menu.items {
            if is_ours(it) {
                found_items = true;
                if it.child_menu >= 0 {
                    stack.push(it.child_menu as usize);
                }
            }
        }
    }
    if !found_items {
        return appendix_remove(&mut con.appendix, qid);
    }
    while let Some(mi) = stack.pop() {
        if mi >= con.menus.len() || !doomed.insert(mi) {
            continue;
        }
        for it in &con.menus[mi].items {
            if it.child_menu >= 0 {
                stack.push(it.child_menu as usize);
            }
        }
    }

    // Drop our items, then keep any "doomed" menu that a surviving item still
    // references (can't happen with our generated layout — pure safety net).
    for menu in con.menus.iter_mut() {
        menu.items.retain(|it| !is_ours(it));
    }
    let referenced: BTreeSet<usize> = con
        .menus
        .iter()
        .enumerate()
        .filter(|(i, _)| !doomed.contains(i))
        .flat_map(|(_, m)| m.items.iter())
        .filter(|it| it.child_menu >= 0)
        .map(|it| it.child_menu as usize)
        .collect();
    doomed.retain(|mi| !referenced.contains(mi));

    // Remove doomed menus back-to-front, remapping surviving child references.
    for &mi in doomed.iter().rev() {
        con.menus.remove(mi);
        for menu in con.menus.iter_mut() {
            for it in menu.items.iter_mut() {
                if it.child_menu > mi as i32 {
                    it.child_menu -= 1;
                }
            }
        }
    }

    appendix_remove(&mut con.appendix, qid);
    true
}

// --------------------------------------------------------------------------
// Warp options: a menu entry that teleports the player somewhere.
//
// Same append mechanism as `append_quest_option`, but the option fires a plain
// QSD trigger (whose reward is REWD_007) instead of driving a quest. Used to
// give planets an entrance when the canonical one is unreachable — Oro's is a
// gate in a Junon Pyramid map we do not have.
//
// The confirm step is not decoration: the trigger warps immediately when
// clicked, and a mis-click that drops the player on another planet is a long
// walk back.
// --------------------------------------------------------------------------

/// Event-string ids for one appended warp option.
pub struct WarpStrings {
    /// The line added to the NPC's root menu ("Take me to ...").
    pub hook_option: i32,
    /// What the NPC says before the player confirms.
    pub confirm: i32,
    /// The player's "yes, go" choice.
    pub accept_option: i32,
    /// The player's "not now" choice.
    pub decline_option: i32,
}

/// Namespace prefix for one warp option's Lua functions and appendix section.
fn warp_prefix(key: &str) -> String {
    format!("QW{key}_")
}

fn warp_option_lua(key: &str, trigger: &str) -> String {
    let p = warp_prefix(key);
    format!(
        "function {p}CHK(E)\n\
         \treturn 1\n\
         end\n\
         function {p}GO(E)\n\
         \tQF_doQuestTrigger(\"{trigger}\")\n\
         \treturn 1\n\
         end\n"
    )
}

/// Keys of every warp option currently appended to this conversation.
pub fn warp_option_keys(con: &ConFile) -> Vec<String> {
    let mut out = Vec::new();
    for menu in &con.menus {
        for it in &menu.items {
            for f in [&it.check_func, &it.click_func] {
                if let Some(rest) = f.strip_prefix("QW") {
                    if let Some((key, _)) = rest.split_once('_') {
                        let key = key.to_string();
                        if !out.contains(&key) {
                            out.push(key);
                        }
                    }
                }
            }
        }
    }
    out
}

/// Append a warp option to the NPC's existing dialog. Replaces the option if
/// `key` is already present, so re-running is a refresh rather than a duplicate.
pub fn append_warp_option(
    con: &mut ConFile,
    key: &str,
    trigger: &str,
    s: &WarpStrings,
) -> Result<()> {
    if con.menus.is_empty() {
        bail!(".CON has no menus — not an NPC conversation?");
    }
    remove_warp_option(con, key);

    let p = warp_prefix(key);
    let base = con.menus.len() as i32;
    // [base] the NPC's "are you sure?" message -> [base+1] the yes/no choices.
    con.menus.push(ConMenu {
        items: vec![menu_item(SC_MSG_NPCSAY, base + 1, "", "", s.confirm)],
    });
    con.menus.push(ConMenu {
        items: vec![
            menu_item(
                SC_MSG_CLOSE,
                -1,
                "",
                &format!("{p}GO"),
                s.accept_option,
            ),
            menu_item(SC_MSG_CLOSE, -1, "", "", s.decline_option),
        ],
    });

    con.menus[0].items.push(menu_item(
        SC_MSG_PLAYERSELECT,
        base,
        &format!("{p}CHK"),
        "",
        s.hook_option,
    ));

    appendix_upsert_named(&mut con.appendix, &p, &warp_option_lua(key, trigger));
    Ok(())
}

/// Remove warp option `key`: its menu items, the menus only it referenced, and
/// its appendix section. Mirrors `remove_quest_option`.
pub fn remove_warp_option(con: &mut ConFile, key: &str) -> bool {
    let p = warp_prefix(key);
    let is_ours = |it: &ConMenuItem| it.check_func.starts_with(&p) || it.click_func.starts_with(&p);

    let mut doomed: BTreeSet<usize> = BTreeSet::new();
    let mut stack: Vec<usize> = Vec::new();
    let mut found = false;
    for menu in &con.menus {
        for it in &menu.items {
            if is_ours(it) {
                found = true;
                if it.child_menu >= 0 {
                    stack.push(it.child_menu as usize);
                }
            }
        }
    }
    if !found {
        return appendix_remove_named(&mut con.appendix, &p);
    }
    while let Some(mi) = stack.pop() {
        if mi >= con.menus.len() || !doomed.insert(mi) {
            continue;
        }
        for it in &con.menus[mi].items {
            if it.child_menu >= 0 {
                stack.push(it.child_menu as usize);
            }
        }
    }

    for menu in con.menus.iter_mut() {
        menu.items.retain(|it| !is_ours(it));
    }
    let referenced: BTreeSet<usize> = con
        .menus
        .iter()
        .enumerate()
        .filter(|(i, _)| !doomed.contains(i))
        .flat_map(|(_, m)| m.items.iter())
        .filter(|it| it.child_menu >= 0)
        .map(|it| it.child_menu as usize)
        .collect();
    doomed.retain(|mi| !referenced.contains(mi));

    for &mi in doomed.iter().rev() {
        con.menus.remove(mi);
        for menu in con.menus.iter_mut() {
            for it in menu.items.iter_mut() {
                if it.child_menu > mi as i32 {
                    it.child_menu -= 1;
                }
            }
        }
    }

    appendix_remove_named(&mut con.appendix, &p);
    true
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

    #[test]
    fn appended_option_prompts_before_accepting() {
        // A minimal "retail" conversation: greeting -> one close option.
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
                items: vec![item(SC_MSG_CLOSE, -1, "", "", 8)],
            },
        ];
        let bytes = build_con(&[], &messages, &menus, b"-- retail\n");
        let mut con = ConFile::parse(&bytes).unwrap();

        let s = GiverStrings::default();
        append_quest_option(&mut con, 5503, "5503-3", &s).unwrap();

        // The root menu gained the hook line: gated by CHK_accept but with NO
        // click action — accepting must go through the start-message prompt.
        let hook = &con.menus[0].items[1];
        assert_eq!(hook.mtype, SC_MSG_PLAYERSELECT);
        assert_eq!(hook.check_func, "QE5503_CHK_accept");
        assert_eq!(hook.click_func, "");
        assert_eq!(hook.str_id, s.hook_option);

        // hook -> start message (NPCSAY) -> Accept / Decline choices.
        let start = &con.menus[hook.child_menu as usize];
        assert_eq!(start.items.len(), 1);
        assert_eq!(start.items[0].mtype, SC_MSG_NPCSAY);
        assert_eq!(start.items[0].str_id, s.greeting);
        let choices = &con.menus[start.items[0].child_menu as usize];
        assert_eq!(choices.items.len(), 2);
        assert_eq!(choices.items[0].click_func, "QE5503_ACT_accept");
        assert_eq!(choices.items[0].str_id, s.accept_option);
        assert_eq!(choices.items[1].mtype, SC_MSG_CLOSE);
        assert_eq!(choices.items[1].str_id, s.decline_option);
        // Accepting leads to the after-accept response.
        let after = &con.menus[choices.items[0].child_menu as usize];
        assert_eq!(after.items[0].str_id, s.after_accept);

        // Round-trips through the serializer, and removal restores the layout.
        let rebuilt = con.rebuild();
        let mut re = ConFile::parse(&rebuilt).unwrap();
        assert_eq!(quest_option_qids(&re), vec![5503]);
        assert!(remove_quest_option(&mut re, 5503));
        assert_eq!(re.menus.len(), 2);
        assert_eq!(re.menus[0].items.len(), 1);
        assert!(re.appendix.is_empty());
    }

    fn minimal_con() -> Vec<u8> {
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
                items: vec![item(SC_MSG_CLOSE, -1, "", "", 8)],
            },
        ];
        build_con(&[], &messages, &menus, b"-- retail\n")
    }

    #[test]
    fn appended_warp_option_confirms_before_warping() {
        let mut con = ConFile::parse(&minimal_con()).unwrap();
        let s = WarpStrings {
            hook_option: 40,
            confirm: 41,
            accept_option: 42,
            decline_option: 43,
        };
        append_warp_option(&mut con, "orlo", "Oro-TravelToOrlo", &s).unwrap();

        // Root gained the hook: visible via CHK, but with no click action, so a
        // single mis-click cannot teleport the player.
        let hook = &con.menus[0].items[1];
        assert_eq!(hook.mtype, SC_MSG_PLAYERSELECT);
        assert_eq!(hook.check_func, "QWorlo_CHK");
        assert_eq!(hook.click_func, "");
        assert_eq!(hook.str_id, 40);

        // hook -> confirm message -> yes / no.
        let confirm = &con.menus[hook.child_menu as usize];
        assert_eq!(confirm.items[0].str_id, 41);
        let choices = &con.menus[confirm.items[0].child_menu as usize];
        assert_eq!(choices.items.len(), 2);
        assert_eq!(choices.items[0].click_func, "QWorlo_GO");
        assert_eq!(choices.items[0].str_id, 42);
        assert_eq!(choices.items[1].click_func, "");
        assert_eq!(choices.items[1].str_id, 43);

        let src = String::from_utf8(con.appendix.clone()).unwrap();
        assert!(src.contains("QF_doQuestTrigger(\"Oro-TravelToOrlo\")"));

        // Round-trips, and removal restores the original layout exactly.
        let rebuilt = con.rebuild();
        let mut re = ConFile::parse(&rebuilt).unwrap();
        assert_eq!(warp_option_keys(&re), vec!["orlo".to_string()]);
        assert!(remove_warp_option(&mut re, "orlo"));
        assert_eq!(re.menus.len(), 2);
        assert_eq!(re.menus[0].items.len(), 1);
        assert!(re.appendix.is_empty());
    }

    #[test]
    fn warp_and_quest_options_coexist_on_one_npc() {
        // The appendix is shared, so each feature must only ever remove its own
        // marked section — a warp option and a quest option on the same NPC.
        let mut con = ConFile::parse(&minimal_con()).unwrap();
        let ws = WarpStrings {
            hook_option: 40,
            confirm: 41,
            accept_option: 42,
            decline_option: 43,
        };
        append_warp_option(&mut con, "orlo", "Oro-TravelToOrlo", &ws).unwrap();
        append_quest_option(&mut con, 5503, "5503-3", &GiverStrings::default()).unwrap();

        let mut re = ConFile::parse(&con.rebuild()).unwrap();
        assert_eq!(warp_option_keys(&re), vec!["orlo".to_string()]);
        assert_eq!(quest_option_qids(&re), vec![5503]);
        let src = String::from_utf8(re.appendix.clone()).unwrap();
        assert!(src.contains("QWorlo_GO") && src.contains("QE5503_ACT_accept"));

        // Dropping the quest leaves the warp option intact, and vice versa.
        assert!(remove_quest_option(&mut re, 5503));
        let src = String::from_utf8(re.appendix.clone()).unwrap();
        assert!(src.contains("QWorlo_GO"), "removing a quest ate the warp Lua");
        assert_eq!(warp_option_keys(&re), vec!["orlo".to_string()]);
        assert!(remove_warp_option(&mut re, "orlo"));
        assert_eq!(re.menus.len(), 2);
        assert_eq!(re.menus[0].items.len(), 1);
        assert!(re.appendix.is_empty());
    }
}
