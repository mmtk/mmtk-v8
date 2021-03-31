use mmtk::vm::*;
use mmtk::AllocationSemantics;
use mmtk::CopyContext;
use mmtk::util::{Address, ObjectReference};
use std::sync::atomic::{AtomicU8};
use super::UPCALLS;
use V8;

pub struct VMObjectModel {}

impl ObjectModel<V8> for VMObjectModel {
    const GC_BYTE_OFFSET: isize = 7;
    
    fn copy(from: ObjectReference, allocator: AllocationSemantics, copy_context: &mut impl CopyContext) -> ObjectReference {
        unimplemented!()
    }

    fn copy_to(from: ObjectReference, to: ObjectReference, region: Address) -> Address {
        unimplemented!()
    }

    fn get_reference_when_copied_to(from: ObjectReference, to: Address) -> ObjectReference {
        unimplemented!()
    }

    fn get_current_size(object: ObjectReference) -> usize {
        unimplemented!()
    }

    fn get_type_descriptor(reference: ObjectReference) -> &'static [i8] {
        unimplemented!()
    }

    fn object_start_ref(object: ObjectReference) -> Address {
        object.to_address()
    }

    fn dump_object(object: ObjectReference) {
        unsafe {
            ((*UPCALLS).dump_object)(object);
        }
    }

    fn ref_to_address(object: ObjectReference) -> Address {
        unimplemented!()
    }
}
