use mmtk::vm::Scanning;
use mmtk::{Mutator, TransitiveClosure};
use mmtk::util::{ObjectReference};
use mmtk::util::OpaquePointer;
use mmtk::scheduler::gc_work::ProcessEdgesWork;
use mmtk::scheduler::GCWorker;
use V8;

pub struct VMScanning {}

impl Scanning<V8> for VMScanning {
    const SCAN_MUTATORS_IN_SAFEPOINT: bool = false;
    const SINGLE_THREAD_MUTATOR_SCANNING: bool = false;

    fn scan_object<T: TransitiveClosure>(trace: &mut T, object: ObjectReference, tls: OpaquePointer) {
        unimplemented!()
    }

    fn notify_initial_thread_scan_complete(_partial_scan: bool, _tls: OpaquePointer) {
        unimplemented!()
    }

    fn scan_objects<W: ProcessEdgesWork<VM=V8>>(objects: &[ObjectReference], worker: &mut GCWorker<V8>) {
        todo!()
    }

    fn scan_thread_roots<W: ProcessEdgesWork<VM=V8>>() {
        unimplemented!()
    }

    fn scan_thread_root<W: ProcessEdgesWork<VM=V8>>(mutator: &'static mut Mutator<V8>, _tls: OpaquePointer) {
        todo!()
    }

    fn scan_vm_specific_roots<W: ProcessEdgesWork<VM=V8>>() {
        todo!()
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }
}