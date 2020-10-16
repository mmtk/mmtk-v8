use libc::c_void;
use mmtk::vm::Collection;
use mmtk::util::OpaquePointer;
use mmtk::{MutatorContext, ParallelCollector};

use V8;
use UPCALLS;

pub struct VMCollection {}

impl Collection<V8> for VMCollection {
    fn stop_all_mutators(tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).stop_all_mutators)(tls);
        }
    }

    fn resume_mutators(tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).resume_mutators)(tls);
        }
    }

    fn block_for_gc(tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).block_for_gc)();
        }
    }

    fn spawn_worker_thread<T: ParallelCollector<V8>>(tls: OpaquePointer, ctx: Option<&mut T>) {
        let ctx_ptr = if let Some(r) = ctx {
            r as *mut T
        } else {
            std::ptr::null_mut()
        };
        unsafe {
            ((*UPCALLS).spawn_collector_thread)(tls, ctx_ptr as usize as _);
        }
    }

    fn prepare_mutator<T: MutatorContext<V8>>(tls: OpaquePointer, m: &T) {
        // unimplemented!()
    }
}