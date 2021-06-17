#ifndef MMTK_VISITORS_H
#define MMTK_VISITORS_H

#include "mmtkUpcalls.h"
#include "log.h"
#include <mutex>
#include <condition_variable>
#include "src/objects/slots-inl.h"
#include "src/heap/safepoint.h"
#include "src/codegen/reloc-info.h"
#include "src/objects/code.h"
#include "src/objects/code-inl.h"
#include "src/objects/map-inl.h"
#include "src/codegen/assembler-inl.h"
#include "src/heap/objects-visiting-inl.h"
#include "weak-refs.h"
#include <unordered_set>


namespace mmtk {

namespace i = v8::internal;
namespace tph = v8::internal::third_party_heap;

class MMTkRootVisitor: public i::RootVisitor {
 public:
  explicit MMTkRootVisitor(i::Heap* heap, TraceRootFn trace_root, void* context, int task_id)
      : heap_(heap), trace_root_(trace_root), context_(context), task_id_(task_id) {
    USE(heap_);
    USE(task_id_);
    DCHECK(task_id <= 8);
  }

  virtual void VisitRootPointer(i::Root root, const char* description, i::FullObjectSlot p) override final {
    ProcessRootEdge(root, p);
  }

  virtual void VisitRootPointers(i::Root root, const char* description, i::FullObjectSlot start, i::FullObjectSlot end) override final {
    for (auto p = start; p < end; ++p) ProcessRootEdge(root, p);
  }

  virtual void VisitRootPointers(i::Root root, const char* description, i::OffHeapObjectSlot start, i::OffHeapObjectSlot end) override final {
    for (auto p = start; p < end; ++p) ProcessRootEdge(root, p);
  }

 private:
  V8_INLINE void ProcessRootEdge(i::Root root, i::FullObjectSlot slot) {
    DCHECK(!i::HasWeakHeapObjectTag(*slot));
    i::HeapObject object;
    if ((*slot).GetHeapObject(&object)) {
      trace_root_((void*) slot.address(), context_);
    }
  }

  v8::internal::Heap* heap_;
  TraceRootFn trace_root_;
  void* context_;
  int task_id_;
};

class MMTkCustomRootBodyVisitor final : public i::ObjectVisitor {
 public:
  explicit MMTkCustomRootBodyVisitor(i::Heap* heap, TraceRootFn trace_root, void* context, int task_id)
      : heap_(heap), trace_root_(trace_root), context_(context), task_id_(task_id) {
    USE(heap_);
    USE(task_id_);
    DCHECK(task_id <= 8);
  }

  void VisitPointer(i::HeapObject host, i::ObjectSlot p) final {}

  void VisitMapPointer(i::HeapObject host) final {}

  void VisitPointers(i::HeapObject host, i::ObjectSlot start, i::ObjectSlot end) final {}

  void VisitPointers(i::HeapObject host, i::MaybeObjectSlot start,
                     i::MaybeObjectSlot end) final {
    // At the moment, custom roots cannot contain weak pointers.
    UNREACHABLE();
  }

  // VisitEmbedderPointer is defined by ObjectVisitor to call VisitPointers.
  void VisitCodeTarget(i::Code host, i::RelocInfo* rinfo) override {
    auto target = i::Code::GetCodeFromTargetAddress(rinfo->target_address());
    DCHECK(!mmtk_is_movable(target));
    trace_root_((void*) &target, context_);
    DCHECK_EQ(target, i::Code::GetCodeFromTargetAddress(rinfo->target_address()));
  }

  void VisitEmbeddedPointer(i::Code host, i::RelocInfo* rinfo) override {
    auto o = rinfo->target_object();
    trace_root_((void*) &o, context_);
    if (o != rinfo->target_object()) rinfo->set_target_object(heap_, o);
  }

 private:
  v8::internal::Heap* heap_;
  TraceRootFn trace_root_;
  void* context_;
  int task_id_;
};

class MMTkEdgeVisitor: public i::HeapVisitor<void, MMTkEdgeVisitor> {
 public:
  explicit MMTkEdgeVisitor(i::Heap* heap, ProcessEdgesFn process_edges, TraceFieldFn trace_field, void* context, int task_id)
      : heap_(heap), process_edges_(process_edges), trace_field_(trace_field), context_(context), task_id_(task_id) {
    USE(heap_);
    DCHECK(task_id <= 8);
    NewBuffer buf = process_edges(NULL, 0, 0);
    buffer_ = buf.buf;
    cap_ = buf.cap;
  }

