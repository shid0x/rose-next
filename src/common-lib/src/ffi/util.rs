use libc::*;
use log::error;

use crate::util;

#[no_mangle]
pub extern "C" fn get_bin_dir(path: *mut c_char, size: size_t) -> bool {
    let dir = util::get_bin_dir();
    let dir_path_length = dir.to_str().unwrap().len();

    if dir_path_length < size {
        unsafe {
            ::std::ptr::copy(
                dir.to_str().unwrap_or_default().as_ptr(),
                path as *mut u8,
                dir_path_length,
            );
        }
    } else {
        error!("Could not copy bin directory because buffer was too small. Path size ({}), buffer size ({})", dir_path_length, size);
        return false;
    }

    true
}
