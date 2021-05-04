#ifndef MMTK_VISITORS_H
#define MMTK_VISITORS_H

#include "src/base/logging.h"
#include "mmtkUpcalls.h"
#include <mutex>
#include <condition_variable>
#include "src/objects/slots-inl.h"
#include "src/heap/safepoint.h"
#include "src/codegen/reloc-info.h"
#include "src/objects/code.h"
#include "src/objects/code-inl.h"
#include "src/codegen/assembler-inl.h"


namespace v8 {
namespace internal {
namespace third_party_heap {


class MMTkEdgeVisitor: public RootVisitor, public ObjectVisitor {
 public:
  explicit MMTkEdgeVisitor(ProcessEdgesFn process_edges): process_edges_(process_edges) {
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

  virtual void VisitRootPointer(Root root, const char* description, FullObjectSlot p) override final {
    ProcessRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, FullObjectSlot start, FullObjectSlot end) override final {
    for (FullObjectSlot p = start; p < end; ++p) ProcessRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, OffHeapObjectSlot start, OffHeapObjectSlot end) override final {
    for (OffHeapObjectSlot p = start; p < end; ++p) ProcessRootEdge(root, p);
  }

  virtual void VisitPointers(HeapObject host, ObjectSlot start, ObjectSlot end) override final {
    for (ObjectSlot p = start; p < end; ++p) ProcessEdge(p);
  }

  virtual void VisitPointers(HeapObject host, MaybeObjectSlot start, MaybeObjectSlot end) override final {
    for (MaybeObjectSlot p = start; p < end; ++p) ProcessEdge(p);
  }

  virtual void VisitCodeTarget(Code host, RelocInfo* rinfo) override final {
    // We need to pass the location of the root pointer (i.e. the slot) to MMTk. Fake it for now.
    Code target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    auto p = new HeapObject(target);
    buffer_[cursor_++] = (void*) p;
    if (cursor_ >= cap_) {
      flush();
    }
  }

  virtual void VisitEmbeddedPointer(Code host, RelocInfo* rinfo) override final {
    // We need to pass the location of the root pointer (i.e. the slot) to MMTk. Fake it for now.
    auto p = new HeapObject(rinfo->target_object());
    buffer_[cursor_++] = (void*) p;
    if (cursor_ >= cap_) {
      flush();
    }
  }

  virtual void VisitMapPointer(HeapObject host) override final {
    ProcessEdge(host.map_slot());
  }

 private:
  V8_INLINE void ProcessRootEdge(Root root, FullObjectSlot p) {
    ProcessEdge(p);
  }

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

}
}
}


#endif // MMTK_VISITORS_H