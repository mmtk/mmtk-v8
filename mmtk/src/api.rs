use libc::c_void;
use libc::c_char;
use std::ffi::CStr;
use mmtk::memory_manager;
use mmtk::AllocationSemantics;
use mmtk::util::{ObjectReference, OpaquePointer, Address};
use mmtk::{Mutator, SelectedPlan};
use mmtk::scheduler::GCWorker;
use mmtk::MMTK;

use V8;
use UPCALLS;
use V8_Upcalls;

#[no_mangle]
pub extern "C" fn v8_new_heap(calls: *const V8_Upcalls, heap_size: usize) 
    -> *mut c_void {
    unsafe { 
        UPCALLS = calls;
    };
    let mmtk: Box<MMTK<V8>> = Box::new(MMTK::new());
    let mmtk: *mut MMTK<V8> = Box::into_raw(mmtk);
    memory_manager::gc_init(unsafe { &mut *mmtk }, heap_size);
    
   mmtk as *mut c_void
}

#[no_mangle]
pub extern "C" fn start_control_collector(mmtk: &mut MMTK<V8>, tls: OpaquePointer) {
    memory_manager::start_control_collector(&*mmtk, tls);
}

#[no_mangle]
pub extern "C" fn bind_mutator(mmtk: &'static mut MMTK<V8>, tls: OpaquePointer) -> *mut Mutator<SelectedPlan<V8>> {
    Box::into_raw(memory_manager::bind_mutator(mmtk, tls))
}

#[no_mangle]
pub extern "C" fn destroy_mutator(mutator: *mut Mutator<SelectedPlan<V8>>) {
    memory_manager::destroy_mutator(unsafe { Box::from_raw(mutator) })
}

#[no_mangle]
pub extern "C" fn alloc(mutator: &mut Mutator<SelectedPlan<V8>>, size: usize,
                    align: usize, offset: isize, semantics: AllocationSemantics) -> Address {
    memory_manager::alloc::<V8>(mutator, size, align, offset, semantics)
}

#[no_mangle]
pub extern "C" fn post_alloc(mutator: &mut Mutator<SelectedPlan<V8>>, refer: ObjectReference,
                                        bytes: usize, semantics: AllocationSemantics) {
    memory_manager::post_alloc::<V8>(mutator, refer, bytes, semantics)
}

#[no_mangle]
pub extern "C" fn will_never_move(object: ObjectReference) -> bool {
    !object.is_movable()
}

#[no_mangle]
pub extern "C" fn start_worker(mmtk: &'static mut MMTK<V8>, tls: OpaquePointer, worker: &'static mut GCWorker<V8>) {
    memory_manager::start_worker::<V8>(tls, worker, mmtk);
}

#[no_mangle]
pub extern "C" fn enable_collection(mmtk: &'static mut MMTK<V8>, tls: OpaquePointer) {
    memory_manager::enable_collection(mmtk, tls);
}

#[no_mangle]
pub extern "C" fn used_bytes(mmtk: &mut MMTK<V8>) -> usize {
    memory_manager::used_bytes(mmtk)
}

#[no_mangle]
pub extern "C" fn free_bytes(mmtk: &mut MMTK<V8>) -> usize {
    memory_manager::free_bytes(mmtk)
}

#[no_mangle]
pub extern "C" fn total_bytes(mmtk: &mut MMTK<V8>) -> usize {
    memory_manager::total_bytes(&*mmtk)
}

#[no_mangle]
#[cfg(feature = "sanity")]
pub extern "C" fn scan_region(mmtk: &mut MMTK<V8>) {
    memory_manager::scan_region(mmtk);
}

#[no_mangle]
pub extern "C" fn is_live_object(object: ObjectReference) -> bool{
    object.is_live()
}

#[no_mangle]
pub extern "C" fn is_mapped_object(object: ObjectReference) -> bool {
    object.is_mapped()
}

#[no_mangle]
pub extern "C" fn is_mapped_address(address: Address) -> bool {
    address.is_mapped()
}

#[no_mangle]
pub extern "C" fn modify_check(mmtk: &mut MMTK<V8>, object: ObjectReference) {
    memory_manager::modify_check(mmtk, object);
}

#[no_mangle]
pub extern "C" fn handle_user_collection_request(mmtk: &mut MMTK<V8>, tls: OpaquePointer) {
    memory_manager::handle_user_collection_request::<V8>(mmtk, tls);
}

#[no_mangle]
pub extern "C" fn add_weak_candidate(mmtk: &mut MMTK<V8>, reff: ObjectReference, referent: ObjectReference) {
    memory_manager::add_weak_candidate(mmtk, reff, referent);
}

#[no_mangle]
pub extern "C" fn add_soft_candidate(mmtk: &mut MMTK<V8>, reff: ObjectReference, referent: ObjectReference) {
    memory_manager::add_soft_candidate(mmtk, reff, referent);
}

#[no_mangle]
pub extern "C" fn add_phantom_candidate(mmtk: &mut MMTK<V8>, reff: ObjectReference, referent: ObjectReference) {
    memory_manager::add_phantom_candidate(mmtk, reff, referent);
}

#[no_mangle]
pub extern "C" fn harness_begin(mmtk: &mut MMTK<V8>, tls: OpaquePointer) {
    memory_manager::harness_begin(mmtk, tls);
}

#[no_mangle]
pub extern "C" fn harness_end(mmtk: &'static mut MMTK<V8>, _tls: OpaquePointer) {
    memory_manager::harness_end(mmtk);
}

#[no_mangle]
pub extern "C" fn process(mmtk: &'static mut MMTK<V8>, name: *const c_char, value: *const c_char) -> bool {
    let name_str: &CStr = unsafe { CStr::from_ptr(name) };
    let value_str: &CStr = unsafe { CStr::from_ptr(value) };
    let res = memory_manager::process(
        mmtk, 
        name_str.to_str().unwrap(), 
        value_str.to_str().unwrap());
    
    res
}

#[no_mangle]
pub extern "C" fn starting_heap_address() -> Address {
    memory_manager::starting_heap_address()
}

#[no_mangle]
pub extern "C" fn last_heap_address() -> Address {
    memory_manager::last_heap_address()
}
