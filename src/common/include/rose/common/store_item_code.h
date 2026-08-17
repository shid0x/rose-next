#pragma once

/// How a shop slot in LIST_SELL.STB names an item.
///
/// Retail packs a slot as the decimal `type * 1000 + id`, which cannot express an
/// item id above 999 -- a weapon at row 1379 encodes as 9379 and decodes back as
/// type 9 id 379, a subweapon. That ceiling is *only* in this packing:
/// `tagBaseITEM` stores the type in 5 bits and the item number in **11** (0..2047),
/// and the buy path sends a tab/column pair rather than an item id, so nothing on
/// the wire, in the database or in the inventory ever sees this encoding.
///
/// So a wide form is added alongside the legacy one rather than replacing it:
///
///     id <= 999   ->  type * 1000   + id      (unchanged; every shipped row)
///     id >  999   ->  type * 100000 + id
///
/// The two ranges cannot collide. The legacy form tops out at
/// `ITEM_TYPE_MONEY * 1000 + 999` = 31,999, so any value at or above kWideBase is
/// unambiguously wide, and every row that exists today keeps decoding bit for bit
/// as it did before.
///
/// **This header exists so the client and the server cannot drift.** The encoding
/// is read in exactly two places -- `CObjNPC::Get_SellITEM` (server, the buy) and
/// `CStore::ChangeStore` (client, the display) -- and if they ever disagree the
/// player is shown one item and sold another. Do not inline a copy of this logic
/// at either site; that is the whole point of putting it here, and it mirrors why
/// `Rose::Combat::is_projectile_presented_skill` lives in common/.
///
/// Validity is deliberately *not* decided here: `tagBaseITEM::Init(type, no)` ->
/// `IsValidITEM` already checks the type range and the id against the real STB row
/// count. This only splits the packed integer.

namespace Rose::Store {

/// tagBaseITEM: m_cType is 5 bits, m_nItemNo is 11.
constexpr int kMaxItemType = 31;
constexpr int kMaxItemNo = 2047;

/// Highest id the legacy decimal packing can carry.
constexpr int kLegacyMaxItemNo = 999;

/// Legacy values top out at 31 * 1000 + 999; anything from here up is the wide form.
constexpr int kWideBase = 100000;

/// Pack a (type, id) pair for storage in a LIST_SELL slot.
constexpr int
encode_store_item(int item_type, int item_no) {
    return (item_no <= kLegacyMaxItemNo) ? (item_type * 1000 + item_no)
                                         : (item_type * kWideBase + item_no);
}

/// Split a stored slot value. Returns false for an empty slot or a value that
/// cannot be a type/id pair at all; the caller should then skip the slot.
constexpr bool
decode_store_item(int packed, int& item_type, int& item_no) {
    if (packed <= 0) {
        return false;
    }

    if (packed >= kWideBase) {
        item_type = packed / kWideBase;
        item_no = packed % kWideBase;
    } else {
        item_type = packed / 1000;
        item_no = packed % 1000;
    }

    if (item_type <= 0 || item_type > kMaxItemType) {
        return false;
    }
    if (item_no <= 0 || item_no > kMaxItemNo) {
        return false;
    }
    return true;
}

} // namespace Rose::Store
