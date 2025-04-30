use std::env;
use std::path::PathBuf;

fn main() {
    // assume the libhires_rt.so is already built at this stage.
    let cpp_build_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../../build/rt");
    println!("cargo:rustc-link-search=native={}", cpp_build_dir.display());
    // should consider static library and static link???
    println!("cargo:rustc-link-lib=dylib=hires_rt");

    let header_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../../rt/include/rt_c.h");
    println!("cargo:rerun-if-changed={}", header_path.display());
    let shared_header_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../../shared/common.h");
    println!("cargo:rerun-if-changed={}", shared_header_path.display());

    let bindings = bindgen::Builder::default()
        .header(header_path.to_str().expect("Header path is not valid UTF-8"))
        .clang_arg(format!(
            "-I{}",
            PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
                .join("../../rt/include")
                .display()
        ))
        .clang_arg(format!( 
            "-I{}",
            PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
                .join("../../shared")
                .display()
        ))
        .derive_default(true)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        // Use core::ffi types instead of std::os::raw
        // .use_core()
        // .ctypes_prefix("::core::ffi")
        .generate()
        .expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}