  virtual ~MMTkEdgeVisitor() {
    if (cursor_ > 0) flush();
    if (buffer_ != NULL) {
      release_buffer(buffer_, cursor_, cap_);
    }
  }

  // V8_INLINE bool AllowDefaultJSObjectVisit() { return false; }

  // V8_INLINE void VisitBytecodeArray(i::Map map, i::BytecodeArray object) {
  //   if (!ShouldVisit(object)) return;
  //   int size = i::BytecodeArray::BodyDescriptor::SizeOf(map, object);
  //   this->VisitMapPointer(object);
  //   i::BytecodeArray::BodyDescriptor::IterateBody(map, object, size, this);
  //   // if (!is_forced_gc_) {
  //     object.MakeOlder();
  //   // }
  // }
  // V8_INLINE void VisitDescriptorArray(i::Map map, i::DescriptorArray array) {
  //   if (!ShouldVisit(array)) return;
  //   VisitMapPointer(array);
  //   // int size = i::DescriptorArray::BodyDescriptor::SizeOf(map, array);
  //   VisitPointers(array, array.GetFirstPointerSlot(), array.GetDescriptorSlot(0));
  //   VisitDescriptors(array, array.number_of_descriptors());
  // }
  // V8_INLINE void VisitEphemeronHashTable(i::Map map, i::EphemeronHashTable table) {
  //   if (!ShouldVisit(table)) return;
  //   weak_objects_->ephemeron_hash_tables.Push(task_id_, table);

  //   for (i::InternalIndex i : table.IterateEntries()) {
  //     // i::ObjectSlot key_slot =
  //     //     table.RawFieldOfElementAt(i::EphemeronHashTable::EntryToIndex(i));
  //     // i::HeapObject key = i::HeapObject::cast(table.KeyAt(i));

  //     // SynchronizePageAccess(key);
  //     // RecordSlot(table, key_slot, key);

  //     i::ObjectSlot value_slot =
  //         table.RawFieldOfElementAt(i::EphemeronHashTable::EntryToValueIndex(i));

  //     // if (marking_state()->IsBlackOrGrey(key)) {
  //       VisitPointer(table, value_slot);
  //     // } else {
  //     //   i::Object value_obj = table.ValueAt(i);

  //     //   if (value_obj.IsHeapObject()) {
  //     //     i::HeapObject value = i::HeapObject::cast(value_obj);
  //     //     // SynchronizePageAccess(value);
  //     //     // RecordSlot(table, value_slot, value);

  //     //     // Revisit ephemerons with both key and value unreachable at end
  //     //     // of concurrent marking cycle.
  //     //     if (marking_state()->IsWhite(value)) {
  //     //       weak_objects_->discovered_ephemerons.Push(task_id_, i::Ephemeron{key, value});
  //     //     }
  //     //   }
  //     // }
  //   }
  // }
  // V8_INLINE void VisitFixedArray(i::Map map, i::FixedArray object) {
  //   // Arrays with the progress bar are not left-trimmable because they reside
  //   // in the large object space.
  //   VisitLeftTrimmableArray(map, object);
  // }
  // V8_INLINE void VisitFixedDoubleArray(i::Map map, i::FixedDoubleArray object) {
  //   VisitLeftTrimmableArray(map, object);
  // }
  // V8_INLINE void VisitJSApiObject(i::Map map, i::JSObject object) {
  //   VisitEmbedderTracingSubclass(map, object);
  // }
  // V8_INLINE void VisitJSArrayBuffer(i::Map map, i::JSArrayBuffer object) {
  //   object.MarkExtension();
  //   VisitEmbedderTracingSubclass(map, object);
  // }
  // V8_INLINE void VisitJSDataView(i::Map map, i::JSDataView object) {
  //   VisitEmbedderTracingSubclass(map, object);
  // }
  // V8_INLINE void VisitJSFunction(i::Map map, i::JSFunction object) {
  //   VisitJSObjectSubclass(map, object);
  //   // Check if the JSFunction needs reset due to bytecode being flushed.
  //   if (/*bytecode_flush_mode_ != BytecodeFlushMode::kDoNotFlushBytecode &&*/
  //       object.NeedsResetDueToFlushedBytecode()) {
  //     weak_objects_->flushed_js_functions.Push(task_id_, object);
  //   }
  // }
  // V8_INLINE void VisitJSTypedArray(i::Map map, i::JSTypedArray object) {
  //   VisitEmbedderTracingSubclass(map, object);
  // }
  // V8_INLINE void VisitJSWeakRef(i::Map map, i::JSWeakRef weak_ref) {
  //   VisitJSObjectSubclass(map, weak_ref);
  //   // if (size == 0) return 0;
  //   if (weak_ref.target().IsHeapObject()) {
  //     // i::HeapObject target = i::HeapObject::cast(weak_ref.target());
  //     // SynchronizePageAccess(target);
  //     // if (concrete_visitor()->marking_state()->IsBlackOrGrey(target)) {
  //     //   // Record the slot inside the JSWeakRef, since the
  //     //   // VisitJSObjectSubclass above didn't visit it.
  //     //   ObjectSlot slot = weak_ref.RawField(JSWeakRef::kTargetOffset);
  //     //   concrete_visitor()->RecordSlot(weak_ref, slot, target);
  //     // } else {
  //       // JSWeakRef points to a potentially dead object. We have to process
  //       // them when we know the liveness of the whole transitive closure.
  //       weak_objects_->js_weak_refs.Push(task_id_, weak_ref);
  //     // }
  //   }
  // }
  // V8_INLINE void VisitMap(i::Map meta_map, i::Map map) {
  //   if (!ShouldVisit(map)) return;
  //   i::Map::BodyDescriptor::SizeOf(meta_map, map);
  //   // VisitDescriptorsForMap(map);

