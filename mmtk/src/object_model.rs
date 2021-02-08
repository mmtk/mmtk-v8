use mmtk::vm::*;
use mmtk::AllocationSemantics;
use mmtk::CopyContext;
use mmtk::util::{Address, ObjectReference};
use std::sync::atomic::{AtomicU8};
use super::V8_UPCALLS;
use V8;

pub struct VMObjectModel {}

impl ObjectModel<V8> for VMObjectModel {
    const HAS_GC_BYTE: bool = false;
    
    fn copy(from: ObjectReference, allocator: AllocationSemantics, copy_context: &mut impl CopyContext) -> ObjectReference {
        let bytes = get_current_size(from);
        let dst = copy_context.alloc_copy(from, bytes, ::std::mem::size_of::<usize>(), 0, allocator);
        // Copy
        let src = from.to_address();
        for i in 0..bytes {
            unsafe { (dst + i).store((src + i).load::<u8>()) };
        }
        let to_obj = unsafe { dst.to_object_reference() };
        copy_context.post_copy(to_obj, unsafe { Address::zero() }, bytes, allocator);
        to_obj
    }

    fn copy_to(from: ObjectReference, to: ObjectReference, region: Address) -> Address {
        unimplemented!()
    }

    fn get_reference_when_copied_to(from: ObjectReference, to: Address) -> ObjectReference {
        unimplemented!()
    }

    fn get_current_size(object: ObjectReference) -> usize {
        unsafe { ((*V8_UPCALLS).get_object_size)(from) }
    }

    fn get_type_descriptor(reference: ObjectReference) -> &'static [i8] {
        unimplemented!()
    }

    fn object_start_ref(object: ObjectReference) -> Address {
        object.to_address()
    }

    fn dump_object(object: ObjectReference) {
        unimplemented!()
    }

    fn ref_to_address(object: ObjectReference) -> Address {
        unimplemented!()
    }
}
