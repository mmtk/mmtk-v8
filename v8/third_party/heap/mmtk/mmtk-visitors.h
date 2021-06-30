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

#define WEAKREF_PROCESSING

#ifdef WEAKREF_PROCESSING
#define WEAKREF_PROCESSING_BOOL true
#else
#define WEAKREF_PROCESSING_BOOL false
#endif

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
  V8_INLINE void ProcessEdge(i::HeapObject host, i::ObjectSlot slot) {
    auto object = *slot;
    if (!object.IsHeapObject()) return;
    trace_root_((void*) &object, context_);
    *slot = object;
  }

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

#ifdef WEAKREF_PROCESSING
  void VisitEphemeronHashTable(i::Map map, i::EphemeronHashTable table) {
    // if (!concrete_visitor()->ShouldVisit(table)) return 0;
    weak_objects_->ephemeron_hash_tables.Push(task_id_, table);

    for (auto i : table.IterateEntries()) {
      auto key_slot = table.RawFieldOfElementAt(i::EphemeronHashTable::EntryToIndex(i));
      auto key = i::HeapObject::cast(table.KeyAt(i));

      // VisitPointer(table, key_slot);
      // concrete_visitor()->SynchronizePageAccess(key);
      // concrete_visitor()->RecordSlot(table, key_slot, key);

      auto value_slot = table.RawFieldOfElementAt(i::EphemeronHashTable::EntryToValueIndex(i));

      if (is_live(key)) {
        if (auto f = get_forwarded_ref(key)) {
          key_slot.store(*f);
        }
        VisitPointer(table, value_slot);
      } else {
        auto value_obj = table.ValueAt(i);
        if (value_obj.IsHeapObject()) {
          auto value = i::HeapObject::cast(value_obj);
      //     concrete_visitor()->SynchronizePageAccess(value);
      //     concrete_visitor()->RecordSlot(table, value_slot, value);

      //     // Revisit ephemerons with both key and value unreachable at end
      //     // of concurrent marking cycle.
      //     if (concrete_visitor()->marking_state()->IsWhite(value)) {
          weak_objects_->discovered_ephemerons.Push(task_id_, i::Ephemeron{key, value});
      //     }
        }
      }
    }
    // return table.SizeFromMap(map);
  }

  V8_INLINE void VisitWeakCell(i::Map map, i::WeakCell weak_cell) {
    // if (!ShouldVisit(weak_cell)) return;

    int size = i::WeakCell::BodyDescriptor::SizeOf(map, weak_cell);
    this->VisitMapPointer(weak_cell);
    i::WeakCell::BodyDescriptor::IterateBody(map, weak_cell, size, this);
    // i::HeapObject target = weak_cell.relaxed_target();
    // i::HeapObject unregister_token = weak_cell.relaxed_unregister_token();
    // concrete_visitor()->SynchronizePageAccess(target);
    // concrete_visitor()->SynchronizePageAccess(unregister_token);
    // if (concrete_visitor()->marking_state()->IsBlackOrGrey(target) &&
    //     concrete_visitor()->marking_state()->IsBlackOrGrey(unregister_token)) {
    //   // Record the slots inside the WeakCell, since the IterateBody above
    //   // didn't visit it.
    //   ObjectSlot slot = weak_cell.RawField(WeakCell::kTargetOffset);
    //   concrete_visitor()->RecordSlot(weak_cell, slot, target);
    //   slot = weak_cell.RawField(WeakCell::kUnregisterTokenOffset);
    //   concrete_visitor()->RecordSlot(weak_cell, slot, unregister_token);
    // } else {
      // WeakCell points to a potentially dead object or a dead unregister
      // token. We have to process them when we know the liveness of the whole
      // transitive closure.
      weak_objects_->weak_cells.Push(task_id_, weak_cell);
    // }
  }

  V8_INLINE void VisitJSWeakRef(i::Map map, i::JSWeakRef weak_ref) {
    VisitJSObjectSubclass(map, weak_ref);
    // if (size == 0) return 0;
    if (weak_ref.target().IsHeapObject()) {
      // i::HeapObject target = i::HeapObject::cast(weak_ref.target());
      // SynchronizePageAccess(target);
      // if (concrete_visitor()->marking_state()->IsBlackOrGrey(target)) {
      //   // Record the slot inside the JSWeakRef, since the
      //   // VisitJSObjectSubclass above didn't visit it.
      //   ObjectSlot slot = weak_ref.RawField(JSWeakRef::kTargetOffset);
      //   concrete_visitor()->RecordSlot(weak_ref, slot, target);
      // } else {
        // JSWeakRef points to a potentially dead object. We have to process
        // them when we know the liveness of the whole transitive closure.
        weak_objects_->js_weak_refs.Push(task_id_, weak_ref);
      // }
    }
  }

  V8_INLINE void VisitBytecodeArray(i::Map map, i::BytecodeArray object) {
    if (!ShouldVisit(object)) return;
    int size = i::BytecodeArray::BodyDescriptor::SizeOf(map, object);
    this->VisitMapPointer(object);
    i::BytecodeArray::BodyDescriptor::IterateBody(map, object, size, this);
    // if (!is_forced_gc_) {
      object.MakeOlder();
    // }
  }

  V8_INLINE void VisitJSFunction(i::Map map, i::JSFunction object) {
    VisitJSObjectSubclass(map, object);
    // Check if the JSFunction needs reset due to bytecode being flushed.
    if (/*bytecode_flush_mode_ != BytecodeFlushMode::kDoNotFlushBytecode &&*/
        object.NeedsResetDueToFlushedBytecode()) {
      weak_objects_->flushed_js_functions.Push(task_id_, object);
    }
  }

  V8_INLINE void VisitSharedFunctionInfo(i::Map map, i::SharedFunctionInfo shared_info) {
    // if (!ShouldVisit(shared_info)) return;
    int size = i::SharedFunctionInfo::BodyDescriptor::SizeOf(map, shared_info);
    VisitMapPointer(shared_info);
    i::SharedFunctionInfo::BodyDescriptor::IterateBody(map, shared_info, size, this);

    auto data = shared_info.function_data(v8::kAcquireLoad);
    if (data.IsHeapObject() || data.IsWeak()) {
      if (auto f = mmtk::get_forwarded_ref(i::HeapObject::cast(data))) {
        shared_info.set_function_data(*f, v8::kReleaseStore);
      }
      // trace_field_((void*) &data, context_);
      // shared_info.set_function_data(data, v8::kReleaseStore);
    }

    // If the SharedFunctionInfo has old bytecode, mark it as flushable,
    // otherwise visit the function data field strongly.
    if (shared_info.ShouldFlushBytecode(i::Heap::GetBytecodeFlushMode(heap_->isolate()))) {
      weak_objects_->bytecode_flushing_candidates.Push(task_id_, shared_info);
    } else {
      VisitPointer(shared_info, shared_info.RawField(i::SharedFunctionInfo::kFunctionDataOffset));
    }
  }

  template <typename T>
  void VisitJSObjectSubclass(i::Map map, T object) {
    // if (!ShouldVisit(object)) return;
    VisitMapPointer(object);
    int size = T::BodyDescriptor::SizeOf(map, object);
    T::BodyDescriptor::IterateBody(map, object, size, this);
  }

  V8_INLINE void VisitTransitionArray(i::Map map, i::TransitionArray array) {
    // if (!ShouldVisit(array)) return;
    VisitMapPointer(array);
    int size = i::TransitionArray::BodyDescriptor::SizeOf(map, array);
    i::TransitionArray::BodyDescriptor::IterateBody(map, array, size, this);
    weak_objects_->transition_arrays.Push(task_id_, array);
  }

  // Custom weak pointers must be ignored by the GC but not other
  // visitors. They're used for e.g., lists that are recreated after GC. The
  // default implementation treats them as strong pointers. Visitors who want to
  // ignore them must override this function with empty.
  virtual void VisitCustomWeakPointers(i::HeapObject host, i::ObjectSlot start, i::ObjectSlot end) override final {
    // for (auto p = start; p < end; ++p) {
    //   printf("@weak? %p.%p -> %p\n", (void*) host.ptr(), (void*) p.address(), (void*) (*p).ptr());
    //   i::HeapObject object;
    //   if (i::ObjectSlot::kCanBeWeak && (*p).GetHeapObjectIfWeak(&object)) {
    //     // printf("@weak %p.%p -> %p\n", (void*) host.ptr(), (void*) p.address(), (void*) (*p).ptr());
    //     // auto s = i::HeapObjectSlot(p.address());
    //     // weak_objects_->weak_references.Push(task_id_, std::make_pair(host, s));
    //   } else if ((*p).GetHeapObjectIfStrong(&object)) {

    //     printf("@string %p.%p -> %p\n", (void*) host.ptr(), (void*) p.address(), (void*) (*p).ptr());
    //     auto s = i::HeapObjectSlot(p.address());
    //     weak_objects_->weak_references.Push(task_id_, std::make_pair(host, s));
    //   }
    // }
  }

