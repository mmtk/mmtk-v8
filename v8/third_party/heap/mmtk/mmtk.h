#ifndef MMTK_H
#define MMTK_H

#include "src/heap/third-party/heap-api.h"
#include "src/base/address-region.h"
#include "src/heap/heap.h"
#include "src/objects/objects.h"
#include "src/objects/objects-inl.h"
#include "src/execution/isolate.h"
#include <stdbool.h>
#include <stddef.h>
#include <cassert>
#include <set>
#include <iterator>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* MMTk_Mutator;
typedef void* MMTk_TraceLocal;
typedef void* MMTk_Heap;
typedef void* MMTk_Heap_Archive;

/**
 * Allocation
 */
extern MMTk_Mutator bind_mutator(void *mmtk, void *tls);

extern void* alloc(MMTk_Mutator mutator, size_t size,
    size_t align, size_t offset, int allocator);

extern void* alloc_slow(MMTk_Mutator mutator, size_t size,
    size_t align, size_t offset, int allocator);

extern void post_alloc(MMTk_Mutator mutator, void* refer,
    int bytes, int allocator);

extern size_t mmtk_object_is_live(void* ref);
extern bool is_mapped_object(void* ref);
extern bool is_mapped_address(void* addr);
extern void modify_check(void *mmtk, void* ref);
extern bool is_in_read_only_space(void* addr);
extern bool is_in_code_space(void* addr);

/**
 * Tracing
 */

/**
 * Misc
 */
extern bool will_never_move(void* object);
extern bool process(void* mmtk, char* name, char* value);
extern void scan_region(void *mmtk);
extern void handle_user_collection_request(void *mmtk, void *tls);

extern void start_control_collector(void *tls);
extern void start_worker(void *tls, void* worker);

/**
 * V8 specific
 */
extern MMTk_Heap    v8_new_heap(void* calls, size_t heap_size);
extern void*    tph_archive_new();
extern void     tph_archive_delete(void* arch);
extern void     tph_archive_insert(void* arch, void* object, void* isolate);
extern void     tph_archive_remove(void* arch, void* object);
extern void     tph_archive_iter_reset(void* arch);
extern void*    tph_archive_iter_next(void* arch);
extern void*    tph_archive_inner_to_obj(void* arch, void* inner_ptr);
extern int mmtk_in_space(void* mmtk, void* object, size_t space);

extern void release_buffer(void** buffer, size_t len, size_t cap);

typedef struct {
  void** buf;
  size_t cap;
} NewBuffer;

typedef NewBuffer (*ProcessEdgesFn)(void** buf, size_t len, size_t cap);
typedef void* (*TraceRootFn)(void* slot, void* ctx);
typedef void* (*TraceFieldFn)(void* slot, void* ctx);

typedef struct {
    void (*stop_all_mutators) (void *tls);
    void (*resume_mutators) (void *tls);
    void (*spawn_collector_thread) (void *tls, void *ctx);
    void (*block_for_gc) ();
    void* (*get_next_mutator) ();
    void (*reset_mutator_iterator) ();
    void (*compute_static_roots) (void* trace, void* tls);
    void (*compute_global_roots) (void* trace, void* tls);
    void (*compute_thread_roots) (void* trace, void* tls);
    void (*scan_object) (void* trace, void* object, void* tls);
    void (*dump_object) (void* object);
    size_t (*get_object_size) (void* object);
    void* (*get_mmtk_mutator) (void* tls);
    bool (*is_mutator) (void* tls);
    void (*scan_roots) (TraceRootFn process_edges, void* context, int task_id);
    void (*scan_objects) (void** objects, size_t count, ProcessEdgesFn process_edges, TraceFieldFn trace_field, void* context, int task_id);
    void (*process_weak_refs) (TraceRootFn process_edges, void* context);
    void (*on_move_event) (void* from, void* to, size_t size);
    void (*process_ephemerons) (TraceRootFn process_edges, void* context, int task_id);
} V8_Upcalls;

/**
 * VM Accounting
 */
extern size_t free_bytes();
extern size_t total_bytes();

/**
 * Reference Processing
 */
extern void add_weak_candidate(void* ref, void* referent);
extern void add_soft_candidate(void* ref, void* referent);
extern void add_phantom_candidate(void* ref, void* referent);

extern void harness_begin(void* ref, void *tls);
extern void harness_end(void* ref);

extern int mmtk_is_movable(v8::internal::Object o);
extern void* mmtk_get_forwarded_object(v8::internal::Object o);

#ifdef __cplusplus
}
#endif

// Helpers

namespace v8 {
namespace internal {
namespace third_party_heap {

namespace i = v8::internal;

class Impl {
 public:
  template <class T>
  static v8::internal::Object VisitWeakList(v8::internal::Heap* heap, v8::internal::Object list, v8::internal::WeakObjectRetainer* retainer);

  V8_INLINE static void ProcessAllWeakReferences(v8::internal::Heap* heap, v8::internal::WeakObjectRetainer* retainer) {
    heap->set_native_contexts_list(VisitWeakList<Context>(heap, heap->native_contexts_list(), retainer));
    heap->set_allocation_sites_list(VisitWeakList<AllocationSite>(heap, heap->allocation_sites_list(), retainer));
    auto head = VisitWeakList<JSFinalizationRegistry>(heap, heap->dirty_js_finalization_registries_list(), retainer);
    heap->set_dirty_js_finalization_registries_list(head);
    if (head.IsUndefined(heap->isolate())) {
      heap->set_dirty_js_finalization_registries_list_tail(head);
    }
  }

