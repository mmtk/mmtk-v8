#include "src/base/logging.h"
#include "mmtkUpcalls.h"
#include <mutex>
#include <condition_variable>
#include "src/objects/slots-inl.h"
#include "src/heap/safepoint.h"

namespace v8 {
namespace internal {
namespace third_party_heap {

extern v8::internal::Heap* v8_heap;

std::mutex m;
std::condition_variable* cv = new std::condition_variable();
bool gcInProgress = false;
SafepointScope* scope;

static void mmtk_stop_all_mutators(void *tls) {
  printf("mmtk_stop_all_mutators\n");
  scope = new SafepointScope(v8_heap);
}

static void mmtk_resume_mutators(void *tls) {
  printf("mmtk_resume_mutators\n");
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

class MMTkRootVisitor: public RootVisitor {
 public:
  explicit MMTkRootVisitor(ProcessEdgesFn process_edges): process_edges_(process_edges) {
    NewBuffer buf = process_edges(NULL, 0, 0);
    buffer_ = buf.buf;
    cap_ = buf.cap;
  }

  virtual ~MMTkRootVisitor() {
    if (cursor_ > 0) flush();
    if (buffer_ != NULL) {
      release_buffer(buffer_, cursor_, cap_);
    }
  }

  void VisitRootPointer(Root root, const char* description, FullObjectSlot p) final {
    // DCHECK(!MapWord::IsPacked(p.Relaxed_Load().ptr()));
    ProcessRootEdge(root, p);
  }

  void VisitRootPointers(Root root, const char* description, FullObjectSlot start, FullObjectSlot end) final {
    for (FullObjectSlot p = start; p < end; ++p) {
      ProcessRootEdge(root, p);
    }
  }

  virtual void VisitRootPointers(Root root, const char* description, OffHeapObjectSlot start, OffHeapObjectSlot end) {
    for (OffHeapObjectSlot p = start; p < end; ++p) {
      ProcessRootEdge(root, p);
    }
  }

 private:
  V8_INLINE void ProcessRootEdge(Root root, FullObjectSlot p) {
    if (!(*p).IsHeapObject()) return;
    buffer_[cursor_++] = (void*) p.address();
    if (cursor_ >= cap_) {
      flush();
    }
  }

  void flush() {
    if (cursor_ > 0) {
      NewBuffer buf = process_edges_(buffer_, cursor_, cap_);
      buffer_ = buf.buf;
      cap_ = buf.cap;
      cursor_ = 0;
    }
  }

  ProcessEdgesFn process_edges_;
  void** buffer_ = nullptr;
  size_t cap_ = 0;
  size_t cursor_ = 0;
};

class MMTkObjectVisitor: public ObjectVisitor {
 public:
  explicit MMTkObjectVisitor(ProcessEdgesFn process_edges): process_edges_(process_edges) {
    NewBuffer buf = process_edges(NULL, 0, 0);
    buffer_ = buf.buf;
    cap_ = buf.cap;
  }

  virtual ~MMTkObjectVisitor() {
    if (cursor_ > 0) flush();
    if (buffer_ != NULL) {
      release_buffer(buffer_, cursor_, cap_);
    }
  }

  void VisitPointers(HeapObject host, ObjectSlot start, ObjectSlot end) override {
    for (ObjectSlot p = start; p < end; ++p) {
      ProcessEdge(p);
    }
  }

  void VisitPointers(HeapObject host, MaybeObjectSlot start, MaybeObjectSlot end) override {
    for (MaybeObjectSlot p = start; p < end; ++p) {
      ProcessEdge(p);
    }
  }

  void VisitCodeTarget(Code host, RelocInfo* rinfo) final {
    // Code target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    // MarkHeapObject(target);
  }

  void VisitEmbeddedPointer(Code host, RelocInfo* rinfo) final {
    // MarkHeapObject(rinfo->target_object());
  }

  void VisitMapPointer(HeapObject object) override {}

 private:
  template<class T>
  V8_INLINE void ProcessEdge(T p) {
    if (!(*p).IsHeapObject()) return;
    buffer_[cursor_++] = (void*) p.address();
    if (cursor_ >= cap_) {
      flush();
    }
  }

  void flush() {
    if (cursor_ > 0) {
      NewBuffer buf = process_edges_(buffer_, cursor_, cap_);
      buffer_ = buf.buf;
      cap_ = buf.cap;
      cursor_ = 0;
    }
  }

  ProcessEdgesFn process_edges_;
  void** buffer_ = nullptr;
  size_t cap_ = 0;
  size_t cursor_ = 0;
};

static void mmtk_scan_roots(ProcessEdgesFn process_edges) {
  MMTkRootVisitor root_visitor(process_edges);
  v8_heap->IterateRoots(&root_visitor, base::EnumSet<SkipRoot>{});
}

static void mmtk_scan_objects(void** objects, size_t count, ProcessEdgesFn process_edges) {
  MMTkObjectVisitor visitor(process_edges);
  for (size_t i = 0; i < count; i++) {
    auto ptr = *(objects + i);
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