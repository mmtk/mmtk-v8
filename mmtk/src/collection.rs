use mmtk::scheduler::gc_work::ProcessEdgesWork;
use mmtk::scheduler::GCWorker;
use mmtk::util::*;
use mmtk::vm::Collection;
use mmtk::{MutatorContext, MMTK};

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
        } as usize;
        std::thread::spawn(move || {
            let mmtk: *mut MMTK<V8> = &*crate::SINGLETON as *const MMTK<V8> as *mut MMTK<V8>;
            if ctx_ptr == 0 {
                crate::api::start_control_collector(unsafe { &mut *mmtk }, OpaquePointer::UNINITIALIZED);
            } else {
                crate::api::start_worker(unsafe { &mut *mmtk }, OpaquePointer::UNINITIALIZED, unsafe { &mut *(ctx_ptr as *mut GCWorker<V8>) });
            }
        });
    }

    fn prepare_mutator<T: MutatorContext<V8>>(_tls: OpaquePointer, _m: &T) {
        unimplemented!()
    }

    fn sweep(addr: Address) {
        unsafe {
            mmtk_delete_object(addr)
        }
    }
}

extern {
    fn mmtk_delete_object(addr: Address);
}
