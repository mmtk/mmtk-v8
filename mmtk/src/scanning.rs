use mmtk::scheduler::gc_work::ProcessEdgesWork;
use mmtk::scheduler::GCWorker;
use mmtk::util::ObjectReference;
use mmtk::util::OpaquePointer;
use mmtk::vm::Scanning;
use mmtk::{Mutator, TransitiveClosure};
use V8;
use mmtk::scheduler::gc_work::*;
use mmtk::scheduler::*;
use crate::*;

use std::marker::PhantomData;

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
        objects: &[ObjectReference],
        worker: &mut GCWorker<V8>,
    ) {
        unsafe {
            let buf = objects.as_ptr();
            let len = objects.len();
            ((*UPCALLS).scan_objects)(buf, len, create_process_edges_work::<W> as _);
        }
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
        SINGLETON.scheduler.work_buckets[WorkBucketStage::Prepare]
                .add(ScanRoots::<W>::new());
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }
}

pub struct ScanRoots<E: ProcessEdgesWork<VM = V8>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = V8>> ScanRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = V8>> GCWork<V8> for ScanRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<V8>, _mmtk: &'static MMTK<V8>) {
        unsafe {
            ((*UPCALLS).scan_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub(crate) extern "C" fn create_process_edges_work<W: ProcessEdgesWork<VM = V8>>(
    ptr: *mut Address,
    length: usize,
    capacity: usize,
) -> NewBuffer {
    if !ptr.is_null() {
        let buf = unsafe { Vec::<Address>::from_raw_parts(ptr, length, capacity) };
        SINGLETON.scheduler.work_buckets[WorkBucketStage::Closure]
            .add(W::new(buf, false, &SINGLETON));
    }
    let (ptr, _, capacity) = Vec::with_capacity(W::CAPACITY).into_raw_parts();
    NewBuffer { ptr, capacity }
}