  //   // Mark the pointer fields of the Map. If there is a transitions array, it has
  //   // been marked already, so it is fine that one of these fields contains a
  //   // pointer to it.
  //   i::Map::BodyDescriptor::IterateBody(meta_map, map, map.Size(), this);
  // }
  // V8_INLINE void VisitSharedFunctionInfo(i::Map map, i::SharedFunctionInfo shared_info) {
  //   if (!ShouldVisit(shared_info)) return;

  //   int size = i::SharedFunctionInfo::BodyDescriptor::SizeOf(map, shared_info);
  //   VisitMapPointer(shared_info);
  //   i::SharedFunctionInfo::BodyDescriptor::IterateBody(map, shared_info, size, this);

  //   // If the SharedFunctionInfo has old bytecode, mark it as flushable,
  //   // otherwise visit the function data field strongly.
  //   if (shared_info.ShouldFlushBytecode(i::Heap::GetBytecodeFlushMode(heap_->isolate()))) {
  //     weak_objects_->bytecode_flushing_candidates.Push(task_id_, shared_info);
  //   } else {
  //     VisitPointer(shared_info, shared_info.RawField(i::SharedFunctionInfo::kFunctionDataOffset));
  //   }
  // }
  V8_INLINE void VisitTransitionArray(i::Map map, i::TransitionArray array) {
    // if (!ShouldVisit(array)) return;
    VisitMapPointer(array);
    int size = i::TransitionArray::BodyDescriptor::SizeOf(map, array);
    i::TransitionArray::BodyDescriptor::IterateBody(map, array, size, this);
    weak_objects_->transition_arrays.Push(task_id_, array);
  }
  // V8_INLINE void VisitWeakCell(i::Map map, i::WeakCell weak_cell) {
  //   if (!ShouldVisit(weak_cell)) return;

  //   int size = i::WeakCell::BodyDescriptor::SizeOf(map, weak_cell);
  //   this->VisitMapPointer(weak_cell);
  //   i::WeakCell::BodyDescriptor::IterateBody(map, weak_cell, size, this);
  //   // i::HeapObject target = weak_cell.relaxed_target();
  //   // i::HeapObject unregister_token = weak_cell.relaxed_unregister_token();
  //   // concrete_visitor()->SynchronizePageAccess(target);
  //   // concrete_visitor()->SynchronizePageAccess(unregister_token);
  //   // if (concrete_visitor()->marking_state()->IsBlackOrGrey(target) &&
  //   //     concrete_visitor()->marking_state()->IsBlackOrGrey(unregister_token)) {
  //   //   // Record the slots inside the WeakCell, since the IterateBody above
  //   //   // didn't visit it.
  //   //   ObjectSlot slot = weak_cell.RawField(WeakCell::kTargetOffset);
  //   //   concrete_visitor()->RecordSlot(weak_cell, slot, target);
  //   //   slot = weak_cell.RawField(WeakCell::kUnregisterTokenOffset);
  //   //   concrete_visitor()->RecordSlot(weak_cell, slot, unregister_token);
  //   // } else {
  //     // WeakCell points to a potentially dead object or a dead unregister
  //     // token. We have to process them when we know the liveness of the whole
  //     // transitive closure.
  //     weak_objects_->weak_cells.Push(task_id_, weak_cell);
  //   // }
  // }

