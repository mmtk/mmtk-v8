#ifndef MMTK_WEAK_REFS_H
#define MMTK_WEAK_REFS_H

#include "src/heap/heap.h"
#include "src/objects/string-table.h"
#include "src/objects/visitors.h"
#include "src/objects/transitions-inl.h"
#include "src/heap/objects-visiting.h"

// TODO: Enable weak ref processing
// #define WEAKREF_PROCESSING

#ifdef WEAKREF_PROCESSING
#define WEAKREF_PROCESSING_BOOL true
#else
#define WEAKREF_PROCESSING_BOOL false
#endif

namespace v8 {
namespace internal {
namespace third_party_heap {

extern v8::internal::Heap* v8_heap;

}
}
}

namespace mmtk {

class MMTkWeakObjectRetainer: public i::WeakObjectRetainer {
 public:
  virtual i::Object RetainAs(i::Object object) override final {
    if (object == i::Object()) return object;
    auto heap_object = i::HeapObject::cast(object);
    if (is_live(heap_object)) {
      auto f = mmtk_get_forwarded_object(heap_object);
      if (f != nullptr) return i::Object((i::Address) f);
      return object;
    } else {
      return i::Object();
    }
  }
};

class WeakRefs {
  static constexpr int kMainThreadTask = 0;

  i::WeakObjects weak_objects_;
  bool have_code_to_deoptimize_ = false;

 public:
  void ProcessEphemerons() {
    // Do nothing at the moment.
    // TODO: Fix this
  }

  static i::Isolate* isolate() {
    return heap()->isolate();
  }
  static i::Heap* heap() {
    return tph::v8_heap;
  }
  i::WeakObjects* weak_objects() {
    return &weak_objects_;
  }

  void ClearNonLiveReferences() {
    have_code_to_deoptimize_ = false;
    {
      MMTkWeakObjectRetainer retainer;
      tph::Impl::ProcessAllWeakReferences(heap(), &retainer);
    }
    DCHECK(!have_code_to_deoptimize_);
  }

  std::function<void(void*)> trace_ = [](void*) { UNREACHABLE(); };
};

WeakRefs* global_weakref_processor = new WeakRefs();

}

#endif // MMTK_WEAK_REFS_H