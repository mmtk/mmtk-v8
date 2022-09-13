extern crate libc;
extern crate mmtk;
#[macro_use]
extern crate lazy_static;

#[macro_use]
extern crate log;

use std::ptr::null_mut;

use libc::c_void;
use mmtk::util::opaque_pointer::*;
use mmtk::util::Address;
use mmtk::util::ObjectReference;
use mmtk::vm::VMBinding;
use mmtk::MMTKBuilder;
use mmtk::Mutator;
use mmtk::MMTK;
pub mod active_plan;
pub mod api;
pub mod collection;
mod object_archive;
pub mod object_model;
pub mod reference_glue;
pub mod scanning;

#[repr(C)]
pub struct V8_Upcalls {
    pub stop_all_mutators: extern "C" fn(tls: VMWorkerThread),
    pub resume_mutators: extern "C" fn(tls: VMWorkerThread),
    pub spawn_gc_thread: extern "C" fn(tls: VMThread, kind: libc::c_int, ctx: *mut libc::c_void),
    pub block_for_gc: extern "C" fn(),
    pub get_next_mutator: extern "C" fn() -> *mut Mutator<V8>,
    pub reset_mutator_iterator: extern "C" fn(),
    pub compute_static_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub compute_global_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub compute_thread_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub scan_object: extern "C" fn(trace: *mut c_void, object: ObjectReference, tls: OpaquePointer),
    pub dump_object: extern "C" fn(object: ObjectReference),
    pub get_object_size: extern "C" fn(object: ObjectReference) -> usize,
    pub get_mmtk_mutator: extern "C" fn(tls: VMMutatorThread) -> *mut Mutator<V8>,
    pub is_mutator: extern "C" fn(tls: VMThread) -> bool,
}

pub static mut UPCALLS: *const V8_Upcalls = null_mut();

#[derive(Default)]
pub struct V8;

/// The edge type of V8.
///
/// TODO: We start with Address to transition from the old API.
/// We should define an edge type suitable for V8.
pub type V8Edge = Address;

impl VMBinding for V8 {
    type VMObjectModel = object_model::VMObjectModel;
    type VMScanning = scanning::VMScanning;
    type VMCollection = collection::VMCollection;
    type VMActivePlan = active_plan::VMActivePlan;
    type VMReferenceGlue = reference_glue::VMReferenceGlue;

    type VMEdge = V8Edge;
    type VMMemorySlice = mmtk::vm::edge_shape::UnimplementedMemorySlice<V8Edge>;

    const MAX_ALIGNMENT: usize = 32;
}

use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;
use std::sync::Mutex;

pub static MMTK_INITIALIZED: AtomicBool = AtomicBool::new(false);

lazy_static! {
    pub static ref BUILDER: Mutex<MMTKBuilder> = Mutex::new(MMTKBuilder::new());
    pub static ref SINGLETON: MMTK<V8> = {
        let builder = BUILDER.lock().unwrap();
        assert!(!MMTK_INITIALIZED.load(Ordering::SeqCst));
        let ret = mmtk::memory_manager::mmtk_init(&builder);
        MMTK_INITIALIZED.store(true, std::sync::atomic::Ordering::SeqCst);
        *ret
    };
}
