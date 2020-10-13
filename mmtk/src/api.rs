use libc::c_void;
use libc::c_char;
use std::ffi::CStr;
use std::ptr::null_mut;
use mmtk::memory_manager;
use mmtk::Allocator;
use mmtk::util::{ObjectReference, OpaquePointer, Address};
use mmtk::Plan;
use mmtk::util::constants::LOG_BYTES_IN_PAGE;
use mmtk::{SelectedMutator, SelectedTraceLocal, SelectedCollector};
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
    memory_manager::gc_init(&*mmtk, heap_size);
    
    Box::into_raw(mmtk) as *mut c_void
}

#[no_mangle]
pub extern "C" fn start_control_collector(mmtk: &mut MMTK<V8>, tls: OpaquePointer) {
    memory_manager::start_control_collector(&*mmtk, tls);
}

#[no_mangle]
pub extern "C" fn bind_mutator(mmtk: &'static mut MMTK<V8>, tls: OpaquePointer) -> *mut SelectedMutator<V8> {
    Box::into_raw(memory_manager::bind_mutator(mmtk, tls))
}

#[no_mangle]
pub extern "C" fn destroy_mutator(mutator: *mut SelectedMutator<V8>) {
    memory_manager::destroy_mutator(unsafe { Box::from_raw(mutator) })
}

#[no_mangle]
pub extern "C" fn alloc(mutator: &mut SelectedMutator<V8>, size: usize,
                    align: usize, offset: isize, allocator: Allocator) -> Address {
    memory_manager::alloc::<V8>(mutator, size, align, offset, allocator)
}

#[no_mangle]
pub extern "C" fn post_alloc(mutator: &mut SelectedMutator<V8>, refer: ObjectReference, type_refer: ObjectReference,
                                        bytes: usize, allocator: Allocator) {
    memory_manager::post_alloc::<V8>(mutator, refer, type_refer, bytes, allocator)
}

#[no_mangle]
pub extern "C" fn will_never_move(object: ObjectReference) -> bool {
    !object.is_movable()
}

#[no_mangle]
pub extern "C" fn report_delayed_root_edge(mmtk: &mut MMTK<V8>, trace_local: &mut SelectedTraceLocal<V8>, addr: Address) {
    memory_manager::report_delayed_root_edge(mmtk, trace_local, addr);
}

#[no_mangle]
pub extern "C" fn will_not_move_in_current_collection(mmtk: &mut MMTK<V8>, trace_local: &mut SelectedTraceLocal<V8>, obj: ObjectReference) -> bool {
    memory_manager::will_not_move_in_current_collection(
        mmtk, trace_local, obj)
}

#[no_mangle]
pub extern "C" fn process_interior_edge(mmtk: &mut MMTK<V8>, trace_local: &mut SelectedTraceLocal<V8>, target: ObjectReference, slot: Address, root: bool) {
    memory_manager::process_interior_edge(mmtk, trace_local, target, slot, root);
}

#[no_mangle]
pub extern "C" fn start_worker(tls: OpaquePointer, worker: &mut SelectedCollector<V8>) {
    memory_manager::start_worker::<V8>(tls, worker);
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
pub extern "C" fn trace_get_forwarded_referent(trace_local: &mut SelectedTraceLocal<V8>, object: ObjectReference) -> ObjectReference{
    memory_manager::trace_get_forwarded_referent::<V8>(trace_local, object)
}

#[no_mangle]
pub extern "C" fn trace_get_forwarded_reference(trace_local: &mut SelectedTraceLocal<V8>, object: ObjectReference) -> ObjectReference{
    memory_manager::trace_get_forwarded_reference::<V8>(trace_local, object)
}

#[no_mangle]
pub extern "C" fn trace_retain_referent(trace_local: &mut SelectedTraceLocal<V8>, object: ObjectReference) -> ObjectReference{
    memory_manager::trace_retain_referent::<V8>(trace_local, object)
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
pub extern "C" fn harness_end(mmtk: &mut MMTK<V8>, _tls: OpaquePointer) {
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
