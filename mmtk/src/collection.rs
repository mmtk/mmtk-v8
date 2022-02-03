use mmtk::scheduler::ProcessEdgesWork;
use mmtk::util::opaque_pointer::*;
use mmtk::vm::{Collection, GCThreadContext};
use mmtk::MutatorContext;

use UPCALLS;
use V8;

const GC_THREAD_KIND_CONTROLLER: libc::c_int = 0;
const GC_THREAD_KIND_WORKER: libc::c_int = 1;

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

    fn spawn_gc_thread(tls: VMThread, ctx: GCThreadContext<V8>) {
        let (ctx_ptr, kind) = match ctx {
            GCThreadContext::Controller(b) => (
                Box::into_raw(b) as *mut libc::c_void,
                GC_THREAD_KIND_CONTROLLER,
            ),
            GCThreadContext::Worker(b) => {
                (Box::into_raw(b) as *mut libc::c_void, GC_THREAD_KIND_WORKER)
            }
        };
        unsafe {
            ((*UPCALLS).spawn_gc_thread)(tls, kind, ctx_ptr);
        }
    }

    fn prepare_mutator<T: MutatorContext<V8>>(
        _tls_worker: VMWorkerThread,
        _tls_mutator: VMMutatorThread,
        _m: &T,
    ) {
        unimplemented!()
    }
}
