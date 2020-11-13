#include "src/base/logging.h"
#include "mmtkUpcalls.h"


namespace v8 {
namespace internal {
namespace third_party_heap {

static void v8_stop_all_mutators(void *tls) {
    UNIMPLEMENTED();
}

static void v8_resume_mutators(void *tls) {
    UNIMPLEMENTED();
}

static void v8_block_for_gc() {
    UNIMPLEMENTED();
}

static void v8_compute_global_roots(void* trace, void* tls) {
    UNIMPLEMENTED();
}

static void v8_compute_static_roots(void* trace, void* tls) {
    UNIMPLEMENTED();
}

static void v8_compute_thread_roots(void* trace, void* tls) {
    UNIMPLEMENTED();
}

static size_t v8_get_object_size(void* object) {
    return HeapObject(reinterpret_cast<Address>(object)).Size();
}

V8_Upcalls v8_upcalls = {
    v8_stop_all_mutators,
    v8_resume_mutators,
    v8_block_for_gc,
    v8_compute_static_roots,
    v8_compute_global_roots,
    v8_compute_thread_roots,
    v8_is_mutator,
};

}   // namespace third_party_heap
}  // namespace internal
}  // namespace v8 