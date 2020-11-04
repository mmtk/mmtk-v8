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

/**
 * Allocation
 */
extern MMTk_Mutator bind_mutator(void *mmtk, void *tls);

extern void* alloc(MMTk_Mutator mutator, size_t size,
    size_t align, size_t offset, int allocator);

extern void* alloc_slow(MMTk_Mutator mutator, size_t size,
    size_t align, size_t offset, int allocator);

extern void post_alloc(MMTk_Mutator mutator, void* refer, void* type_refer,
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
extern void*    tph_archive_obj_to_isolate(void* arch, void* obj_ptr);
extern uint8_t  tph_archive_obj_to_space(void* arch, void* obj_ptr);

typedef struct {
    void (*stop_all_mutators) (void *tls);
    void (*resume_mutators) (void *tls);
    void (*spawn_collector_thread) (void *tls, void *ctx);
    void (*block_for_gc) ();
    void* (*active_collector) (void* tls);
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

#ifdef __cplusplus
}
#endif

#endif // MMTK_H