#endif

  V8_INLINE void VisitDescriptorArray(i::Map map, i::DescriptorArray array) {
    VisitMapPointer(array);
    VisitPointers(array, array.GetFirstPointerSlot(), array.GetDescriptorSlot(0));
    VisitDescriptors(array, array.number_of_descriptors());
  }

  void VisitDescriptors(i::DescriptorArray descriptor_array, int number_of_own_descriptors) {
    int16_t new_marked = static_cast<int16_t>(number_of_own_descriptors);
    // int16_t old_marked = descriptor_array.UpdateNumberOfMarkedDescriptors(
    //     heap_->gc_count(), new_marked);
    // if (old_marked < new_marked) {
      VisitPointers(descriptor_array,
          i::MaybeObjectSlot(descriptor_array.GetDescriptorSlot(0)),
          i::MaybeObjectSlot(descriptor_array.GetDescriptorSlot(new_marked)));
    // }
  }

  V8_INLINE void VisitMap(i::Map meta_map, i::Map map) {
    int size = i::Map::BodyDescriptor::SizeOf(meta_map, map);
    size += VisitDescriptorsForMap(map);
    // Mark the pointer fields of the Map. If there is a transitions array, it has
    // been marked already, so it is fine that one of these fields contains a
    // pointer to it.
    i::Map::BodyDescriptor::IterateBody(meta_map, map, size, this);
  }

  V8_INLINE int VisitDescriptorsForMap(i::Map map) {
    if (!map.CanTransition()) return 0;
    // Maps that can transition share their descriptor arrays and require
    // special visiting logic to avoid memory leaks.
    // Since descriptor arrays are potentially shared, ensure that only the
    // descriptors that belong to this map are marked. The first time a
    // non-empty descriptor array is marked, its header is also visited. The
    // slot holding the descriptor array will be implicitly recorded when the
    // pointer fields of this map are visited.
    i::Object maybe_descriptors =
        i::TaggedField<i::Object, i::Map::kInstanceDescriptorsOffset>::Acquire_Load(
            heap_->isolate(), map);
    // If the descriptors are a Smi, then this Map is in the process of being
    // deserialized, and doesn't yet have an initialized descriptor field.
    if (maybe_descriptors.IsSmi()) {
      DCHECK_EQ(maybe_descriptors, i::Deserializer::uninitialized_field_value());
      return 0;
    }
    auto descriptors = i::DescriptorArray::cast(maybe_descriptors);
    // Don't do any special processing of strong descriptor arrays, let them get
    // marked through the normal visitor mechanism.
    if (descriptors.IsStrongDescriptorArray()) {
      return 0;
    }
    // Mark weak DescriptorArray
    auto old_descriptors = descriptors;
    trace_field_((void*) &descriptors, context_);
    if (old_descriptors != descriptors) {
      i::TaggedField<i::Object, i::Map::kInstanceDescriptorsOffset>::Release_Store(map, descriptors);
    }
    auto size = i::DescriptorArray::BodyDescriptor::SizeOf(descriptors.map(), descriptors);
    int number_of_own_descriptors = map.NumberOfOwnDescriptors();
    if (number_of_own_descriptors) {
      // It is possible that the concurrent marker observes the
      // number_of_own_descriptors out of sync with the descriptors. In that
      // case the marking write barrier for the descriptor array will ensure
      // that all required descriptors are marked. The concurrent marker
      // just should avoid crashing in that case. That's why we need the
      // std::min<int>() below.
      VisitDescriptors(descriptors, std::min<int>(number_of_own_descriptors, descriptors.number_of_descriptors()));
    }
    return size;
  }

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
    } else if (host.IsWeakObject(o) && WEAKREF_PROCESSING_BOOL) {
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

 private:
  template<class TSlot>
  V8_INLINE void ProcessEdge(i::HeapObject host, TSlot slot) {
    DCHECK(mmtk::is_live(host));
    DCHECK(!mmtk::get_forwarded_ref(host));
    i::HeapObject object;
    if ((*slot).GetHeapObjectIfStrong(&object)) {
      PushEdge((void*) slot.address());
    } else if (TSlot::kCanBeWeak && (*slot).GetHeapObjectIfWeak(&object)) {
      if (!WEAKREF_PROCESSING_BOOL) {
        PushEdge((void*) slot.address());
      // } else if (auto f = mmtk::get_forwarded_ref(object)) {
      //   i::HeapObjectSlot s = i::HeapObjectSlot(slot.address());
      //   s.StoreHeapObject(*f);
      } else {
        i::HeapObjectSlot s = i::HeapObjectSlot(slot.address());
        weak_objects_->weak_references.Push(task_id_, std::make_pair(host, s));
      }
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
      if (!is_live(o)) {
        printf("Dead edge %p.%p -> %p\n", (void*) host.ptr(), (void*) edge, (void*) o.ptr());
      }
      CHECK(is_live(o));
      if (get_forwarded_ref(o)) {
        printf("Unforwarded edge %p.%p -> %p\n", (void*) host.ptr(), (void*) edge, (void*) o.ptr());
      }
      CHECK(!get_forwarded_ref(o));
      mark_stack_.push_back(o);
    }
  }

  std::unordered_set<i::Address> marked_objects_;
  std::vector<i::HeapObject> mark_stack_;
};

} // namespace mmtk

#endif // MMTK_VISITORS_H