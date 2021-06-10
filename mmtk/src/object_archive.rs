use libc::c_void;
use mmtk::util::address::Address;
use std::sync::RwLock;

const INITIAL_ARCHIVE_SIZE: usize = 10000;
const ADDITIONAL_ARCHIVE_SIZE: usize = 1000;

#[no_mangle]
pub extern "C" fn tph_archive_new() -> *const c_void {
    Box::into_raw(Box::new(ObjectArchive::new())) as *const c_void
}

#[no_mangle]
pub extern "C" fn tph_archive_delete(arch: *mut c_void) {
    unsafe {
        Box::from_raw(arch as *mut ObjectArchive);
    };
}

#[no_mangle]
pub extern "C" fn tph_archive_iter_reset(arch: *mut c_void) {
    let mut arch = unsafe { Box::from_raw(arch as *mut ObjectArchive) };
    arch.reset_iterator();
    Box::into_raw(arch);
}

#[no_mangle]
pub extern "C" fn tph_archive_iter_next(arch: *mut c_void) -> *mut c_void {
    let mut arch = unsafe { Box::from_raw(arch as *mut ObjectArchive) };
    let res = arch.next_object();
    Box::into_raw(arch);
    res.to_mut_ptr()
}

#[no_mangle]
pub extern "C" fn tph_archive_inner_to_obj(
    arch: *mut c_void,
    inner_ptr: *mut c_void,
) -> *mut c_void {
    let arch = unsafe { Box::from_raw(arch as *mut ObjectArchive) };
    let res = arch.inner_addr_to_object(Address::from_mut_ptr(inner_ptr));
    Box::into_raw(arch);
    res.to_mut_ptr()
}

#[no_mangle]
pub extern "C" fn tph_archive_obj_to_isolate(
    arch: *mut c_void,
    obj_ptr: *mut c_void,
) -> *mut c_void {
    let arch = unsafe { Box::from_raw(arch as *mut ObjectArchive) };
    let res = arch.object_to_isolate(Address::from_mut_ptr(obj_ptr));
    Box::into_raw(arch);
    res.to_mut_ptr()
}

#[no_mangle]
pub extern "C" fn tph_archive_obj_to_space(arch: *mut c_void, obj_ptr: *mut c_void) -> u8 {
    let arch = unsafe { Box::from_raw(arch as *mut ObjectArchive) };
    let res = arch.object_to_space(Address::from_mut_ptr(obj_ptr));
    Box::into_raw(arch);
    res
}

#[no_mangle]
pub extern "C" fn tph_archive_insert(
    arch: *mut c_void,
    obj_ptr: *mut c_void,
    iso_ptr: *mut c_void,
    space: u8,
) {
    let obj_addr = Address::from_mut_ptr(obj_ptr);
    let iso_addr = Address::from_mut_ptr(iso_ptr);
    let mut arch = unsafe { Box::from_raw(arch as *mut ObjectArchive) };
    arch.insert_object(obj_addr, iso_addr, space);
    Box::into_raw(arch);
}

#[no_mangle]
pub extern "C" fn tph_archive_remove(arch: *mut c_void, obj_ptr: *mut c_void) {
    let obj_addr = Address::from_mut_ptr(obj_ptr);
    let mut arch = unsafe { Box::from_raw(arch as *mut ObjectArchive) };
    arch.remove_object(obj_addr);
    Box::into_raw(arch);
}

pub struct ObjectArchive {
    sorted_addr_list: RwLock<Vec<usize>>,
    isolate_list: RwLock<Vec<usize>>,
    space_list: Vec<u8>,
    iter_pos: usize,
    iter_len: usize,
}

impl ObjectArchive {
    pub fn new() -> ObjectArchive {
        ObjectArchive {
            sorted_addr_list: RwLock::new(Vec::with_capacity(INITIAL_ARCHIVE_SIZE)),
            isolate_list: RwLock::new(Vec::with_capacity(INITIAL_ARCHIVE_SIZE)),
            space_list: Vec::with_capacity(INITIAL_ARCHIVE_SIZE),
            iter_pos: 0usize,
            iter_len: 0usize,
        }
    }

