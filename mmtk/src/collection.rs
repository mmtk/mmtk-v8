use mmtk::scheduler::gc_work::ProcessEdgesWork;
use mmtk::scheduler::GCWorker;
use mmtk::util::OpaquePointer;
use mmtk::vm::Collection;
use mmtk::MutatorContext;

use UPCALLS;
use V8;

pub struct VMCollection {}

impl Collection<V8> for VMCollection {
    fn stop_all_mutators<E: ProcessEdgesWork<VM = V8>>(tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).stop_all_mutators)(tls);
        }
    }

    fn resume_mutators(tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).resume_mutators)(tls);
        }
    }

    fn block_for_gc(_tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).block_for_gc)();
        }
    }

    fn spawn_worker_thread(tls: OpaquePointer, ctx: Option<&GCWorker<V8>>) {
        let ctx_ptr = if let Some(r) = ctx {
            r as *const GCWorker<V8> as *mut GCWorker<V8>
        } else {
            std::ptr::null_mut()
        };
        unsafe {
            ((*UPCALLS).spawn_worker_thread)(tls, ctx_ptr as usize as _);
        }
    }

    fn prepare_mutator<T: MutatorContext<V8>>(
        _tls_worker: OpaquePointer,
        _tls_mutator: OpaquePointer,
        _m: &T,
    ) {
        unimplemented!()
    }
}
