use libc::c_char;
use libc::c_void;
use mmtk::memory_manager;
use mmtk::scheduler::GCWorker;
use mmtk::util::opaque_pointer::*;
use mmtk::util::options::PlanSelector;
use mmtk::util::{Address, ObjectReference};
use mmtk::AllocationSemantics;
use mmtk::Mutator;
use mmtk::MMTK;
use mmtk::policy::space::Space;
use std::ffi::CStr;

use V8_Upcalls;
use UPCALLS;
use V8;

/// Release an address buffer
#[no_mangle]
pub unsafe extern "C" fn mmtk_release_buffer(ptr: *mut Address, length: usize, capacity: usize) {
    let _vec = Vec::<Address>::from_raw_parts(ptr, length, capacity);
}

/// Check whether an object is movable.
#[no_mangle]
pub unsafe extern "C" fn mmtk_is_movable(object: ObjectReference) -> i32 {
    let object = {
        let untagged_word = object.to_address().as_usize() & !0b11usize;
        Address::from_usize(untagged_word).to_object_reference()
    };
    if object.is_movable() { 1 } else { 0 }
}

/// Get the forwarding pointer, or NULL if the object is not forwarded
#[no_mangle]
pub unsafe extern "C" fn mmtk_get_forwarded_object(object: ObjectReference) -> *mut c_void {
    let tag = object.to_address().as_usize() & 0b11usize;
    let object = {
        let untagged_word = object.to_address().as_usize() & !0b11usize;
        Address::from_usize(untagged_word).to_object_reference()
    };
    object.get_forwarded_object().map(|x| (x.to_address().as_usize() | tag) as *mut c_void).unwrap_or(0 as _)
}

#[no_mangle]
pub extern "C" fn v8_new_heap(calls: *const V8_Upcalls, heap_size: usize) -> *mut c_void {
    unsafe {
        UPCALLS = calls;
    };
    let mmtk: *const MMTK<V8> = &*crate::SINGLETON;
    memory_manager::gc_init(unsafe { &mut *(mmtk as *mut MMTK<V8>) }, heap_size);
    enable_collection(unsafe { &mut *(mmtk as *mut MMTK<V8>) }, VMThread::UNINITIALIZED);

    mmtk as *mut c_void
}

#[no_mangle]
pub extern "C" fn start_control_collector(mmtk: &mut MMTK<V8>, tls: VMWorkerThread) {
    memory_manager::start_control_collector(&*mmtk, tls);
}

#[no_mangle]
pub extern "C" fn bind_mutator(
    mmtk: &'static mut MMTK<V8>,
    tls: VMMutatorThread,
) -> *mut Mutator<V8> {
    Box::into_raw(memory_manager::bind_mutator(mmtk, tls))
}

#[no_mangle]
pub unsafe extern "C" fn mmtk_in_space(mmtk: &'static MMTK<V8>, object: ObjectReference, space: AllocationSemantics) -> i32 {
    match space {
        AllocationSemantics::Default => {
            (object.is_mapped()
                && mmtk_in_space(mmtk, object, AllocationSemantics::ReadOnly) == 0
                && mmtk_in_space(mmtk, object, AllocationSemantics::Immortal) == 0
                && mmtk_in_space(mmtk, object, AllocationSemantics::Los) == 0
                && mmtk_in_space(mmtk, object, AllocationSemantics::Code) == 0
                && mmtk_in_space(mmtk, object, AllocationSemantics::LargeCode) == 0) as _
        },
        AllocationSemantics::ReadOnly => mmtk.plan.base().ro_space.in_space(object) as _,
        AllocationSemantics::Immortal => mmtk.plan.common().immortal.in_space(object) as _,
        AllocationSemantics::Los => mmtk.plan.common().los.in_space(object) as _,
        AllocationSemantics::Code => mmtk.plan.base().code_space.in_space(object) as _,
        AllocationSemantics::LargeCode => mmtk.plan.base().code_lo_space.in_space(object) as _,
    }
}

#[no_mangle]
// It is fine we turn the pointer back to box, as we turned a boxed value to the raw pointer in bind_mutator()
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn destroy_mutator(mutator: *mut Mutator<V8>) {
    memory_manager::destroy_mutator(unsafe { Box::from_raw(mutator) })
}