    pub fn insert_object(&mut self, obj_addr: Address, isolate: Address, space: u8) {
        let mut lst = match self.sorted_addr_list.write() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.insert: LST LOCK ACQ failed with err: {:#?}", err);
            }
        };
        let mut iso_lst = match self.isolate_list.write() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.insert: ISO LOCK ACQ failed with err: {:#?}", err);
            }
        };

        assert_eq!(lst.len(), iso_lst.len());

        if lst.capacity() == lst.len() {
            lst.reserve(ADDITIONAL_ARCHIVE_SIZE);
            iso_lst.reserve(ADDITIONAL_ARCHIVE_SIZE);
            self.space_list.reserve(ADDITIONAL_ARCHIVE_SIZE);
        }
        match lst.binary_search(&obj_addr.as_usize()) {
            Ok(_) => {
                debug!("OA.insert: Object {:?} already archived", obj_addr);
            }
            Err(idx) => {
                lst.insert(idx, obj_addr.as_usize());
                iso_lst.insert(idx, isolate.as_usize());
                self.space_list.insert(idx, space);
            }
        };
    }

    pub fn remove_object(&mut self, obj_addr: Address) {
        let mut lst = match self.sorted_addr_list.write() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.remove: LST LOCK ACQ failed with err: {:#?}", err);
            }
        };
        let mut iso_lst = match self.isolate_list.write() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.remove: ISO LOCK ACQ failed with err: {:#?}", err);
            }
        };
        assert_eq!(lst.len(), iso_lst.len());

        let idx = match lst.binary_search(&obj_addr.as_usize()) {
            Ok(idx) => idx,
            Err(_) => {
                panic!("OA.remove: Object {:?} not archived!", obj_addr);
            }
        };
        lst.remove(idx);
        iso_lst.remove(idx);
        self.space_list.remove(idx);
    }

    pub fn inner_addr_to_object(&self, inner_addr: Address) -> Address {
        let lst = match self.sorted_addr_list.read() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.inner_addr_to_object: LOCK ACQ failed: {:#?}", err);
            }
        };
        let idx = match lst.binary_search(&inner_addr.as_usize()) {
            Ok(idx) => idx,
            Err(idx) => idx - 1,
        };
        unsafe { Address::from_usize(lst[idx]) }
    }

    pub fn object_to_isolate(&self, obj_addr: Address) -> Address {
        let lst = match self.sorted_addr_list.read() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.object_to_isolate: LST LOCK ACQ failed: {:#?}", err);
            }
        };
        let iso_lst = match self.isolate_list.read() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.object_to_isolate: ISO LOCK ACQ failed: {:#?}", err);
            }
        };
        assert_eq!(lst.len(), iso_lst.len());
        let idx = match lst.binary_search(&obj_addr.as_usize()) {
            Ok(idx) => idx,
            Err(idx) => idx - 1,
        };
        unsafe { Address::from_usize(iso_lst[idx]) }
    }

    pub fn object_to_space(&self, obj_addr: Address) -> u8 {
        let lst = match self.sorted_addr_list.read() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.object_to_isolate: LST LOCK ACQ failed: {:#?}", err);
            }
        };
        let idx = match lst.binary_search(&obj_addr.as_usize()) {
            Ok(idx) => idx,
            Err(idx) => idx - 1,
        };
        self.space_list[idx]
    }

    pub fn reset_iterator(&mut self) {
        let lst = match self.sorted_addr_list.read() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.reset_iterator: LOCK ACQ failed: {:#?}", err);
            }
        };
        self.iter_pos = 0;
        self.iter_len = lst.len();
    }

    pub fn next_object(&mut self) -> Address {
        let lst = match self.sorted_addr_list.read() {
            Ok(res) => res,
            Err(err) => {
                panic!("OA.inner_addr_to_object: LOCK ACQ failed: {:#?}", err);
            }
        };
        if self.iter_len != lst.len() {
            warn!(
                "ObjectArchive changed from {} to {}.",
                self.iter_len,
                lst.len()
            );
            self.iter_len = lst.len();
        }
        if self.iter_pos < lst.len() {
            let obj = unsafe { Address::from_usize(lst[self.iter_pos]) };
            self.iter_pos += 1;
            obj
        } else {
            unsafe { Address::zero() }
        }
    }
}
