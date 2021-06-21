#ifndef MMTK_H
#define MMTK_H

#include "src/heap/third-party/heap-api.h"
#include "src/base/address-region.h"
#include "src/heap/heap.h"
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


namespace v8 {
namespace internal {

class Isolate;

namespace third_party_heap {

class Heap;

class TPHData {
  Heap*  v8_tph_;
  MMTk_Heap mmtk_heap_;
  v8::internal::Isolate* isolate_;
  MMTk_Heap_Archive tph_archive_;

 public:
  Heap* v8_tph() { return v8_tph_; }
  MMTk_Heap mmtk_heap() { return mmtk_heap_; }
  v8::internal::Isolate * isolate() { return isolate_; }
  MMTk_Heap_Archive archive() { return tph_archive_; }

  TPHData(Heap* v8_tph, MMTk_Heap mmtk_heap, Isolate* isolate, MMTk_Heap_Archive tph_archive):
    v8_tph_(v8_tph), mmtk_heap_(mmtk_heap), isolate_(isolate), tph_archive_(tph_archive) {}
};

class BumpAllocator {
 public:
  TPHData* tph_data;
  uintptr_t cursor;
  uintptr_t limit;
  void* space;
};

} // third_party_heap
} // internal
} // v8
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

extern bool is_live_object(void* ref);
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
extern void     tph_archive_insert(void* arch, void* object, void* isolate, uint8_t space);
extern void     tph_archive_remove(void* arch, void* object);
extern void     tph_archive_iter_reset(void* arch);
extern void*    tph_archive_iter_next(void* arch);
extern void*    tph_archive_inner_to_obj(void* arch, void* inner_ptr);
extern uint8_t  tph_archive_obj_to_space(void* arch, void* obj_ptr);
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

#endif // MMTK_H