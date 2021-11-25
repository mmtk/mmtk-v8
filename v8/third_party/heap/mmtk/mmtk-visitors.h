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
      : heap_(heap), process_edges_(process_edges), task_id_(task_id) {
    trace_field_ = [=](i::HeapObject o) -> base::Optional<i::HeapObject> {
      auto old = o;
      trace_field((void*) &o, context);
      return o != old ? base::make_optional(o) : base::nullopt;
    };
    USE(heap_);
    DCHECK(1 <= task_id && task_id <= 7);
    NewBuffer buf = process_edges(NULL, 0, 0);
    buffer_ = buf.buf;
    cap_ = buf.cap;
    USE(task_id_);
  }

  virtual ~MMTkEdgeVisitor() {
    if (cursor_ > 0) flush();
    if (buffer_ != NULL) {
      mmtk_release_buffer(buffer_, cursor_, cap_);
    }
  }

  V8_INLINE void VisitDescriptorArray(i::Map map, i::DescriptorArray array) {
    VisitMapPointer(array);
    VisitPointers(array, array.GetFirstPointerSlot(), array.GetDescriptorSlot(0));
    VisitDescriptors(array, array.number_of_descriptors());
  }

  void VisitDescriptors(i::DescriptorArray descriptor_array, int number_of_own_descriptors) {
    int16_t new_marked = static_cast<int16_t>(number_of_own_descriptors);
    // Note: Always trace all the element in descriptor_arrays.
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
      DCHECK_EQ(maybe_descriptors, i::Smi::uninitialized_deserialization_value());
      return 0;
    }
    auto descriptors = i::DescriptorArray::cast(maybe_descriptors);
    // Don't do any special processing of strong descriptor arrays, let them get
    // marked through the normal visitor mechanism.
    if (descriptors.IsStrongDescriptorArray()) {
      return 0;
    }
    // Mark weak DescriptorArray
    if (auto forwarded = trace_field_(descriptors)) {
      descriptors = i::DescriptorArray::cast(*forwarded);
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
    auto forwarded = trace_field_(target);
    DCHECK(!forwarded);
    USE(forwarded);
  }

  virtual void VisitEmbeddedPointer(i::Code host, i::RelocInfo* rinfo) override final {
    auto o = rinfo->target_object();
    if (auto forwarded = mmtk::get_forwarded_ref(o)) {
      rinfo->set_target_object(heap_, *forwarded);
    } else if (host.IsWeakObject(o) && WEAKREF_PROCESSING_BOOL) {
        // TODO: Enable weak ref processing
        UNIMPLEMENTED();
    } else {
      if (auto forwarded = trace_field_(o)) {
        rinfo->set_target_object(heap_, *forwarded);
      }
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
      } else {
        // TODO: Enable weak ref processing
        UNIMPLEMENTED();
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
  int task_id_;
  void** buffer_ = nullptr;
  size_t cap_ = 0;
  size_t cursor_ = 0;
  i::WeakObjects* weak_objects_ = mmtk::global_weakref_processor->weak_objects();
  std::function<base::Optional<i::HeapObject>(i::HeapObject)> trace_field_;
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