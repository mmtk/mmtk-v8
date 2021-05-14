#include "src/base/logging.h"
#include "mmtkUpcalls.h"


namespace v8 {
namespace internal {
namespace third_party_heap {

static void mmtk_stop_all_mutators(void *tls) {
    UNIMPLEMENTED();
}

static void mmtk_resume_mutators(void *tls) {
    UNIMPLEMENTED();
}

static void mmtk_spawn_collector_thread(void* tls, void* ctx) {
    UNIMPLEMENTED();
}

static void mmtk_block_for_gc() {
    UNIMPLEMENTED();
}

static void* mmtk_get_mmtk_mutator(void* tls) {
    UNIMPLEMENTED();
}

static bool mmtk_is_mutator(void* tls) {
    return false;
}

static void* mmtk_get_next_mutator() {
    UNIMPLEMENTED();
}

static void mmtk_reset_mutator_iterator() {
    UNIMPLEMENTED();
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
};

}   // namespace third_party_heap
}  // namespace internal
}  // namespace v8 