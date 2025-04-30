use std::path::PathBuf;
use std::env;

fn main() {
    let libhires_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap()).join("../build/rt/");
    println!("cargo:rustc-link-search=native={}", libhires_path.display());
    println!("cargo:rustc-link-lib=dylib=hires_rt");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", libhires_path.display());
}
