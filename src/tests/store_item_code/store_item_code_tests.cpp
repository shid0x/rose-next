#include "rose/common/drop_item_code.h"
#include "rose/common/store_item_code.h"

#include <cstdlib>
#include <iostream>

using Rose::Store::decode_store_item;
using Rose::Store::encode_store_item;
using Rose::Store::kLegacyMaxItemNo;
using Rose::Store::kMaxItemNo;
using Rose::Store::kMaxItemType;
using Rose::Store::kWideBase;

namespace {

void
expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void
expect_decodes(int packed, int want_type, int want_no, const char* message) {
    int type = -1, no = -1;
    const bool ok = decode_store_item(packed, type, no);
    if (!ok || type != want_type || no != want_no) {
        std::cerr << "FAILED: " << message << " -- decode(" << packed << ") gave ok=" << ok
                  << " type=" << type << " no=" << no << ", wanted type=" << want_type
                  << " no=" << want_no << "\n";
        std::exit(1);
    }
}

void
expect_rejected(int packed, const char* message) {
    int type = -1, no = -1;
    if (decode_store_item(packed, type, no)) {
        std::cerr << "FAILED: " << message << " -- decode(" << packed
                  << ") unexpectedly succeeded with type=" << type << " no=" << no << "\n";
        std::exit(1);
    }
}

} // namespace

