fn main() {
    let _build = cxx_build::bridge("src/lib.rs");

    println!("cargo:rerun-if-changed=src/lib.rs");
    // cxx_build::bridge("src/main.rs")
    //     .file("src/blobstore.cc")
    //     .std("c++14")
    //     .compile("cxxbridge-demo");

    // println!("cargo:rerun-if-changed=src/main.rs");
    // println!("cargo:rerun-if-changed=src/blobstore.cc");
    // println!("cargo:rerun-if-changed=include/blobstore.h");
}