  V8_INLINE static void UpdateExternalStringTable(v8::internal::Heap* heap, RootVisitor* external_visitor) {
    heap->external_string_table_.IterateAll(external_visitor);
    heap->external_string_table_.CleanUpAll();
  }

  V8_INLINE static void EphemeronHashTable_RemoveEntry(EphemeronHashTable& table, InternalIndex entry) {
    table.RemoveEntry(entry);
  }

  V8_INLINE static void TransitionArray_SetNumberOfTransitions(TransitionArray& array, int number_of_transitions) {
    array.SetNumberOfTransitions(number_of_transitions);
  }

  V8_INLINE static int TransitionArray_Capacity(TransitionArray& array) {
    return array.Capacity();
  }

  V8_INLINE static Map TransitionsAccessor_GetTargetFromRaw(MaybeObject raw) {
    return TransitionsAccessor::GetTargetFromRaw(raw);
  }

  V8_INLINE static bool TransitionsAccessor_HasSimpleTransitionTo(Isolate* isolate, Map parent, Map target, DisallowGarbageCollection* no_gc) {
    return TransitionsAccessor(isolate, parent, no_gc).HasSimpleTransitionTo(target);
  }

  V8_INLINE static void FlushNumberStringCache(v8::internal::Heap* heap) {
    heap->FlushNumberStringCache();
  }

  V8_INLINE static Heap* get_tp_heap(v8::internal::Heap* heap) {
    return heap->tp_heap_.get();
  }

  V8_INLINE Impl(MMTk_Heap mmtk_heap, Isolate* isolate, MMTk_Heap_Archive tph_archive)
    : mmtk_heap_(mmtk_heap), isolate_(isolate), tph_archive_(tph_archive) {}

  MMTk_Heap mmtk_heap_;
  v8::internal::Isolate* isolate_;
  MMTk_Heap_Archive tph_archive_;
  std::vector<MMTk_Mutator> mutators_ {};
};

// TODO(wenyuzhao): We only support one heap at the moment.
extern v8::internal::Heap* v8_heap;

}
}
}

namespace mmtk {

namespace i = v8::internal;
namespace base = v8::base;
namespace tph = v8::internal::third_party_heap;

// TODO(wenyuzhao): Using of thread_local is incorrect for multiple isolates.
extern thread_local MMTk_Mutator mutator;

enum class MMTkAllocationSemantic: uint8_t {
  kDefault = 0,
  kImmortal = 1,
  kLos = 2,
  kCode = 3,
  kReadOnly = 4,
  kLargeCode = 5,
};

V8_INLINE MMTkAllocationSemantic GetAllocationSemanticForV8Space(i::AllocationSpace space) {
  switch (space) {
    case i::RO_SPACE:      return mmtk::MMTkAllocationSemantic::kReadOnly;
    case i::OLD_SPACE:     return mmtk::MMTkAllocationSemantic::kDefault;
    case i::CODE_SPACE:    return mmtk::MMTkAllocationSemantic::kCode;
    case i::MAP_SPACE:     return mmtk::MMTkAllocationSemantic::kImmortal;
    case i::LO_SPACE:      return mmtk::MMTkAllocationSemantic::kLos;
    case i::CODE_LO_SPACE: return mmtk::MMTkAllocationSemantic::kLargeCode;
    default:            UNREACHABLE();
  }
}

V8_INLINE MMTkAllocationSemantic GetAllocationSemanticForV8AllocationType(i::AllocationType type, bool large) {
  if (type == i::AllocationType::kCode) {
    return large ? MMTkAllocationSemantic::kLargeCode : MMTkAllocationSemantic::kCode;
  } else if (type == i::AllocationType::kReadOnly) {
    return MMTkAllocationSemantic::kReadOnly;
  } else if (type == i::AllocationType::kMap) {
    return MMTkAllocationSemantic::kImmortal;
  } else {
    return large ? MMTkAllocationSemantic::kLos : MMTkAllocationSemantic::kDefault;
  }
}

V8_INLINE bool is_live(i::HeapObject o) {
  return mmtk_object_is_live(reinterpret_cast<void*>(o.address())) != 0;
}

V8_INLINE i::MaybeObject to_weakref(i::HeapObject o) {
  DCHECK(o.IsStrong());
  return i::MaybeObject::MakeWeak(i::MaybeObject::FromObject(o));
}

V8_INLINE base::Optional<i::HeapObject> get_forwarded_ref(i::HeapObject o) {
  auto f = mmtk_get_forwarded_object(o);
  if (f != nullptr) {
    auto x = i::HeapObject::cast(i::Object((i::Address) f));
    return x;
  }
  return base::nullopt;
}

V8_INLINE std::vector<MMTk_Mutator>& get_mmtk_mutators(i::Heap* heap) {
  return tph::Impl::get_tp_heap(heap)->impl()->mutators_;
}

V8_INLINE MMTk_Heap get_mmtk_instance(i::Heap* heap) {
  return tph::Impl::get_tp_heap(heap)->impl()->mmtk_heap_;
}

V8_INLINE MMTk_Heap get_mmtk_instance(tph::Heap* tp_heap) {
  return tp_heap->impl()->mmtk_heap_;
}

V8_INLINE MMTk_Heap get_object_archive(tph::Heap* tp_heap) {
  return tp_heap->impl()->tph_archive_;
}

V8_INLINE i::Isolate* get_isolate(tph::Heap* tp_heap) {
  return tp_heap->impl()->isolate_;
}

}

#endif // MMTK_H