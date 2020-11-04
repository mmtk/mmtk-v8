use libc::c_void;
use mmtk::{Plan, SelectedPlan};
use mmtk::scheduler::GCWorker;
use mmtk::vm::ActivePlan;
use mmtk::util::OpaquePointer;
use V8;
use SINGLETON;
use super::UPCALLS;
use std::sync::Mutex;

pub struct VMActivePlan<> {}

impl ActivePlan<V8> for VMActivePlan {
    fn global() -> &'static SelectedPlan<V8> {
        &SINGLETON.plan
    }

    fn worker(tls: OpaquePointer) -> &'static mut GCWorker<V8> {
        let c = unsafe { ((*UPCALLS).active_collector)(tls) };
        assert!(!c.is_null());
        unsafe { &mut *c }
    }

    unsafe fn is_mutator(tls: OpaquePointer) -> bool {
        ((*UPCALLS).is_mutator)(tls)
    }

    unsafe fn mutator(tls: OpaquePointer) -> &'static mut <SelectedPlan<V8> as Plan>::Mutator {
        let m = ((*UPCALLS).get_mmtk_mutator)(tls);
        unsafe { &mut *m }
    }

    fn collector_count() -> usize {
        unimplemented!()
    }

    fn reset_mutator_iterator() {
        unsafe {
            ((*UPCALLS).reset_mutator_iterator)();
        }
    }

    fn get_next_mutator() -> Option<&'static mut <SelectedPlan<V8> as Plan>::Mutator> {
        let _guard = MUTATOR_ITERATOR_LOCK.lock().unwrap();
        unsafe {
            let m = ((*UPCALLS).get_next_mutator)();
            if m.is_null() {
                None
            } else {
                Some(&mut *m)
            }
        }
    }

    fn number_of_mutators() -> usize {
        unimplemented!()
    }
}

lazy_static! {
    pub static ref MUTATOR_ITERATOR_LOCK: Mutex<()> = Mutex::new(());
}
