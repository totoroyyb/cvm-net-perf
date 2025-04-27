use std::env;
use std::path::PathBuf;

fn main() {
    // 1. Tell cargo to link against the C++ library (libprofiler_rt)
    // Assume the C++ library is built *before* this crate and installed
    // to a location known to the linker (e.g., /usr/local/lib) or specified
    // via environment variables (e.g., LIBRARY_PATH).
    // A more robust solution uses cmake crate to build the C++ lib first.

    // Option A: Assume library is in standard linker path or LIBRARY_PATH
    println!("cargo:rustc-link-lib=dylib=profiler_rt"); // Link dynamically

    // Option B: Specify path explicitly if needed (e.g., relative to build)
    // let cpp_build_dir = PathBuf::from(env::var("OUT_DIR").unwrap()).join("../../profiler_rt_cpp/build"); // Adjust path
    // println!("cargo:rustc-link-search=native={}", cpp_build_dir.display());
    // println!("cargo:rustc-link-lib=dylib=profiler_rt");


    // 2. Tell cargo to invalidate the built crate whenever the C API header changes
    // Adjust path relative to this build.rs script
    let header_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../../rt/include/c_api.h");
    println!("cargo:rerun-if-changed={}", header_path.display());
    // Also rerun if the shared header changes
    let shared_header_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../../shared/common.h");
     println!("cargo:rerun-if-changed={}", shared_header_path.display());


    // 3. Generate Rust bindings using bindgen (optional but recommended)
    // Add bindgen as a build dependency in profiler_rt_sys/Cargo.toml:
    // [build-dependencies]
    // bindgen = "0.66" # Check for latest version

    /* // Uncomment to enable bindgen
    println!("cargo:rerun-if-changed={}", header_path.display()); // Already done above

    let bindings = bindgen::Builder::default()
        .header(header_path.to_str().unwrap())
        // Also include the shared header if needed for struct defs within C API header
        .clang_arg(format!(
            "-I{}",
            PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
                .join("../../shared_include")
                .display()
        ))
        // Tell bindgen the names of functions/types to generate bindings for
        .allowlist_function("profiler_.*")
        .allowlist_type("ProfilerConnectionHandle")
        .allowlist_type("shared_ring_buffer_t") // Generate if needed by C API usage
        .allowlist_type("log_entry_t")          // Generate if needed by C API usage
        .allowlist_var("LOG_FLAG_.*")           // Generate constants
        .allowlist_var("RING_BUFFER_.*")        // Generate constants
        // Invalidate the built crate whenever any included header file changes
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        // Use core::ffi types instead of std::os::raw
        .use_core()
        .ctypes_prefix("::core::ffi")
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
    */
}