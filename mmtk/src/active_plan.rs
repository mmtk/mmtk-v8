use mmtk::Plan;
use mmtk::Mutator;
use mmtk::scheduler::GCWorker;
use mmtk::vm::ActivePlan;
use mmtk::util::OpaquePointer;
use V8;
use SINGLETON;
use super::UPCALLS;
use std::sync::Mutex;

pub struct VMActivePlan<> {}

impl ActivePlan<V8> for VMActivePlan {
    fn global() -> &'static dyn Plan<VM=V8> {
        &*SINGLETON.plan
    }

    unsafe fn worker(tls: OpaquePointer) -> &'static mut GCWorker<V8> {
        let c = unsafe { ((*UPCALLS).active_collector)(tls) };
        assert!(!c.is_null());
        unsafe { &mut *c }
    }

    unsafe fn is_mutator(tls: OpaquePointer) -> bool {
        ((*UPCALLS).is_mutator)(tls)
    }

    unsafe fn mutator(tls: OpaquePointer) -> &'static mut Mutator<V8> {
        let m = ((*UPCALLS).get_mmtk_mutator)(tls);
        &mut *m
    }

    fn reset_mutator_iterator() {
        unsafe {
            ((*UPCALLS).reset_mutator_iterator)();
        }
    }

    fn get_next_mutator() -> Option<&'static mut Mutator<V8>> {
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
