#include "src/base/logging.h"
#include "mmtkUpcalls.h"
#include <mutex>
#include <condition_variable>
#include "src/objects/slots-inl.h"
#include "src/heap/safepoint.h"
#include "mmtkVisitors.h"

namespace v8 {
namespace internal {
namespace third_party_heap {

extern v8::internal::Heap* v8_heap;

std::mutex m;
std::condition_variable* cv = new std::condition_variable();
bool gcInProgress = false;
SafepointScope* scope;

static void mmtk_stop_all_mutators(void *tls) {
  fprintf(stderr, "mmtk_stop_all_mutators\n");
  scope = new SafepointScope(v8_heap);
  fprintf(stderr, "mmtk_stop_all_mutators: heap verify start\n");
  MMTkHeapVerifier visitor;
  v8_heap->IterateRoots(&visitor, {});
  fprintf(stderr, "mmtk_stop_all_mutators: heap verify end\n");
}

static void mmtk_resume_mutators(void *tls) {
  fprintf(stderr, "mmtk_resume_mutators: heap verify start\n");
  MMTkHeapVerifier visitor;
  v8_heap->IterateRoots(&visitor, {});
  fprintf(stderr, "mmtk_resume_mutators: heap verify end\n");
  fprintf(stderr, "mmtk_resume_mutators\n");
  delete scope;
  scope = nullptr;
  std::unique_lock<std::mutex> lock(m);
  gcInProgress = false;
  cv->notify_all();
}

static void mmtk_spawn_collector_thread(void* tls, void* ctx) {
    UNIMPLEMENTED();
}


static void mmtk_block_for_gc() {
  gcInProgress = true;
  std::unique_lock<std::mutex> lock(m);
  cv->wait(lock, []{ return !gcInProgress; });
  fprintf(stderr, "mmtk_block_for_gc end\n");
}

static void* mmtk_active_collector(void* tls) {
    UNIMPLEMENTED();
}

static void* mmtk_get_mmtk_mutator(void* tls) {
    UNIMPLEMENTED();
}

static bool mmtk_is_mutator(void* tls) {
    return false;
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
  UNIMPLEMENTED();
}

static void mmtk_scan_roots(ProcessEdgesFn process_edges) {
  MMTkEdgeVisitor root_visitor(process_edges);
  v8_heap->IterateRoots(&root_visitor, {});
}

static void mmtk_scan_objects(void** objects, size_t count, ProcessEdgesFn process_edges) {
  MMTkEdgeVisitor visitor(process_edges);
  for (size_t i = 0; i < count; i++) {
    auto ptr = *(objects + i);
    DCHECK_EQ(((Address) ptr) & 1, 0);
    auto obj = HeapObject::FromAddress(((Address) ptr));
    obj.Iterate(&visitor);
  }
}

V8_Upcalls mmtk_upcalls = {
  mmtk_stop_all_mutators,
  mmtk_resume_mutators,
  mmtk_spawn_collector_thread,
  mmtk_block_for_gc,
  mmtk_active_collector,
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
};

}   // namespace third_party_heap
}  // namespace internal
}  // namespace v8