#[no_mangle]
pub extern "C" fn alloc(
    mutator: &mut Mutator<V8>,
    size: usize,
    align: usize,
    offset: isize,
    semantics: AllocationSemantics,
) -> Address {
    let a = memory_manager::alloc::<V8>(mutator, size, align, offset, semantics);
    unsafe { memory_manager::post_alloc::<V8>(mutator, a.to_object_reference(), size, semantics); }
    if PlanSelector::PageProtect == mutator.plan.options().plan && AllocationSemantics::Default == semantics {
        // Possible `array_header_size` values that can be passed to [AllocateUninitializedJSArrayWithElements](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/codegen/code-stub-assembler.h;l=1884).
        let array_header_sizes = [0x20, 0x50, 0x58];
        for array_header_size in array_header_sizes {
            unsafe {
                memory_manager::post_alloc::<V8>(mutator, a.add(array_header_size).to_object_reference(), 0, semantics);
            }
        }
    }
    a
}

#[no_mangle]
pub extern "C" fn post_alloc(
    mutator: &mut Mutator<V8>,
    refer: ObjectReference,
    bytes: usize,
    semantics: AllocationSemantics,
) {
    memory_manager::post_alloc::<V8>(mutator, refer, bytes, semantics)
}

#[no_mangle]
pub extern "C" fn will_never_move(object: ObjectReference) -> bool {
    !object.is_movable()
}

#[no_mangle]
pub extern "C" fn start_worker(
    mmtk: &'static mut MMTK<V8>,
    tls: VMWorkerThread,
    worker: &'static mut GCWorker<V8>,
) {
    memory_manager::start_worker::<V8>(tls, worker, mmtk);
}

#[no_mangle]
pub extern "C" fn initialize_collection(mmtk: &'static mut MMTK<V8>, tls: VMThread) {
    memory_manager::initialize_collection(mmtk, tls);
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
pub extern "C" fn mmtk_object_is_live(object: ObjectReference) -> usize {
    debug_assert_eq!(object.to_address().as_usize() & 0b11, 0);
    if crate::SINGLETON.plan.base().ro_space.in_space(object) {
        return 1;
    }
    if object.is_reachable() { 1 } else { 0 }
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
pub extern "C" fn handle_user_collection_request(mmtk: &mut MMTK<V8>, tls: VMMutatorThread) {
    memory_manager::handle_user_collection_request::<V8>(mmtk, tls);
}

#[no_mangle]
pub extern "C" fn add_weak_candidate(
    mmtk: &mut MMTK<V8>,
    reff: ObjectReference,
    referent: ObjectReference,
) {
    memory_manager::add_weak_candidate(mmtk, reff, referent);
}

#[no_mangle]
pub extern "C" fn add_soft_candidate(
    mmtk: &mut MMTK<V8>,
    reff: ObjectReference,
    referent: ObjectReference,
) {
    memory_manager::add_soft_candidate(mmtk, reff, referent);
}

#[no_mangle]
pub extern "C" fn add_phantom_candidate(
    mmtk: &mut MMTK<V8>,
    reff: ObjectReference,
    referent: ObjectReference,
) {
    memory_manager::add_phantom_candidate(mmtk, reff, referent);
}

#[no_mangle]
pub extern "C" fn harness_begin(mmtk: &mut MMTK<V8>, tls: VMMutatorThread) {
    memory_manager::harness_begin(mmtk, tls);
}

#[no_mangle]
pub extern "C" fn harness_end(mmtk: &'static mut MMTK<V8>, _tls: OpaquePointer) {
    memory_manager::harness_end(mmtk);
}

#[no_mangle]
// We trust the name/value pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn process(
    mmtk: &'static mut MMTK<V8>,
    name: *const c_char,
    value: *const c_char,
) -> bool {
    let name_str: &CStr = unsafe { CStr::from_ptr(name) };
    let value_str: &CStr = unsafe { CStr::from_ptr(value) };
    let res = memory_manager::process(
        mmtk,
        name_str.to_str().unwrap(),
        value_str.to_str().unwrap(),
    );

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