  // void VisitDescriptors(i::DescriptorArray descriptor_array, int number_of_own_descriptors) {
  //   // int16_t new_marked = static_cast<int16_t>(number_of_own_descriptors);
  //   // int16_t old_marked = descriptor_array.UpdateNumberOfMarkedDescriptors(
  //   //     mark_compact_epoch_, new_marked);
  //   // if (old_marked < new_marked) {
  //   //   VisitPointers(descriptor_array,
  //   //       MaybeObjectSlot(descriptor_array.GetDescriptorSlot(old_marked)),
  //   //       MaybeObjectSlot(descriptor_array.GetDescriptorSlot(new_marked)));
  //   // }
  // }

  // template <typename T>
  // void VisitLeftTrimmableArray(i::Map map, T object) {
  //   // The length() function checks that the length is a Smi.
  //   // This is not necessarily the case if the array is being left-trimmed.
  //   i::Object length = object.unchecked_length(v8::kAcquireLoad);
  //   if (!ShouldVisit(object)) return;
  //   // The cached length must be the actual length as the array is not black.
  //   // Left trimming marks the array black before over-writing the length.
  //   DCHECK(length.IsSmi());
  //   int size = T::SizeFor(i::Smi::ToInt(length));
  //   VisitMapPointer(object);
  //   T::BodyDescriptor::IterateBody(map, object, size, this);
  // }

  // template <typename T>
  // void VisitEmbedderTracingSubclass(i::Map map, T object) {
  //   DCHECK(object.IsApiWrapper());
  //   VisitJSObjectSubclass(map, object);
  //   // if (size && is_embedder_tracing_enabled_) {
  //   //   // Success: The object needs to be processed for embedder references on
  //   //   // the main thread.
  //   //   local_marking_worklists_->PushEmbedder(object);
  //   // }
  // }

  // template <typename T>
  // void VisitJSObjectSubclass(i::Map map, T object) {
  //   if (!ShouldVisit(object)) return;
  //   VisitMapPointer(object);
  //   int size = T::BodyDescriptor::SizeOf(map, object);
  //   T::BodyDescriptor::IterateBody(map, object, size, this);
  // }


  // V8_INLINE void VisitSharedFunctionInfo(i::Map map, i::SharedFunctionInfo shared_info) {
  //   // If the SharedFunctionInfo has old bytecode, mark it as flushable,
  //   // otherwise visit the function data field strongly.
  //   if (shared_info.ShouldFlushBytecode(i::Heap::GetBytecodeFlushMode(heap_->isolate()))) {
  //     weak_objects_->bytecode_flushing_candidates.Push(task_id_, shared_info);
  //   } else {
  //     VisitPointer(shared_info, shared_info.RawField(i::SharedFunctionInfo::kFunctionDataOffset));
  //   }
  // }

  // V8_INLINE void VisitJSFunction(i::Map map, i::JSFunction object) {
  //   if (v8::internal::Heap::GetBytecodeFlushMode(heap_->isolate()) != i::BytecodeFlushMode::kDoNotFlushBytecode && object.NeedsResetDueToFlushedBytecode()) {
  //     weak_objects_->flushed_js_functions.Push(task_id_, object);
  //   }
  // }

  // V8_INLINE void VisitTransitionArray(i::Map map, i::TransitionArray array) {
  //   weak_objects_->transition_arrays.Push(task_id_, array);
  // }

  // void VisitEphemeronHashTable(i::Map map, i::EphemeronHashTable table) {
  //   weak_objects_->ephemeron_hash_tables.Push(task_id_, table);
  //   for (i::InternalIndex i : table.IterateEntries()) {
  //     // ObjectSlot key_slot =
  //     //     table.RawFieldOfElementAt(EphemeronHashTable::EntryToIndex(i));
  //     auto key = i::HeapObject::cast(table.KeyAt(i));

  //     // concrete_visitor()->SynchronizePageAccess(key);
  //     // concrete_visitor()->RecordSlot(table, key_slot, key);

  //     // ObjectSlot value_slot = table.RawFieldOfElementAt(EphemeronHashTable::EntryToValueIndex(i));

