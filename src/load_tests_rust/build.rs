use std::env;

fn main()
{
    let libaktualizr_c_path = env::var("LIBAKTUALIZR_C_PATH").unwrap();
    println!("cargo:rustc-link-search=native={}", libaktualizr_c_path);
    println!("cargo:rustc-link-lib=dylib=aktualizr-c");
}
