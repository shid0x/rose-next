use std::env;
use std::path::{Path, PathBuf};

use cbindgen;
use flatc_rust;


fn main() {
  let crate_dir = env!("CARGO_MANIFEST_DIR");
  let config = cbindgen::Config::from_root_or_default(&crate_dir);

  cbindgen::generate_with_config(&crate_dir, config)
    .expect("Failed to generate bindings")
    .write_to_file("include/rose/common/common_interface.h");

  let config = match env::var("PROFILE").as_deref() {
    Ok("release") => "release",
    _ => "debug",
  };

  let crate_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
  let root_dir = &crate_dir.parent().unwrap().parent().unwrap();
  let packet_dir = crate_dir.join("packets");

  let mut out_dir_cpp = PathBuf::from(&root_dir);
  out_dir_cpp.push("build");
  out_dir_cpp.push("gen");
  out_dir_cpp.push("common");
  out_dir_cpp.push("rose");
  out_dir_cpp.push("network");
  out_dir_cpp.push("packets");

  let out_dir_rs = PathBuf::from(env::var("OUT_DIR").unwrap());

  let mut flatc_path = PathBuf::from(&root_dir);
  flatc_path.push("bin");
  flatc_path.push(config);
  flatc_path.push("thirdparty");
  flatc_path.push("flatc.exe");

  let flatbuffers = &[
    "common/char_move_mode.fbs",
    "common/vec3.fbs",
    "char_create_req.fbs",
    "char_move_attack.fbs",
    "char_move.fbs",
    "combat.fbs",
    "login_rep.fbs",
    "login_req.fbs",
    "packet_data.fbs",
    "update_stats.fbs",
  ];

  eprintln!("{}", flatc_path.display());

  let input_paths: Vec<PathBuf> = flatbuffers.iter().map(|p|
    packet_dir.join(p)
  ).collect();
  let input: Vec<&Path> = input_paths.iter().map(|p| p.as_path()).collect();

  let flatc = flatc_rust::Flatc::from_path(&flatc_path);
  flatc.check().expect(&format!("{} is not valid", &flatc_path.display()));

  let args_rs = flatc_rust::Args {
    lang: "rust",
    inputs: input.as_slice(),
    out_dir: &out_dir_rs,
    ..Default::default()
  };

  let args_cpp = flatc_rust::Args {
    lang: "cpp",
    inputs: &input,
    out_dir: &out_dir_cpp,
    extra: &["--scoped-enums"],
    ..Default::default()
  };

  for path in &input {
    println!("cargo:rerun-if-changed={}", path.display());
  }

  flatc.run(args_rs).expect("flatc: Failed to generate rust code");
  flatc.run(args_cpp).expect("flatc: Failed to generate cpp code");
}