int
main() {
    // ---- the legacy form must be bit-for-bit what it always was -------------
    // These are real values lifted from the shipped LIST_SELL.STB.
    expect_decodes(10631, 10, 631, "shipped row 200 slot 0 (use item)");
    expect_decodes(8301, 8, 301, "shipped row 221 slot 0 (magic weapon)");
    expect_decodes(10001, 10, 1, "shipped row 222 slot 0 (medicine)");
    expect_decodes(2977, 2, 977, "cap near the legacy ceiling");

    // The old decoder was literally `iItem / 1000` and `iItem % 1000` guarded by
    // `if (1001 > iItem) return;`. Sweep the whole legacy space and require an
    // exact match against that, so this can never quietly change meaning.
    for (int type = 1; type <= kMaxItemType; ++type) {
        for (int no = 1; no <= kLegacyMaxItemNo; ++no) {
            const int packed = type * 1000 + no;
            if (packed < 1001) {
                continue;
            }
            int got_type = -1, got_no = -1;
            const bool ok = decode_store_item(packed, got_type, got_no);
            expect(ok, "legacy value should decode");
            expect(got_type == packed / 1000, "legacy type must match the original arithmetic");
            expect(got_no == packed % 1000, "legacy id must match the original arithmetic");
            expect(encode_store_item(type, no) == packed, "legacy encode must round-trip");
        }
    }

    // ---- the wide form ------------------------------------------------------
    // The case this whole change exists for: our lv230 weapons at LIST_WEAPON
    // rows 1355-1367 and the lv210 ones at 1368-1379.
    expect_decodes(encode_store_item(8, 1355), 8, 1355, "lv230 weapon, first row");
    expect_decodes(encode_store_item(8, 1379), 8, 1379, "lv210 weapon, last row");
    expect(encode_store_item(8, 1379) == 801379, "wide encoding should be type*100000 + id");

    // Under the old packing this exact item collided with a different table:
    // 8 * 1000 + 1379 = 9379 decodes as type 9 (subweapon) id 379.
    expect_decodes(9379, 9, 379, "the old collision value still means what it always meant");
    expect(encode_store_item(8, 1379) != 9379,
        "the wide form must not collide with the legacy one");

    // Round-trip every representable pair across both forms.
    for (int type = 1; type <= kMaxItemType; ++type) {
        for (int no = 1; no <= kMaxItemNo; ++no) {
            int got_type = -1, got_no = -1;
            const int packed = encode_store_item(type, no);
            expect(decode_store_item(packed, got_type, got_no), "every pair should decode");
            expect(got_type == type && got_no == no, "every pair should round-trip exactly");
        }
    }

    // ---- the boundary between the two forms ---------------------------------
    expect_decodes(encode_store_item(1, kLegacyMaxItemNo),
        1,
        kLegacyMaxItemNo,
        "last legacy id stays legacy");
    expect_decodes(encode_store_item(1, kLegacyMaxItemNo + 1),
        1,
        kLegacyMaxItemNo + 1,
        "first wide id crosses over");
    expect(encode_store_item(1, kLegacyMaxItemNo) < kWideBase, "legacy form stays below the base");
    expect(encode_store_item(1, kLegacyMaxItemNo + 1) >= kWideBase, "wide form starts at the base");

    // The ranges cannot overlap: the largest legacy value is 31,999, well under
    // kWideBase. If someone lowers kWideBase this is what should fail first.
    expect(kMaxItemType * 1000 + kLegacyMaxItemNo < kWideBase,
        "legacy space must fit entirely below kWideBase");

    // ---- rejected input -----------------------------------------------------
    expect_rejected(0, "an empty slot");
    expect_rejected(-1, "a negative value");
    expect_rejected(999, "a value with no type component");
    expect_rejected(1000, "type 1 id 0 -- the old code rejected everything below 1001");
    expect_rejected((kMaxItemType + 1) * 1000 + 5, "a type past the 5-bit field");
    expect_rejected(kWideBase * (kMaxItemType + 1) + 5, "a wide type past the 5-bit field");
    expect_rejected(kWideBase * 8 + (kMaxItemNo + 1), "an id past the 11-bit field");

    // ---- drop cells: same packing, different sentinels -----------------------
    // rose/common/drop_item_code.h. Covered here rather than in a project of
    // its own because the two headers share one encoding and must not drift.
    {
        using Rose::Drop::decode_drop_item;
        using Rose::Drop::encode_drop_item;
        using Rose::Drop::is_redirect_group;
        using Rose::Drop::kMaxRedirectGroup;
        using Rose::Drop::kMaxSentinel;
        using Rose::Drop::redirect_column;

        auto drop_decodes = [](int packed, int want_type, int want_no, const char* message) {
            int type = -1, no = -1;
            const bool ok = decode_drop_item(packed, type, no);
            if (!ok || type != want_type || no != want_no) {
                std::cerr << "FAILED: " << message << " -- decode_drop_item(" << packed
                          << ") gave ok=" << ok << " type=" << type << " no=" << no
                          << ", wanted type=" << want_type << " no=" << want_no << "\n";
                std::exit(1);
            }
        };
        auto drop_rejected = [](int packed, const char* message) {
            int type = -1, no = -1;
            if (decode_drop_item(packed, type, no)) {
                std::cerr << "FAILED: " << message << " -- decode_drop_item(" << packed
                          << ") unexpectedly succeeded\n";
                std::exit(1);
            }
        };

        // The packing is shared with shop slots, so every existing cell is unchanged.
        drop_decodes(10001, 10, 1, "a legacy use-item cell");
        drop_decodes(8006, 8, 6, "a legacy weapon cell");

        // The whole point: ids above 999 become droppable. 1381 is the first
        // imported Jrose weapon, which under the legacy packing alone would be
        // 9381 -- type 9 id 381, a subweapon.
        drop_decodes(encode_drop_item(8, 1381), 8, 1381, "a wide weapon cell");
        expect(8 * 1000 + 1381 == 9381, "the collision this encoding avoids");
        drop_decodes(encode_drop_item(8, kMaxItemNo), 8, kMaxItemNo, "the largest droppable id");

        // Sentinels are the drop-only part and must never decode as items.
        drop_rejected(0, "an empty drop cell");
        for (int g = 1; g <= kMaxRedirectGroup; ++g) {
            expect(is_redirect_group(g), "1..4 are redirect groups");
            drop_rejected(g, "a redirect group is not an item");
        }
        expect(!is_redirect_group(0), "0 is empty, not a redirect group");
        expect(!is_redirect_group(kMaxRedirectGroup + 1), "5 is junk, not a redirect group");
        drop_rejected(5, "junk below the sentinel ceiling");
        drop_rejected(kMaxSentinel, "the sentinel ceiling itself is not an item");
        drop_decodes(kMaxSentinel + 1, 1, 1, "one past the ceiling is type 1 id 1");

        // Redirect windows are contiguous and non-overlapping, and match the
        // 25 + g*5 + rand(5) the callers use: index 30 = STB column 31 ("Valuable 1"), and
        // group 4's last cell is index 49 = column 50, the table's last.
        expect(redirect_column(1) == 30, "group 1 reads from index 30 (column 31)");
        expect(redirect_column(kMaxRedirectGroup) + Rose::Drop::kRedirectGroupWidth - 1 == 49,
            "group 4 ends at index 49 (column 50)");
        for (int g = 1; g < kMaxRedirectGroup; ++g) {
            expect(redirect_column(g) + Rose::Drop::kRedirectGroupWidth == redirect_column(g + 1),
                "redirect windows must be contiguous");
        }

        // A wide cell must not be mistaken for a sentinel at any type.
        for (int type = 1; type <= kMaxItemType; ++type) {
            expect(encode_drop_item(type, kLegacyMaxItemNo + 1) > kMaxSentinel,
                "no wide cell may fall into the sentinel range");
        }
    }

    std::cout << "store_item_code_tests passed\n";
    return 0;
}
