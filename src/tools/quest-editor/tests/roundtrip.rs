//! Phase 1 acceptance test: every retail `.QSD` file must parse and re-serialize
//! byte-for-byte identically. This is the gate that proves the codec is safe to
//! build a generator on top of — if it passes, the binary format (including
//! struct padding and uninitialized-fill bytes) is fully accounted for.

use std::path::PathBuf;

use quest_editor::qsd::QsdFile;

/// Locate the repo's QuestData dir relative to this crate. The crate lives at
/// `src/tools/quest-editor`, so the repo root is three levels up.
fn questdata_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../../data/3DDATA/QUESTDATA")
        .canonicalize()
        .unwrap_or_else(|_| {
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../data/3DDATA/QUESTDATA")
        })
}

fn collect_qsd(dir: &std::path::Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    if let Ok(rd) = std::fs::read_dir(dir) {
        for entry in rd.flatten() {
            let path = entry.path();
            if path
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| e.eq_ignore_ascii_case("qsd"))
            {
                out.push(path);
            }
        }
    }
    out.sort();
    out
}

#[test]
fn all_retail_qsd_round_trip_byte_exact() {
    let dir = questdata_dir();
    let files = collect_qsd(&dir);

    // If the data tree isn't present (e.g. a checkout without game data), skip
    // loudly rather than fail — the test is only meaningful with the data.
    if files.is_empty() {
        eprintln!(
            "SKIP: no .QSD files found under {} — game data not present",
            dir.display()
        );
        return;
    }

    let mut failures = Vec::new();
    for path in &files {
        let raw = std::fs::read(path).expect("read qsd");
        match QsdFile::parse(&raw) {
            Ok(parsed) => {
                let reencoded = parsed.to_bytes();
                if reencoded != raw {
                    let first_diff = raw
                        .iter()
                        .zip(&reencoded)
                        .position(|(a, b)| a != b)
                        .map(|p| p.to_string())
                        .unwrap_or_else(|| format!("len {} vs {}", raw.len(), reencoded.len()));
                    failures.push(format!(
                        "DRIFT {} (first diff @ {first_diff})",
                        path.display()
                    ));
                }
            }
            Err(e) => failures.push(format!("PARSE {}: {e:#}", path.display())),
        }
    }

    assert!(
        failures.is_empty(),
        "{}/{} QSD files failed round-trip:\n{}",
        failures.len(),
        files.len(),
        failures.join("\n")
    );

    eprintln!("OK: {} QSD files round-tripped byte-exact", files.len());
}
