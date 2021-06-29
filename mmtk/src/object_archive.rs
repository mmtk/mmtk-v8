use libc::c_void;
use mmtk::util::ObjectReference;
use mmtk::util::address::Address;
use std::collections::HashMap;
use std::sync::Mutex;


lazy_static! {
    pub static ref OBJECT_ARCHIVE: ObjectArchive = ObjectArchive::new();
}

pub fn global_object_archive() -> &'static mut ObjectArchive {
    unsafe { &mut *(&OBJECT_ARCHIVE as &ObjectArchive as *const ObjectArchive as *mut ObjectArchive) }
}

#[no_mangle]
pub extern "C" fn tph_archive_new() -> *const c_void {
    &OBJECT_ARCHIVE as &ObjectArchive as *const ObjectArchive as *const c_void
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
pub extern "C" fn tph_archive_obj_to_space(arch: *mut c_void, obj_ptr: *mut c_void) -> u8 {
    let arch = unsafe { Box::from_raw(arch as *mut ObjectArchive) };
    let res = arch.object_to_space(Address::from_mut_ptr(obj_ptr)).unwrap();
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

#[derive(Default)]
pub struct ObjectArchive {
    untagged_objects: Vec<ObjectReference>,
    space_map: HashMap<ObjectReference, u8>,
    iter_pos: usize,
    iter_len: usize,
}

impl ObjectArchive {
    pub fn new() -> ObjectArchive {
        Default::default()
    }

    pub fn insert_object(&mut self, addr: Address, _isolate: Address, space: u8) {
        let untagged_object = unsafe {
            Address::from_usize(addr.as_usize() & !0b11).to_object_reference()
        };
        self.space_map.insert(untagged_object, space);
        match self.untagged_objects.binary_search_by(|o| o.to_address().cmp(&untagged_object.to_address())) {
            Ok(_) => unreachable!(),
            Err(idx) => {
                self.untagged_objects.insert(idx, untagged_object);
            }
        }
    }

    pub fn remove_object(&mut self, addr: Address) {
        let untagged_object = unsafe { Address::from_usize(addr.as_usize() & !0b11).to_object_reference() };
        self.space_map.remove(&untagged_object);
        let index = self.untagged_objects.iter().position(|x| *x == untagged_object).unwrap();
        self.untagged_objects.remove(index);
    }

    pub fn inner_addr_to_object(&self, inner_addr: Address) -> Address {
        let idx = match self.untagged_objects.binary_search_by(|o| o.to_address().cmp(&inner_addr)) {
            Ok(idx) => idx,
            Err(idx) => idx - 1,
        };
        self.untagged_objects[idx].to_address()
    }

    pub fn object_to_space(&self, mut obj_addr: Address) -> Option<u8> {
        obj_addr = unsafe { Address::from_usize(obj_addr.as_usize() & !0b11) };
        debug_assert_eq!(obj_addr.as_usize() & 0b11, 0);
        unsafe { self.space_map.get(&obj_addr.to_object_reference()).cloned() }
    }

    pub fn update(&mut self) {
        let mut new_objects = vec![];
        let mut new_space_map = HashMap::new();
        for object in &self.untagged_objects {
            debug_assert_eq!(object.to_address().as_usize() & 0b11, 0);
            if object.is_reachable() || self.space_map[&object] == 0 {
                let new_object = object.get_forwarded_object().unwrap_or(*object);
                debug_assert_eq!(new_object.to_address().as_usize() & 0b11, 0);
                new_objects.push(new_object);
                new_space_map.insert(new_object, self.space_map[&object]);
            }
        }
        new_objects.dedup();
        new_objects.sort_by(|a, b| a.to_address().cmp(&b.to_address()));
        self.untagged_objects = new_objects;
        self.space_map = new_space_map;
    }

    pub fn copy(&mut self, from: Address, to: Address) {
        lazy_static! {
            static ref LOCK: Mutex<()> = Mutex::default();
        }
        let _lock = LOCK.lock().unwrap();
        unsafe {
            let space = self.space_map[&from.to_object_reference()];
            self.space_map.insert(to.to_object_reference(), space);
            // self.untagged_objects.push(to.to_object_reference());
            // self.untagged_objects.sort_by(|a, b| a.to_address().cmp(&b.to_address()));
        }
    }

    pub fn reset_iterator(&mut self) {
        self.iter_pos = 0;
        self.iter_len = self.untagged_objects.len();
    }

    pub fn next_object(&mut self) -> Address {
        if self.iter_len != self.untagged_objects.len() {
            warn!(
                "ObjectArchive changed from {} to {}.",
                self.iter_len,
                self.untagged_objects.len()
            );
            self.iter_len = self.untagged_objects.len();
        }
        if self.iter_pos < self.untagged_objects.len() {
            let o =    self.untagged_objects[self.iter_pos].to_address();
            self.iter_pos += 1;
            o
        } else {
            unsafe { Address::zero() }
        }
    }
}
