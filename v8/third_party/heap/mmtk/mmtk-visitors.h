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

  V8_INLINE void VisitSharedFunctionInfo(i::Map map, i::SharedFunctionInfo shared_info) {
    // If the SharedFunctionInfo has old bytecode, mark it as flushable,
    // otherwise visit the function data field strongly.
    if (shared_info.ShouldFlushBytecode(i::Heap::GetBytecodeFlushMode(heap_->isolate()))) {
      weak_objects_->bytecode_flushing_candidates.Push(task_id_, shared_info);
    }
  }

  V8_INLINE void VisitJSFunction(i::Map map, i::JSFunction object) {
    if (v8::internal::Heap::GetBytecodeFlushMode(heap_->isolate()) != i::BytecodeFlushMode::kDoNotFlushBytecode && object.NeedsResetDueToFlushedBytecode()) {
      weak_objects_->flushed_js_functions.Push(task_id_, object);
    }
  }

  V8_INLINE void VisitTransitionArray(i::Map map, i::TransitionArray array) {
    weak_objects_->transition_arrays.Push(task_id_, array);
  }

  void VisitEphemeronHashTable(i::Map map, i::EphemeronHashTable table) {
    weak_objects_->ephemeron_hash_tables.Push(task_id_, table);
    for (i::InternalIndex i : table.IterateEntries()) {
      // ObjectSlot key_slot =
      //     table.RawFieldOfElementAt(EphemeronHashTable::EntryToIndex(i));
      auto key = i::HeapObject::cast(table.KeyAt(i));

      // concrete_visitor()->SynchronizePageAccess(key);
      // concrete_visitor()->RecordSlot(table, key_slot, key);

      // ObjectSlot value_slot = table.RawFieldOfElementAt(EphemeronHashTable::EntryToValueIndex(i));

      // if (concrete_visitor()->marking_state()->IsBlackOrGrey(key)) {
        // VisitPointer(table, value_slot);
      // } else {
        auto value_obj = table.ValueAt(i);
        if (value_obj.IsHeapObject()) {
          auto value = i::HeapObject::cast(value_obj);
          // concrete_visitor()->SynchronizePageAccess(value);
          // concrete_visitor()->RecordSlot(table, value_slot, value);
          // Revisit ephemerons with both key and value unreachable at end
          // of concurrent marking cycle.
          if (!mmtk::is_live(value)) {
            weak_objects_->discovered_ephemerons.Push(task_id_, i::Ephemeron{key, value});
          }
        }
      // }
    }
  }

  void VisitJSWeakRef(i::Map map, i::JSWeakRef weak_ref) {
    if (weak_ref.target().IsHeapObject()) {
      weak_objects_->js_weak_refs.Push(task_id_, weak_ref);
    }
  }

  virtual void VisitPointers(i::HeapObject host, i::ObjectSlot start, i::ObjectSlot end) override final {
    for (auto p = start; p < end; ++p) ProcessEdge2(host, p);
  }

  virtual void VisitPointers(i::HeapObject host, i::MaybeObjectSlot start, i::MaybeObjectSlot end) override final {
    for (auto p = start; p < end; ++p) ProcessEdge2(host, p);
  }

  virtual void VisitCodeTarget(i::Code host, i::RelocInfo* rinfo) override final {
    auto target = i::Code::GetCodeFromTargetAddress(rinfo->target_address());
    DCHECK(!mmtk_is_movable(target));
    trace_field_((void*) &target, context_);
    DCHECK_EQ(target, i::Code::GetCodeFromTargetAddress(rinfo->target_address()));
  }

  virtual void VisitEmbeddedPointer(i::Code host, i::RelocInfo* rinfo) override final {
    auto o = rinfo->target_object();
    trace_field_((void*) &o, context_);
    if (o != rinfo->target_object()) {
      rinfo->set_target_object(heap_, o);
    }
  }

  virtual void VisitMapPointer(i::HeapObject host) override final {
    ProcessEdge(host.map_slot());
  }

 private:
  V8_INLINE void ProcessRootEdge(i::Root root, i::FullObjectSlot p) {
    ProcessEdge(p);
  }

  template<class T>
  V8_INLINE void ProcessEdge(T p) {
    i::HeapObject object;
    if ((*p).GetHeapObject(&object)) {
      PushEdge((void*) p.address());
    }
  }

  template<class TSlot>
  V8_INLINE void ProcessEdge2(i::HeapObject host, TSlot slot) {
    i::HeapObject object;
    if ((*slot).GetHeapObjectIfStrong(&object)) {
      PushEdge((void*) slot.address());
    } else if (TSlot::kCanBeWeak && (*slot).GetHeapObjectIfWeak(&object)) {
      // ProcessWeakHeapObject(host, THeapObjectSlot(slot), heap_object);
      i::HeapObjectSlot s = i::HeapObjectSlot(slot.address());
      weak_objects_->weak_references.Push(task_id_, std::make_pair(host, s));
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