use super::UPCALLS;
use mmtk::util::opaque_pointer::*;
use mmtk::vm::ActivePlan;
use mmtk::Mutator;
use std::sync::Mutex;
use V8;

pub struct VMActivePlan {}

impl ActivePlan<V8> for VMActivePlan {
    fn is_mutator(tls: VMThread) -> bool {
        unsafe { ((*UPCALLS).is_mutator)(tls) }
    }

    fn mutator(tls: VMMutatorThread) -> &'static mut Mutator<V8> {
        unsafe {
            let m = ((*UPCALLS).get_mmtk_mutator)(tls);
            &mut *m
        }
    }

    fn mutators<'a>() -> Box<dyn Iterator<Item = &'a mut Mutator<V8>> + 'a> {
        unimplemented!()
    }

    fn number_of_mutators() -> usize {
        unimplemented!()
    }
}

lazy_static! {
    pub static ref MUTATOR_ITERATOR_LOCK: Mutex<()> = Mutex::new(());
}
