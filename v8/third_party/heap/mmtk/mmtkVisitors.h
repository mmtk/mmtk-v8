#ifndef MMTK_VISITORS_H
#define MMTK_VISITORS_H

#include "src/base/logging.h"
#include "mmtkUpcalls.h"
#include <mutex>
#include <condition_variable>
#include "src/objects/slots-inl.h"
#include "src/heap/safepoint.h"
#include "src/codegen/reloc-info.h"
#include "src/objects/code.h"
#include "src/objects/code-inl.h"
#include "src/objects/map-inl.h"
#include "src/codegen/assembler-inl.h"
#include <unordered_set>


namespace v8 {
namespace internal {
namespace third_party_heap {

class MMTkRootVisitor: public RootVisitor {
 public:
  explicit MMTkRootVisitor(v8::internal::Heap* heap, TraceRootFn trace_root, void* context): heap_(heap), trace_root_(trace_root), context_(context) {
    USE(heap_);
  }

  virtual void VisitRootPointer(Root root, const char* description, FullObjectSlot p) override final {
    ProcessRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, FullObjectSlot start, FullObjectSlot end) override final {
    for (FullObjectSlot p = start; p < end; ++p) ProcessRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, OffHeapObjectSlot start, OffHeapObjectSlot end) override final {
    for (OffHeapObjectSlot p = start; p < end; ++p) ProcessRootEdge(root, p);
  }

 private:
  V8_INLINE void ProcessRootEdge(Root root, FullObjectSlot slot) {
    HeapObject object;
    if ((*slot).GetHeapObject(&object)) {
      trace_root_((void*) slot.address(), context_);
    }
  }

  v8::internal::Heap* heap_;
  TraceRootFn trace_root_;
  void* context_;
};

class MMTkEdgeVisitor: public ObjectVisitor {
 public:
  explicit MMTkEdgeVisitor(v8::internal::Heap* heap, ProcessEdgesFn process_edges, TraceFieldFn trace_field, void* context): heap_(heap), process_edges_(process_edges), trace_field_(trace_field), context_(context) {
    USE(heap_);
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

  virtual void VisitPointers(HeapObject host, ObjectSlot start, ObjectSlot end) override final {
    for (ObjectSlot p = start; p < end; ++p) ProcessEdge(p);
  }

  virtual void VisitPointers(HeapObject host, MaybeObjectSlot start, MaybeObjectSlot end) override final {
    for (MaybeObjectSlot p = start; p < end; ++p) ProcessEdge(p);
  }

  virtual void VisitCodeTarget(Code host, RelocInfo* rinfo) override final {
    Code target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    AddNonMovingEdge(target);
  }

  virtual void VisitEmbeddedPointer(Code host, RelocInfo* rinfo) override final {
    auto o = rinfo->target_object();
    trace_field_((void*) &o, context_);
    if (o != rinfo->target_object()) {
      rinfo->set_target_object(heap_, o);
    }
  }

  virtual void VisitMapPointer(HeapObject host) override final {
    ProcessEdge(host.map_slot());
  }

 private:
  V8_INLINE void ProcessRootEdge(Root root, FullObjectSlot p) {
    ProcessEdge(p);
  }

  template<class T>
  V8_INLINE void ProcessEdge(T p) {
    HeapObject object;
    if ((*p).GetHeapObject(&object)) {
      PushEdge((void*) p.address());
    }
  }

  V8_INLINE void AddNonMovingEdge(HeapObject o) {
    DCHECK(!mmtk_is_movable(o));
    // TODO: Don't malloc
    HeapObject* edge = new HeapObject();
    *edge = o;
    PushEdge((void*) edge);
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
};



class MMTkHeapVerifier: public RootVisitor, public ObjectVisitor {
 public:
  explicit MMTkHeapVerifier() {
  }

  virtual ~MMTkHeapVerifier() {}

  virtual void VisitRootPointer(Root root, const char* description, FullObjectSlot p) override final {
    VerifyRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, FullObjectSlot start, FullObjectSlot end) override final {
    for (FullObjectSlot p = start; p < end; ++p) VerifyRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, OffHeapObjectSlot start, OffHeapObjectSlot end) override final {
    for (OffHeapObjectSlot p = start; p < end; ++p) VerifyRootEdge(root, p);
  }

  virtual void VisitPointers(HeapObject host, ObjectSlot start, ObjectSlot end) override final {
    for (ObjectSlot p = start; p < end; ++p) VerifyEdge(host, p);
  }

  virtual void VisitPointers(HeapObject host, MaybeObjectSlot start, MaybeObjectSlot end) override final {
    for (MaybeObjectSlot p = start; p < end; ++p) VerifyEdge(host, p);
  }

  virtual void VisitCodeTarget(Code host, RelocInfo* rinfo) override final {
    Code target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    VerifyHeapObject(host, 0, target);
  }

  virtual void VisitEmbeddedPointer(Code host, RelocInfo* rinfo) override final {
    VerifyHeapObject(host, 0, rinfo->target_object());
  }

  virtual void VisitMapPointer(HeapObject host) override final {
    VerifyEdge(host, host.map_slot());
  }

  void TransitiveClosure() {
    while (mark_stack_.size() != 0) {
      auto o = mark_stack_.back();
      mark_stack_.pop_back();
      o.Iterate(this);
    }
  }

  static void Verify(v8::internal::Heap* heap) {
    MMTkHeapVerifier visitor;
    heap->IterateRoots(&visitor, {});
    visitor.TransitiveClosure();
  }

 private:
  V8_INLINE void VerifyRootEdge(Root root, FullObjectSlot p) {
    VerifyEdge(HeapObject(), p);
  }

  template<class T>
  V8_INLINE void VerifyEdge(HeapObject host, T p) {
    HeapObject object;
    if ((*p).GetHeapObject(&object)) {
      VerifyHeapObject(host, p.address(), object);
    }
  }

  V8_INLINE void VerifyHeapObject(HeapObject host, Address edge, HeapObject o) {
    if (marked_objects_.find(o.ptr()) == marked_objects_.end()) {
      marked_objects_.insert(o.ptr());
      if (!third_party_heap::Heap::IsValidHeapObject(o)) {
        printf("Dead edge %p.%p -> %p\n", (void*) host.ptr(), (void*) edge, (void*) o.ptr());
      }
      CHECK(third_party_heap::Heap::IsValidHeapObject(o));
      mark_stack_.push_back(o);
    }
  }

  std::unordered_set<Address> marked_objects_;
  std::vector<HeapObject> mark_stack_;
};


class MMTkWeakObjectRetainer: public WeakObjectRetainer {
 public:
  virtual Object RetainAs(Object object) override final {
    // fprintf(stderr, "RetainAs %p\n", (void*) object.ptr());
    if (object == Object()) return object;
    HeapObject heap_object = HeapObject::cast(object);
    if (third_party_heap::Heap::IsValidHeapObject(heap_object)) {
      auto f = mmtk_get_forwarded_object(heap_object);
      if (f != nullptr) {
        // fprintf(stderr, "%p -> %p\n", (void*) object.ptr(), (void*) f);
        return Object((Address) f);
      }
      // fprintf(stderr, "%p is dead 1 \n", (void*) object.ptr());
      return object;
    } else {
      // fprintf(stderr, "%p is dead 2 \n", (void*) object.ptr());
      return Object();
    }
  }
};




}
}
}


#endif // MMTK_VISITORS_H