  //     // if (concrete_visitor()->marking_state()->IsBlackOrGrey(key)) {
  //       // VisitPointer(table, value_slot);
  //     // } else {
  //       auto value_obj = table.ValueAt(i);
  //       if (value_obj.IsHeapObject()) {
  //         auto value = i::HeapObject::cast(value_obj);
  //         // concrete_visitor()->SynchronizePageAccess(value);
  //         // concrete_visitor()->RecordSlot(table, value_slot, value);
  //         // Revisit ephemerons with both key and value unreachable at end
  //         // of concurrent marking cycle.
  //         if (!mmtk::is_live(value)) {
  //           weak_objects_->discovered_ephemerons.Push(task_id_, i::Ephemeron{key, value});
  //         }
  //       }
  //     // }
  //   }
  // }

  // void VisitWeakCell(i::Map map, i::WeakCell weak_cell) {
  //   auto target = weak_cell.relaxed_target();
  //   auto unregister_token = weak_cell.relaxed_unregister_token();
  //   if (!mmtk::is_live(target) || !mmtk::is_live(unregister_token)) {
  //     // WeakCell points to a potentially dead object or a dead unregister
  //     // token. We have to process them when we know the liveness of the whole
  //     // transitive closure.
  //     weak_objects_->weak_cells.Push(task_id_, weak_cell);
  //   }
  // }

  // void VisitJSWeakRef(i::Map map, i::JSWeakRef weak_ref) {
  //   if (weak_ref.target().IsHeapObject()) {
  //     weak_objects_->js_weak_refs.Push(task_id_, weak_ref);
  //   }
  // }

  virtual void VisitPointers(i::HeapObject host, i::ObjectSlot start, i::ObjectSlot end) override final {
    for (auto p = start; p < end; ++p) ProcessEdge(host, p);
  }

  virtual void VisitPointers(i::HeapObject host, i::MaybeObjectSlot start, i::MaybeObjectSlot end) override final {
    for (auto p = start; p < end; ++p) ProcessEdge(host, p);
  }

  virtual void VisitCodeTarget(i::Code host, i::RelocInfo* rinfo) override final {
    auto target = i::Code::GetCodeFromTargetAddress(rinfo->target_address());
    DCHECK(!mmtk_is_movable(target));
    trace_field_((void*) &target, context_);
    DCHECK_EQ(target, i::Code::GetCodeFromTargetAddress(rinfo->target_address()));
  }

  virtual void VisitEmbeddedPointer(i::Code host, i::RelocInfo* rinfo) override final {
    auto o = rinfo->target_object();
    auto f = mmtk::get_forwarded_ref(o);
    if (f) {
      rinfo->set_target_object(heap_, *f);
    } else if (host.IsWeakObject(o)) {
        DCHECK(i::RelocInfo::IsCodeTarget(rinfo->rmode()) || i::RelocInfo::IsEmbeddedObjectMode(rinfo->rmode()));
        weak_objects_->weak_objects_in_code.Push(task_id_, std::make_pair(o, host));
    } else {
      trace_field_((void*) &o, context_);
      if (o != rinfo->target_object()) rinfo->set_target_object(heap_, o);
    }
  }

  virtual void VisitMapPointer(i::HeapObject host) override final {
    ProcessEdge(host, host.map_slot());
  }

  // Custom weak pointers must be ignored by the GC but not other
  // visitors. They're used for e.g., lists that are recreated after GC. The
  // default implementation treats them as strong pointers. Visitors who want to
  // ignore them must override this function with empty.
  // virtual void VisitCustomWeakPointers(i::HeapObject host, i::ObjectSlot start,
  //                                      i::ObjectSlot end) override final {
  //   // VisitPointers(host, start, end);
  //   if (!(host.IsWeakCell() || host.IsJSWeakRef())) {
  //     VisitPointers(host, start, end);
  //     return;
  //   }
  //   // DCHECK(host.IsWeakCell() || host.IsJSWeakRef());
  //   // for (auto p = start; p < end; ++p) {
  //   //   weak_objects_->weak_references.Push(task_id_, std::make_pair(host, i::HeapObjectSlot(p.address())));
  //   // }
  // }
 private:
  template<class TSlot>
  V8_INLINE void ProcessEdge(i::HeapObject host, TSlot slot) {
    DCHECK(mmtk::is_live(host));
    DCHECK(!mmtk::get_forwarded_ref(host));
    i::HeapObject object;
    if ((*slot).GetHeapObjectIfStrong(&object)) {
      PushEdge((void*) slot.address());
    } else if (TSlot::kCanBeWeak && (*slot).GetHeapObjectIfWeak(&object)) {
      // if (auto f = mmtk::get_forwarded_ref(object)) {
      //   i::HeapObjectSlot s = i::HeapObjectSlot(slot.address());
      //   s.StoreHeapObject(*f);
      // } else  {
        // PushEdge((void*) slot.address());
        // // ProcessWeakHeapObject(host, THeapObjectSlot(slot), heap_object);
        i::HeapObjectSlot s = i::HeapObjectSlot(slot.address());
        weak_objects_->weak_references.Push(task_id_, std::make_pair(host, s));
      // }
    }
  }

