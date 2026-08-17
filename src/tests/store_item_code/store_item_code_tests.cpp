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

    std::cout << "store_item_code_tests passed\n";
    return 0;
}
