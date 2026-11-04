//! Which codec `Codec::Auto` picks for each item and monster table of real
//! dumps, so a change to `text::detect` can be checked against all of them.
//!
//!     set ROSE_DUMPS=C:\dump one;C:\dump two
//!     cargo test -p npc-shop-editor --test codec_dumps -- --ignored --nocapture
use std::collections::BTreeMap;
use std::path::Path;

use npc_shop_editor::assets::Assets;
use npc_shop_editor::text::{Codec, RawStb};

const TABLES: [&str; 15] = [
    "LIST_FACEITEM", "LIST_CAP", "LIST_BODY", "LIST_ARMS", "LIST_FOOT", "LIST_BACK", "LIST_JEWEL",
    "LIST_WEAPON", "LIST_SUBWPN", "LIST_USEITEM", "LIST_JEMITEM", "LIST_NATURAL", "LIST_QUESTITEM",
    "LIST_PAT", "LIST_NPC",
];

#[test]
#[ignore]
fn auto_codec_per_dump() {
    let dumps = std::env::var("ROSE_DUMPS").expect("set ROSE_DUMPS to ;-separated folders");
    for dump in dumps.split(';').filter(|d| !d.trim().is_empty()) {
        let assets = match Assets::open(Path::new(dump.trim())) {
            Ok(assets) => assets,
            Err(e) => {
                println!("{dump}: cannot open: {e:#}");
                continue;
            }
        };
        let mut picks: BTreeMap<String, Vec<&str>> = BTreeMap::new();
        for table in TABLES {
            let Ok(bytes) = assets.read(&format!("3DDATA/STB/{table}.STB")) else {
                continue;
            };
            let Ok(stb) = RawStb::parse(&bytes) else {
                continue;
            };
            let codec = Codec::Auto.resolve(stb.cells());
            picks.entry(codec.label().to_string()).or_default().push(table);
        }
        println!("{dump}:");
        for (codec, tables) in picks {
            println!("  {codec:<9} {}", tables.join(" "));
        }
    }
}
