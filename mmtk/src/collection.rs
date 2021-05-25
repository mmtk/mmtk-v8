use mmtk::scheduler::GCWorker;
use mmtk::util::*;
use mmtk::scheduler::ProcessEdgesWork;
use mmtk::vm::Collection;
use mmtk::{MutatorContext, MMTK};

use UPCALLS;
use V8;

use crate::object_archive::global_object_archive;

pub struct VMCollection {}

impl Collection<V8> for VMCollection {
    fn stop_all_mutators<E: ProcessEdgesWork<VM = V8>>(tls: VMWorkerThread) {
        unsafe {
            ((*UPCALLS).stop_all_mutators)(tls);
        }
    }

    fn resume_mutators(tls: VMWorkerThread) {
        unsafe {
            ((*UPCALLS).resume_mutators)(tls);
        }
    }

    fn block_for_gc(_tls: VMMutatorThread) {
        unsafe {
            ((*UPCALLS).block_for_gc)();
        }
    }

    fn spawn_worker_thread(_tls: VMThread, ctx: Option<&GCWorker<V8>>) {
        let ctx_ptr = if let Some(r) = ctx {
            r as *const GCWorker<V8> as *mut GCWorker<V8>
        } else {
            std::ptr::null_mut()
        } as usize;
        std::thread::spawn(move || {
            let mmtk: *mut MMTK<V8> = &*crate::SINGLETON as *const MMTK<V8> as *mut MMTK<V8>;
            if ctx_ptr == 0 {
                crate::api::start_control_collector(unsafe { &mut *mmtk }, VMWorkerThread(VMThread::UNINITIALIZED));
            } else {
                crate::api::start_worker(unsafe { &mut *mmtk }, VMWorkerThread(VMThread::UNINITIALIZED), unsafe { &mut *(ctx_ptr as *mut GCWorker<V8>) });
            }
        });
    }

    fn prepare_mutator<T: MutatorContext<V8>>(
        _tls_worker: VMWorkerThread,
        _tls_mutator: VMMutatorThread,
        _m: &T,
    ) {
        unimplemented!()
    }

    fn update_object_archive() {
        global_object_archive().update();
    }

    fn process_weak_refs() {
        unsafe {
            ((*UPCALLS).process_weak_refs)();
        }
    }
}
