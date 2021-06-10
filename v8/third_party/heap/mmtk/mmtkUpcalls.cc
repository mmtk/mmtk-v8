#include "mmtkUpcalls.h"
#include "src/objects/slots-inl.h"
#include "src/heap/safepoint.h"
#include "src/heap/array-buffer-sweeper.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/execution/frames-inl.h"
#include "src/regexp/regexp.h"
#include "mmtk-visitors.h"
#include "main-thread-sync.h"
#include "log.h"
#include "weak-refs.h"

namespace v8 {
namespace internal {
namespace third_party_heap {

extern v8::internal::Heap* v8_heap;

mmtk::MainThreadSynchronizer* main_thread_synchronizer = new mmtk::MainThreadSynchronizer();

static void mmtk_stop_all_mutators(void *tls) {
  MMTK_LOG("[mmtk_stop_all_mutators] START\n");
  main_thread_synchronizer->RunMainThreadTask([=]() {
    main_thread_synchronizer->EnterSafepoint(v8_heap);
    MMTK_LOG("[mmtk_stop_all_mutators] Verify heap\n");
    MMTkHeapVerifier::Verify(v8_heap);
    MMTK_LOG("[mmtk_stop_all_mutators] Flush cache\n");
    v8_heap->isolate()->descriptor_lookup_cache()->Clear();
    RegExpResultsCache::Clear(v8_heap->string_split_cache());
    RegExpResultsCache::Clear(v8_heap->regexp_multiple_cache());
    // v8_heap->FlushNumberStringCache();
    int len = v8_heap->number_string_cache().length();
    for (int i = 0; i < len; i++) {
      v8_heap->number_string_cache().set_undefined(i);
    }
  });
  MMTK_LOG("[mmtk_stop_all_mutators] END\n");
}

static void mmtk_process_weak_refs() {
  main_thread_synchronizer->RunMainThreadTask([=]() {
    MMTK_LOG("[mmtk_process_weak_refs]\n");
    mmtk::global_weakref_processor->ClearNonLiveReferences();
  });
}

static void mmtk_resume_mutators(void *tls) {
  MMTK_LOG("[mmtk_resume_mutators] START\n");
  main_thread_synchronizer->RunMainThreadTask([=]() {
    MMTK_LOG("[mmtk_resume_mutators] Verify heap\n");
    MMTkHeapVerifier::Verify(v8_heap);
    MMTK_LOG("[mmtk_resume_mutators] Flush cache\n");
    v8_heap->isolate()->inner_pointer_to_code_cache()->Flush();
    // The stub caches are not traversed during GC; clear them to force
    // their lazy re-initialization. This must be done after the
    // GC, because it relies on the new address of certain old space
    // objects (empty string, illegal builtin).
    v8_heap->isolate()->load_stub_cache()->Clear();
    v8_heap->isolate()->store_stub_cache()->Clear();
    // v8_heap->array_buffer_sweeper()->RequestSweepFull();
    // Some code objects were marked for deoptimization during the GC.
    // Deoptimizer::DeoptimizeMarkedCode(v8_heap->isolate());
    main_thread_synchronizer->ExitSafepoint();
  });
  main_thread_synchronizer->WakeUp();
  MMTK_LOG("[mmtk_resume_mutators] END\n");
}

static void mmtk_spawn_collector_thread(void* tls, void* ctx) {
  UNIMPLEMENTED();
}

static void mmtk_block_for_gc() {
  main_thread_synchronizer->Block();
}

static void* mmtk_get_mmtk_mutator(void* tls) {
  UNIMPLEMENTED();
}

extern thread_local bool is_mutator;

static bool mmtk_is_mutator(void* tls) {
  return is_mutator;
}

extern std::vector<BumpAllocator*>* all_mutators;
size_t index = 0;

static void* mmtk_get_next_mutator() {
  if (index >= all_mutators->size()) return nullptr;
  return (*all_mutators)[index++];
}

static void mmtk_reset_mutator_iterator() {
  index = 0;
}


static void mmtk_compute_global_roots(void* trace, void* tls) {
  UNIMPLEMENTED();
}

static void mmtk_compute_static_roots(void* trace, void* tls) {
  UNIMPLEMENTED();
}

static void mmtk_compute_thread_roots(void* trace, void* tls) {
  UNIMPLEMENTED();
}

static void mmtk_scan_object(void* trace, void* object, void* tls) {
  UNIMPLEMENTED();
}

static void mmtk_dump_object(void* object) {
  UNIMPLEMENTED();
}

static size_t mmtk_get_object_size(void* object) {
  auto o = HeapObject::FromAddress((Address) object);
  auto m = o.map();
  return o.SizeFromMap(m);
}

static void mmtk_on_move_event(void* from_address, void* to_address, size_t size) {
  auto from = HeapObject::FromAddress((Address) from_address);
  auto to = HeapObject::FromAddress((Address) to_address);
  v8_heap->OnMoveEvent(to, from, (int) size);
}

static void mmtk_scan_roots(TraceRootFn trace_root, void* context) {
  main_thread_synchronizer->RunMainThreadTask([=]() {
    MMTkRootVisitor root_visitor(v8_heap, trace_root, context);
    v8_heap->IterateRoots(&root_visitor, {});
  });
}

static void mmtk_scan_objects(void** objects, size_t count, ProcessEdgesFn process_edges, TraceFieldFn trace_field, void* context) {
  MMTkEdgeVisitor visitor(v8_heap, process_edges, trace_field,  context);
  for (size_t i = 0; i < count; i++) {
    auto ptr = *(objects + i);
    DCHECK_EQ(((Address) ptr) & 1, 0);
    auto obj = HeapObject::FromAddress(((Address) ptr));
    obj.Iterate(&visitor);
    visitor.Visit(obj);
  }
}

V8_Upcalls mmtk_upcalls = {
  mmtk_stop_all_mutators,
  mmtk_resume_mutators,
  mmtk_spawn_collector_thread,
  mmtk_block_for_gc,
  mmtk_get_next_mutator,
  mmtk_reset_mutator_iterator,
  mmtk_compute_static_roots,
  mmtk_compute_global_roots,
  mmtk_compute_thread_roots,
  mmtk_scan_object,
  mmtk_dump_object,
  mmtk_get_object_size,
  mmtk_get_mmtk_mutator,
  mmtk_is_mutator,
  mmtk_scan_roots,
  mmtk_scan_objects,
  mmtk_process_weak_refs,
  mmtk_on_move_event,
};
}   // namespace third_party_heap
}  // namespace internal
}  // namespace v8