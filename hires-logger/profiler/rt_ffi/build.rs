use std::env;
use std::path::PathBuf;

fn main() {
    // 1. Tell cargo to link against the C++ library (libhires_rt)
    // Assume the C++ library is built *before* this crate and installed
    // to a location known to the linker (e.g., /usr/local/lib) or specified
    // via environment variables (e.g., LIBRARY_PATH).
    // A more robust solution uses cmake crate to build the C++ lib first.

    // Option A: Assume library is in standard linker path or LIBRARY_PATH
    // println!("cargo:rustc-link-lib=dylib=profiler_rt"); // Link dynamically

    // Option B: Specify path explicitly if needed (e.g., relative to build)
    // Construct the path relative to the OUT_DIR, assuming rt/build is a sibling of profiler/target/debug/build/.../out
    // This might be fragile depending on the exact build setup. Consider using the cmake crate.
    let cpp_build_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../../rt/build");
    println!("cargo:rustc-link-search=native={}", cpp_build_dir.display());
    println!("cargo:rustc-link-lib=dylib=hires_rt"); // Link statically if possible, or dylib=hires_rt


    // 2. Tell cargo to invalidate the built crate whenever the C API header changes
    // Adjust path relative to this build.rs script
    let header_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../../rt/include/rt_c.h");
    println!("cargo:rerun-if-changed={}", header_path.display());
    // Also rerun if the shared header changes
    let shared_header_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../../shared/common.h");
    println!("cargo:rerun-if-changed={}", shared_header_path.display());

    // 3. Generate Rust bindings using bindgen
    let bindings = bindgen::Builder::default()
        .header(header_path.to_str().expect("Header path is not valid UTF-8"))
        .clang_arg(format!( // Include path for rt/include
            "-I{}",
            PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
                .join("../../rt/include")
                .display()
        ))
        .clang_arg(format!( // Include path for shared/
            "-I{}",
            PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
                .join("../../shared")
                .display()
        ))
        .derive_default(true)
        // Tell bindgen the names of functions/types to generate bindings for
        // .allowlist_function("hires_.*") // Allowlist C functions
        // .allowlist_type("HiResLoggerConnHandle") // Allowlist opaque handle
        // .allowlist_type("shared_ring_buffer_t") // Generate shared struct
        // .allowlist_type("log_entry_t")          // Generate log entry struct
        // .allowlist_var("LOG_FLAG_.*")           // Generate constants
        // .allowlist_var("RING_BUFFER_.*")        // Generate constants
        // Invalidate the built crate whenever any included header file changes
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        // Use core::ffi types instead of std::os::raw
        // .use_core()
        // .ctypes_prefix("::core::ffi")
        // Finish the builder and generate the bindings.
        .generate()
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");

    // Remove the old commented out block as it's now integrated above
    /* // Uncomment to enable bindgen
    ... (removed old commented block) ...
    */
}