  V8_INLINE void PushEdge(void* edge) {
    buffer_[cursor_++] = edge;
    if (cursor_ >= cap_) flush();
  }

  void flush() {
    if (cursor_ > 0) {
      NewBuffer buf = process_edges_(buffer_, cursor_, cap_);
      buffer_ = buf.buf;
      cap_ = buf.cap;
      cursor_ = 0;
    }
  }

  v8::internal::Heap* heap_;
  ProcessEdgesFn process_edges_;
  TraceFieldFn trace_field_;
  void* context_;
  void** buffer_ = nullptr;
  size_t cap_ = 0;
  size_t cursor_ = 0;
  i::WeakObjects* weak_objects_ = mmtk::global_weakref_processor->weak_objects();
  int task_id_;
};



class MMTkHeapVerifier: public i::RootVisitor, public i::ObjectVisitor {
 public:
  explicit MMTkHeapVerifier() {
  }

  virtual ~MMTkHeapVerifier() {}

  virtual void VisitRootPointer(i::Root root, const char* description, i::FullObjectSlot p) override final {
    VerifyRootEdge(root, p);
  }

  virtual void VisitRootPointers(i::Root root, const char* description, i::FullObjectSlot start, i::FullObjectSlot end) override final {
    for (auto p = start; p < end; ++p) VerifyRootEdge(root, p);
  }

  virtual void VisitRootPointers(i::Root root, const char* description, i::OffHeapObjectSlot start, i::OffHeapObjectSlot end) override final {
    for (auto p = start; p < end; ++p) VerifyRootEdge(root, p);
  }

  virtual void VisitPointers(i::HeapObject host, i::ObjectSlot start, i::ObjectSlot end) override final {
    for (auto p = start; p < end; ++p) VerifyEdge(host, p);
  }

  virtual void VisitPointers(i::HeapObject host, i::MaybeObjectSlot start, i::MaybeObjectSlot end) override final {
    for (auto p = start; p < end; ++p) VerifyEdge(host, p);
  }

  virtual void VisitCodeTarget(i::Code host, i::RelocInfo* rinfo) override final {
    auto target = i::Code::GetCodeFromTargetAddress(rinfo->target_address());
    VerifyHeapObject(host, 0, target);
  }

  virtual void VisitEmbeddedPointer(i::Code host, i::RelocInfo* rinfo) override final {
    VerifyHeapObject(host, 0, rinfo->target_object());
  }

  virtual void VisitMapPointer(i::HeapObject host) override final {
    VerifyEdge(host, host.map_slot());
  }

  void TransitiveClosure() {
    while (mark_stack_.size() != 0) {
      auto o = mark_stack_.back();
      mark_stack_.pop_back();
      o.Iterate(this);
    }
  }

  static void Verify(i::Heap* heap) {
    MMTkHeapVerifier visitor;
    heap->IterateRoots(&visitor, {});
    visitor.TransitiveClosure();
  }

 private:
  V8_INLINE void VerifyRootEdge(i::Root root, i::FullObjectSlot p) {
    VerifyEdge(i::HeapObject(), p);
  }

  template<class T>
  V8_INLINE void VerifyEdge(i::HeapObject host, T p) {
    i::HeapObject object;
    if ((*p).GetHeapObject(&object)) {
      VerifyHeapObject(host, p.address(), object);
    }
  }

  V8_INLINE void VerifyHeapObject(i::HeapObject host, i::Address edge, i::HeapObject o) {
    if (marked_objects_.find(o.ptr()) == marked_objects_.end()) {
      marked_objects_.insert(o.ptr());
      if (!tph::Heap::IsValidHeapObject(o)) {
        printf("Dead edge %p.%p -> %p\n", (void*) host.ptr(), (void*) edge, (void*) o.ptr());
      }
      CHECK(tph::Heap::IsValidHeapObject(o));
      mark_stack_.push_back(o);
    }
  }

  std::unordered_set<i::Address> marked_objects_;
  std::vector<i::HeapObject> mark_stack_;
};

} // namespace mmtk

#endif // MMTK_VISITORS_H