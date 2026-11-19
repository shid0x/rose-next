#pragma once

#include "rose/common/store_item_code.h"

/// How a cell in ITEM_DROP.STB names an item.
///
/// Drop cells use **the same packing as shop slots** (see store_item_code.h):
/// `type * 1000 + id` for ids up to 999, and the wide `type * 100000 + id`
/// above that. What differs is not the packing but the sentinels a drop cell
/// can carry, which is why this is a separate header rather than a second use
/// of `Rose::Store`:
///
///     0            empty cell
///     1 .. 4       *redirect group* -- re-roll at index 25 + n*5 + rand(5)
///     5 .. 1000    junk; retail's own loader zeroes these
///     > 1000       a packed item, legacy or wide
///
/// The ranges cannot collide. The legacy form tops out at
/// `ITEM_TYPE_MONEY * 1000 + 999` = 31,999, so anything at or above
/// `Rose::Store::kWideBase` is unambiguously wide, and every drop cell that
/// exists today keeps decoding exactly as it did.
///
/// Widening matters because the imported Jrose weapon sets sit at ids
/// 1381-1453. Under the legacy packing alone they can never drop: 8 * 1000 +
/// 1381 = 9381, which decodes as type 9 id 381, a subweapon. Shops escaped
/// this in store_item_code.h; drops did not, so the best items Karkia owns had
/// no way to reach a loot table. Nothing on the wire limits them --
/// `tagBaseITEM` stores the type in 5 bits and the item number in **11**
/// (0..2047).
///
/// **This header exists so the three readers cannot drift.** A drop cell is
/// decoded in `CCal::Get_DropITEM` (the authority, common/), in
/// `CLIB_GameSRV::CheckSTB_DropITEM` (the server's start-up sanitiser, which
/// *zeroes* cells it cannot make sense of -- so a decoder that does not know
/// the wide form does not merely ignore a wide cell, it deletes it) and in
/// `CMonsterInspectorPanel` (the client's drop-list display). Do not inline a
/// copy of this logic at any of them.
///
/// Validity is deliberately *not* decided here, exactly as in the store
/// header: the callers check the type range and the id against the real STB
/// row count. This only splits the packed integer.

namespace Rose::Drop {

/// Cells at or below this are sentinels, never a packed item.
constexpr int kMaxSentinel = 1000;

/// Cells 1..4 select a redirect group instead of naming an item.
constexpr int kMaxRedirectGroup = 4;

/// Where a redirect group's re-roll reads from: index 25 + group * 5 + rand(5), which
/// DROPITEM_ITEMNO reads as STB column 26 + group * 5 + k. ITEM_DROP.STB names its columns
/// Item 1-20 (1-20), Common 1-10 (21-30) and Valuable 1-20 (31-50): four groups of five
/// at columns 31-35, 36-40, 41-45, 46-50. The retail base was 26 (2026-11-19 and before),
/// one column late: "Valuable 1" (filled in 211 of ruff's 228 tables with redirects) was
/// never read, every group took the next group's first cell, and group 4 reached for a
/// column 51 that does not exist. doc/project-drops.md section 7.
constexpr int kRedirectBaseColumn = 25;
constexpr int kRedirectGroupWidth = 5;

/// True for a cell that names a redirect group rather than an item.
constexpr bool
is_redirect_group(int packed) {
    return packed >= 1 && packed <= kMaxRedirectGroup;
}

/// First column of a redirect group's re-roll window.
constexpr int
redirect_column(int group) {
    return kRedirectBaseColumn + group * kRedirectGroupWidth;
}

/// Split a drop cell. False for an empty cell, a redirect group, junk below
/// the sentinel ceiling, or a value that cannot be a type/id pair at all --
/// callers must handle `is_redirect_group` themselves *before* calling this.
constexpr bool
decode_drop_item(int packed, int& item_type, int& item_no) {
    if (packed <= kMaxSentinel) {
        return false;
    }
    return Rose::Store::decode_store_item(packed, item_type, item_no);
}

/// Pack a (type, id) pair into a drop cell. Shares the store encoding.
constexpr int
encode_drop_item(int item_type, int item_no) {
    return Rose::Store::encode_store_item(item_type, item_no);
}

} // namespace Rose::Drop
