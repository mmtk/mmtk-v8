use libc::c_void;
use mmtk::vm::Scanning;
use mmtk::{Mutator, SelectedPlan, TransitiveClosure, TraceLocal};
use mmtk::util::{ObjectReference, SynchronizedCounter};
use mmtk::util::OpaquePointer;
use mmtk::scheduler::gc_works::ProcessEdgesWork;
use V8;
use super::UPCALLS;

static COUNTER: SynchronizedCounter = SynchronizedCounter::new(0);

pub struct VMScanning {}

impl Scanning<V8> for VMScanning {
    const SCAN_MUTATORS_IN_SAFEPOINT: bool = false;
    const SINGLE_THREAD_MUTATOR_SCANNING: bool = false;

    fn scan_object<T: TransitiveClosure>(trace: &mut T, object: ObjectReference, tls: OpaquePointer) {
        unimplemented!()
    }

    fn reset_thread_counter() {
        unimplemented!()
    }

    fn notify_initial_thread_scan_complete(_partial_scan: bool, _tls: OpaquePointer) {
        unimplemented!()
    }

    fn scan_objects<W: ProcessEdgesWork<VM=V8>>(objects: &[ObjectReference]) {
        unimplemented!()
    }

    fn scan_thread_roots<W: ProcessEdgesWork<VM=V8>>() {
        unimplemented!()
    }

    fn scan_thread_root<W: ProcessEdgesWork<VM=V8>>(mutator: &'static mut Mutator<SelectedPlan<V8>>, _tls: OpaquePointer) {
        unimplemented!()
    }

    fn scan_vm_specific_roots<W: ProcessEdgesWork<VM=V8>>() {
        unimplemented!()
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }
}