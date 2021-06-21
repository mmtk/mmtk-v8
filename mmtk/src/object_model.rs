use super::UPCALLS;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::*;
use mmtk::AllocationSemantics;
use mmtk::CopyContext;
use V8;

pub struct VMObjectModel {}

impl ObjectModel<V8> for VMObjectModel {
    const GC_BYTE_OFFSET: isize = 7;

    #[inline(always)]
    fn copy(
        from: ObjectReference,
        allocator: AllocationSemantics,
        copy_context: &mut impl CopyContext,
    ) -> ObjectReference {
        let bytes = unsafe { ((*UPCALLS).get_object_size)(from) };
        let dst = copy_context.alloc_copy(from, bytes, ::std::mem::size_of::<usize>(), 0, allocator);
        // Copy
        // println!("Copy {:?} -> {:?}", from, dst);
        let src = from.to_address();
        for i in 0..bytes {
            unsafe { (dst + i).store((src + i).load::<u8>()) };
        }
        let to_obj = unsafe { dst.to_object_reference() };
        copy_context.post_copy(to_obj, unsafe { Address::zero() }, bytes, allocator);
        unsafe { ((*UPCALLS).on_move_event)(from, to_obj, bytes) };
        super::object_archive::global_object_archive().copy(src, dst);
        to_obj
    }

    fn copy_to(_from: ObjectReference, _to: ObjectReference, _region: Address) -> Address {
        unimplemented!()
    }

    fn get_reference_when_copied_to(_from: ObjectReference, _to: Address) -> ObjectReference {
        unimplemented!()
    }

    fn get_current_size(_object: ObjectReference) -> usize {
        unimplemented!()
    }

    fn get_type_descriptor(_reference: ObjectReference) -> &'static [i8] {
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
        unsafe { Address::from_usize(object.to_address().as_usize() & !0b1) }
    }
}
