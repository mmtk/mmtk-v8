use mmtk::scheduler::GCWorker;
use mmtk::scheduler::ProcessEdgesWork;
use mmtk::util::opaque_pointer::*;
use mmtk::util::ObjectReference;
use mmtk::vm::Scanning;
use mmtk::{Mutator, TransitiveClosure};
use V8;
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
        _tls: VMWorkerThread,
    ) {
        unimplemented!()
    }

    fn notify_initial_thread_scan_complete(_partial_scan: bool, _tls: VMWorkerThread) {
        unimplemented!()
    }

    fn scan_objects<W: ProcessEdgesWork<VM = V8>>(
        objects: &[ObjectReference],
        worker: &mut GCWorker<V8>,
    ) {
        unsafe {
            debug_assert!(OBJECTS_TO_SCAN.is_empty());
            OBJECTS_TO_SCAN = objects.to_vec();
            while !OBJECTS_TO_SCAN.is_empty() {
                let objects = OBJECTS_TO_SCAN.clone();
                OBJECTS_TO_SCAN = vec![];
                let buf = objects.as_ptr();
                let len = objects.len();
                ((*UPCALLS).scan_objects)(buf, len, create_process_edges_work::<W> as _, trace_slot::<W> as _, worker as *mut _ as _);
            }
        }
    }

    fn scan_thread_roots<W: ProcessEdgesWork<VM = V8>>() {
        unimplemented!()
    }

    fn scan_thread_root<W: ProcessEdgesWork<VM = V8>>(
        _mutator: &'static mut Mutator<V8>,
        _tls: VMWorkerThread,
    ) {
        unimplemented!()
    }

    fn scan_vm_specific_roots<W: ProcessEdgesWork<VM = V8>>() {
        mmtk::memory_manager::add_work_packet(
            &SINGLETON,
            WorkBucketStage::Closure,
            ScanAndForwardRoots::<W>::new(),
        );
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }
}

pub struct ScanAndForwardRoots<E: ProcessEdgesWork<VM = V8>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = V8>> ScanAndForwardRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = V8>> GCWork<V8> for ScanAndForwardRoots<E> {
    fn do_work(&mut self, worker: &mut GCWorker<V8>, _mmtk: &'static MMTK<V8>) {
        unsafe {
            debug_assert!(ROOT_OBJECTS.is_empty());
            ((*UPCALLS).scan_roots)(trace_root::<E> as _, worker as *mut _ as _);
            if !ROOT_OBJECTS.is_empty() {
                flush_roots::<E>(worker);
            }
            debug_assert!(ROOT_OBJECTS.is_empty());
        }
    }
}

#[thread_local]
static mut ROOT_OBJECTS: Vec<ObjectReference> = Vec::new();

fn flush_roots<W: ProcessEdgesWork<VM = V8>>(_worker: &mut GCWorker<V8>) {
    let mut buf = vec![];
    unsafe { std::mem::swap(&mut buf, &mut ROOT_OBJECTS); }
    let scan_objects_work = mmtk::scheduler::gc_work::ScanObjects::<W>::new(buf, false);
    mmtk::memory_manager::add_work_packet(
        &SINGLETON,
        WorkBucketStage::Closure,
        scan_objects_work,
    );
}

pub(crate) extern "C" fn trace_root<W: ProcessEdgesWork<VM = V8>>(slot: Address, worker: &'static mut GCWorker<V8>) -> ObjectReference {
    let obj: ObjectReference = unsafe { slot.load() };
    let tag = obj.to_address().as_usize() & 0b11usize;
    let mut w = W::new(vec![], false, &SINGLETON);
    w.set_worker(worker);
    let object_untagged = unsafe {
        Address::from_usize(obj.to_address().as_usize() & !0b11usize).to_object_reference()
    };
    let new_obj = w.trace_object(object_untagged);
    // println!("Root {:?} {:?} -> {:?}", slot, obj, new_obj);
    if W::OVERWRITE_REFERENCE {
        unsafe {
            slot.store((new_obj.to_address().as_usize() & !0b11) | tag);
        }
    }
    unsafe {
        if ROOT_OBJECTS.is_empty() {
            ROOT_OBJECTS.reserve(W::CAPACITY);
        }
    }
    for o in &w.nodes {
        unsafe { ROOT_OBJECTS.push(*o); }
    }
    unsafe {
        if ROOT_OBJECTS.len() > W::CAPACITY {
            flush_roots::<W>(worker);
        }
    }
    new_obj
}

#[thread_local]
static mut OBJECTS_TO_SCAN: Vec<ObjectReference> = Vec::new();

pub(crate) extern "C" fn trace_slot<W: ProcessEdgesWork<VM = V8>>(slot: Address, worker: &'static mut GCWorker<V8>) -> ObjectReference {
    let obj: ObjectReference = unsafe { slot.load() };
    let tag = obj.to_address().as_usize() & 0b11usize;
    let mut w = W::new(vec![], false, &SINGLETON);
    w.set_worker(worker);
    let object_untagged = unsafe {
        Address::from_usize(obj.to_address().as_usize() & !0b11usize).to_object_reference()
    };
    let new_obj = w.trace_object(object_untagged);
    if W::OVERWRITE_REFERENCE {
        unsafe {
            slot.store((new_obj.to_address().as_usize() & !0b11) | tag);
        }
    }
    unsafe {
        if OBJECTS_TO_SCAN.is_empty() {
            OBJECTS_TO_SCAN.reserve(W::CAPACITY);
        }
    }
    for o in &w.nodes {
        unsafe { OBJECTS_TO_SCAN.push(*o); }
    }
    new_obj
}

pub(crate) extern "C" fn create_process_edges_work<W: ProcessEdgesWork<VM = V8>>(
    ptr: *mut Address,
    length: usize,
    capacity: usize,
) -> NewBuffer {
    if !ptr.is_null() {
        let buf = unsafe { Vec::<Address>::from_raw_parts(ptr, length, capacity) };
        mmtk::memory_manager::add_work_packet(
            &SINGLETON,
            WorkBucketStage::Closure,
            W::new(buf, false, &SINGLETON),
        );
    }
    let (ptr, _, capacity) = Vec::with_capacity(W::CAPACITY).into_raw_parts();
    NewBuffer { ptr, capacity }
}