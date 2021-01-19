extern crate mmtk;
extern crate libc;
#[macro_use]
extern crate lazy_static;

#[macro_use]
extern crate log;

use std::ptr::null_mut;

use mmtk::vm::VMBinding;
use mmtk::util::OpaquePointer;
use mmtk::MMTK;
use mmtk::util::ObjectReference;
use mmtk::Plan;
use mmtk::Mutator;
use mmtk::scheduler::GCWorker;
use libc::c_void;
mod object_archive;
pub mod scanning;
pub mod collection;
pub mod object_model;
pub mod active_plan;
pub mod reference_glue;
pub mod api;

#[repr(C)]
pub struct V8_Upcalls {
    pub stop_all_mutators: extern "C" fn(tls: OpaquePointer),
    pub resume_mutators: extern "C" fn(tls: OpaquePointer),
    pub spawn_worker_thread: extern "C" fn(tls: OpaquePointer, ctx: *mut GCWorker<V8>),
    pub block_for_gc: extern "C" fn(),
    pub active_collector: extern "C" fn(tls: OpaquePointer) -> *mut GCWorker<V8>,
    pub get_next_mutator: extern "C" fn() -> *mut Mutator<V8>,
    pub reset_mutator_iterator: extern "C" fn(),
    pub compute_static_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub compute_global_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub compute_thread_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub scan_object: extern "C" fn(trace: *mut c_void, object: ObjectReference, tls: OpaquePointer),
    pub dump_object: extern "C" fn(object: ObjectReference),
    pub get_object_size: extern "C" fn(object: ObjectReference) -> usize,
    pub get_mmtk_mutator: extern "C" fn(tls: OpaquePointer) -> *mut Mutator<V8>,
    pub is_mutator: extern "C" fn(tls: OpaquePointer) -> bool,
}

pub static mut UPCALLS: *const V8_Upcalls = null_mut();

#[derive(Default)]
pub struct V8;

impl VMBinding for V8 {
    type VMObjectModel = object_model::VMObjectModel;
    type VMScanning = scanning::VMScanning;
    type VMCollection = collection::VMCollection;
    type VMActivePlan = active_plan::VMActivePlan;
    type VMReferenceGlue = reference_glue::VMReferenceGlue;

    const MAX_ALIGNMENT: usize = 32;
}

lazy_static! {
    pub static ref SINGLETON: MMTK<V8> = {
        #[cfg(feature = "nogc")]
        std::env::set_var("MMTK_PLAN", "NoGC");

        MMTK::new()
    };
}
