use mmtk::vm::ReferenceGlue;
use mmtk::util::ObjectReference;
use mmtk::TraceLocal;
use mmtk::util::OpaquePointer;
use V8;

pub struct VMReferenceGlue {}

impl ReferenceGlue<V8> for VMReferenceGlue {
    fn set_referent(_reff: ObjectReference, _referent: ObjectReference) {
        unimplemented!()
    }
    fn get_referent(_object: ObjectReference) -> ObjectReference {
        unimplemented!()
    }
    fn process_reference<T: TraceLocal>(_trace: &mut T, _reference: ObjectReference, _tls: OpaquePointer) -> ObjectReference {
        unimplemented!()
    }
}