use std::sync::atomic::Ordering;
use std::ptr;
use super::UPCALLS;
use mmtk::util::metadata::{header_metadata::HeaderMetadataSpec};
use mmtk::util::{Address, ObjectReference, metadata};
use mmtk::vm::*;
use mmtk::AllocationSemantics;
use mmtk::CopyContext;
use V8;

pub struct VMObjectModel {}

impl ObjectModel<V8> for VMObjectModel {
    const GLOBAL_LOG_BIT_SPEC: VMGlobalLogBitSpec = VMGlobalLogBitSpec::side_first();
    const LOCAL_FORWARDING_POINTER_SPEC: VMLocalForwardingPointerSpec = VMLocalForwardingPointerSpec::in_header(0);
    const LOCAL_FORWARDING_BITS_SPEC: VMLocalForwardingBitsSpec = VMLocalForwardingBitsSpec::side_first();
    const LOCAL_MARK_BIT_SPEC: VMLocalMarkBitSpec = VMLocalMarkBitSpec::side_after(&Self::LOCAL_FORWARDING_BITS_SPEC.as_spec());
    const LOCAL_LOS_MARK_NURSERY_SPEC: VMLocalLOSMarkNurserySpec = VMLocalLOSMarkNurserySpec::side_after(&Self::LOCAL_MARK_BIT_SPEC.as_spec());

    fn load_metadata(
        metadata_spec: &HeaderMetadataSpec,
        object: ObjectReference,
        optional_mask: Option<usize>,
        atomic_ordering: Option<Ordering>,
    ) -> usize {
        metadata::header_metadata::load_metadata(metadata_spec, object, optional_mask, atomic_ordering)
    }

    fn store_metadata(
        metadata_spec: &HeaderMetadataSpec,
        object: ObjectReference,
        val: usize,
        _optional_mask: Option<usize>,
        atomic_ordering: Option<Ordering>,
    ) {
        metadata::header_metadata::store_metadata(
            metadata_spec,
            object,
            val,
            None,
            atomic_ordering,
        );
    }

    fn compare_exchange_metadata(
        _metadata_spec: &HeaderMetadataSpec,
        _object: ObjectReference,
        _old_val: usize,
        _new_val: usize,
        _mask: Option<usize>,
        _success_order: Ordering,
        _failure_order: Ordering,
    ) -> bool {
        unimplemented!()
    }

    fn fetch_add_metadata(
        _metadata_spec: &HeaderMetadataSpec,
        _object: ObjectReference,
        _val: usize,
        _order: Ordering,
    ) -> usize {
        unimplemented!()
    }

    fn fetch_sub_metadata(
        _metadata_spec: &HeaderMetadataSpec,
        _object: ObjectReference,
        _val: usize,
        _order: Ordering,
    ) -> usize {
        unimplemented!()
    }

    #[inline(always)]
    fn copy(
        from: ObjectReference,
        allocator: AllocationSemantics,
        copy_context: &mut impl CopyContext,
    ) -> ObjectReference {
        let bytes = unsafe { ((*UPCALLS).get_object_size)(from) };
        let dst = copy_context.alloc_copy(from, bytes, ::std::mem::size_of::<usize>(), 0, allocator);
        // Copy
        unsafe {
            ptr::copy_nonoverlapping::<u8>(from.to_address().to_ptr(), dst.to_mut_ptr(), bytes);
        }
        let to_obj = unsafe { dst.to_object_reference() };
        copy_context.post_copy(to_obj, unsafe { Address::zero() }, bytes, allocator);
        unsafe { ((*UPCALLS).on_move_event)(from, to_obj, bytes) };
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
