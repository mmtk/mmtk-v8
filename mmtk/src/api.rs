use libc::c_char;
use libc::c_void;
use mmtk::memory_manager;
use mmtk::scheduler::{GCController, GCWorker};
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::AllocationSemantics;
use mmtk::MMTKBuilder;
use mmtk::Mutator;
use mmtk::MMTK;
use std::ffi::CStr;

use V8_Upcalls;
use BUILDER;
use SINGLETON;
use UPCALLS;
use V8;

#[no_mangle]
pub extern "C" fn v8_new_heap(calls: *const V8_Upcalls, heap_size: usize) -> *mut c_void {
    unsafe {
        UPCALLS = calls;
    };

    {
        use mmtk::util::options::PlanSelector;
        let mut builder = BUILDER.lock().unwrap();
        // set heap size
        let success =
            builder
                .options
                .gc_trigger
                .set(mmtk::util::options::GCTriggerSelector::FixedHeapSize(
                    heap_size,
                ));
        assert!(success, "Failed to set heap size to {}", heap_size);

        // set plan based on features
        let plan = if cfg!(feature = "nogc") {
            PlanSelector::NoGC
        } else {
            panic!("No plan feature is enabled for V8. V8 requiers one plan feature to build.")
        };
        let success = builder.options.plan.set(plan);
        assert!(success, "Failed to set plan to {:?}", plan);
    }

    // Make sure that we haven't initialized MMTk (by accident) yet
    assert!(!crate::MMTK_INITIALIZED.load(std::sync::atomic::Ordering::Relaxed));
    // Make sure we initialize MMTk here
    lazy_static::initialize(&SINGLETON);

    let mmtk: &MMTK<V8> = &SINGLETON;
    mmtk as *const MMTK<V8> as *mut c_void
}

#[no_mangle]
pub extern "C" fn bind_mutator(
    mmtk: &'static mut MMTK<V8>,
    tls: VMMutatorThread,
) -> *mut Mutator<V8> {
    Box::into_raw(memory_manager::bind_mutator(mmtk, tls))
}

#[no_mangle]
// It is fine we turn the pointer back to box, as we turned a boxed value to the raw pointer in bind_mutator()
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn destroy_mutator(mutator: *mut Mutator<V8>) {
    // Turn this back to boxed mutator, and let Rust reclaim it when it goes out of scope
    let mut boxed_mutator = unsafe { Box::from_raw(mutator) };
    memory_manager::destroy_mutator(&mut boxed_mutator);
}

#[no_mangle]
pub extern "C" fn alloc(
    mutator: &mut Mutator<V8>,
    size: usize,
    align: usize,
    offset: usize,
    semantics: AllocationSemantics,
) -> Address {
    memory_manager::alloc::<V8>(mutator, size, align, offset, semantics)
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
// We trust the worker pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn start_control_collector(
    mmtk: &'static mut MMTK<V8>,
    tls: VMWorkerThread,
    gc_controller: *mut GCController<V8>,
) {
    let mut gc_controller = unsafe { Box::from_raw(gc_controller) };
    memory_manager::start_control_collector(&*mmtk, tls, &mut gc_controller);
}

#[no_mangle]
// We trust the worker pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn start_worker(
    mmtk: &'static mut MMTK<V8>,
    tls: VMWorkerThread,
    worker: *mut GCWorker<V8>,
) {
    let mut worker = unsafe { Box::from_raw(worker) };
    memory_manager::start_worker::<V8>(mmtk, tls, &mut worker);
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
pub extern "C" fn is_live_object(object: ObjectReference) -> bool {
    memory_manager::is_live_object(object)
}

#[no_mangle]
pub extern "C" fn is_in_mmtk_spaces(object: ObjectReference) -> bool {
    memory_manager::is_in_mmtk_spaces::<V8>(object)
}

#[no_mangle]
pub extern "C" fn is_mapped_address(address: Address) -> bool {
    memory_manager::is_mapped_address(address)
}

#[no_mangle]
pub extern "C" fn handle_user_collection_request(mmtk: &mut MMTK<V8>, tls: VMMutatorThread) {
    memory_manager::handle_user_collection_request::<V8>(mmtk, tls);
}

#[no_mangle]
pub extern "C" fn add_weak_candidate(mmtk: &mut MMTK<V8>, reff: ObjectReference) {
    memory_manager::add_weak_candidate(mmtk, reff);
}

#[no_mangle]
pub extern "C" fn add_soft_candidate(mmtk: &mut MMTK<V8>, reff: ObjectReference) {
    memory_manager::add_soft_candidate(mmtk, reff);
}

#[no_mangle]
pub extern "C" fn add_phantom_candidate(mmtk: &mut MMTK<V8>, reff: ObjectReference) {
    memory_manager::add_phantom_candidate(mmtk, reff);
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
    builder: &mut MMTKBuilder,
    name: *const c_char,
    value: *const c_char,
) -> bool {
    let name_str: &CStr = unsafe { CStr::from_ptr(name) };
    let value_str: &CStr = unsafe { CStr::from_ptr(value) };
    let res = memory_manager::process(
        builder,
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
