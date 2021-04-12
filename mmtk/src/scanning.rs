use mmtk::scheduler::gc_work::ProcessEdgesWork;
use mmtk::scheduler::GCWorker;
use mmtk::util::ObjectReference;
use mmtk::util::OpaquePointer;
use mmtk::vm::Scanning;
use mmtk::{Mutator, TransitiveClosure};
use V8;

pub struct VMScanning {}

impl Scanning<V8> for VMScanning {
    const SCAN_MUTATORS_IN_SAFEPOINT: bool = false;
    const SINGLE_THREAD_MUTATOR_SCANNING: bool = false;

    fn scan_object<T: TransitiveClosure>(
        _trace: &mut T,
        _object: ObjectReference,
        _tls: OpaquePointer,
    ) {
        unimplemented!()
    }

    fn notify_initial_thread_scan_complete(_partial_scan: bool, _tls: OpaquePointer) {
        unimplemented!()
    }

    fn scan_objects<W: ProcessEdgesWork<VM = V8>>(
        _objects: &[ObjectReference],
        _worker: &mut GCWorker<V8>,
    ) {
        unimplemented!()
    }

    fn scan_thread_roots<W: ProcessEdgesWork<VM = V8>>() {
        unimplemented!()
    }

    fn scan_thread_root<W: ProcessEdgesWork<VM = V8>>(
        _mutator: &'static mut Mutator<V8>,
        _tls: OpaquePointer,
    ) {
        unimplemented!()
    }

    fn scan_vm_specific_roots<W: ProcessEdgesWork<VM = V8>>() {
        unimplemented!()
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }
}
