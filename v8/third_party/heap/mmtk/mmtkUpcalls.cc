#include "mmtkUpcalls.h"

namespace v8 {
namespace internal {
namespace third_party_heap {

static void v8_stop_all_mutators(void* tls) {
  gc_in_progress.store(true);
  v8::internal::Isolate* isolate = tph_mutator_->tph_data->isolate();
  DisallowJavascriptExecution no_js(isolate);
}

static void v8_resume_mutators(void* tls) {
  gc_in_progress.store(false);

  base::MutexGuard guard(&gc_mutex);
  gc_condvar.NotifyAll();

  v8::internal::Isolate* isolate = tph_mutator_->tph_data->isolate();
  AllowJavascriptExecution allow_js(isolate);
}

static void v8_spawn_worker_thread(void* tls, void* ctx) {
  if (ctx == NULL) {
    MMTkControllerThread* new_collector_thread = new MMTkControllerThread();
    CHECK(new_collector_thread->Start());
  } else {
    MMTkWorkerThread* new_worker_thread = new MMTkWorkerThread(ctx);
    CHECK(new_worker_thread->Start());
  }
}

static void v8_block_for_gc() {
  gc_in_progress.store(true);
  base::MutexGuard guard(&gc_mutex);

  while (gc_in_progress.load() == true) {
    gc_condvar.Wait(&mutex);
  }
}

// TODO(remove)
// This function is only used by ActivePlan::worker which is not used anywhere.
// So, it's redundant and should be removed.
static void* v8_active_collector(void* tls) { UNREACHABLE(); }

static void* v8_get_mmtk_mutator(void* tls) {
  DCHECK(tls == reinterpret_cast<void*>(&tph_mutator_));
  return (void*)tph_mutator_;
}

static bool v8_is_mutator(void* tls) {
  return tls == reinterpret_cast<void*>(&tph_mutator_);
}

static void* v8_get_next_mutator() {
  if (mutator_iteration_start) {
    return (void*)tph_mutator_;
  } else {
    return nullptr;
  }
}

static void v8_reset_mutator_iterator() { mutator_iteration_start = true; }

static void v8_scan_thread_roots(ProcessEdgesFn process_edges, void* tls) {
  UNIMPLEMENTED();
}

static void v8_scan_other_roots(ProcessEdgesFn process_edges) {
  UNIMPLEMENTED();
}

static void v8_scan_object(void* trace, void* object, void* tls) {
  UNIMPLEMENTED();
}

static void v8_dump_object(void* object) {
  HeapObject obj = HeapObject::FromAddress(reinterpret_cast<Address>(object));
  obj.HeapObjectShortPrint(&std::cout);
}

static size_t v8_get_object_size(void* object) {
  HeapObject obj = HeapObject::FromAddress(reinterpret_cast<Address>(object));
  Map m = obj.map();
  return obj.SizeFromMap(m);
}

V8_Upcalls v8_upcalls = {
    v8_stop_all_mutators,
    v8_resume_mutators,
    v8_spawn_worker_thread,
    v8_block_for_gc,
    v8_active_collector,
    v8_get_next_mutator,
    v8_reset_mutator_iterator,
    v8_scan_thread_roots,
    v8_scan_other_roots,
    v8_scan_object,
    v8_dump_object,
    v8_get_object_size,
    v8_get_mmtk_mutator,
    v8_is_mutator,
};

}  // namespace third_party_heap
}  // namespace internal
}  // namespace v8