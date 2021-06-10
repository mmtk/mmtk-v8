#ifndef MMTK_WEAK_REFS_H
#define MMTK_WEAK_REFS_H

#include "src/heap/heap.h"
#include "src/objects/string-table.h"
#include "src/objects/visitors.h"
#include "mmtk-visitors.h"

namespace v8 {
namespace internal {
namespace third_party_heap {

extern v8::internal::Heap* v8_heap;

}
}
}

namespace mmtk {

namespace i = v8::internal;

class InternalizedStringTableCleaner : public i::RootVisitor {
 public:
  explicit InternalizedStringTableCleaner(i::Heap* heap)
      : heap_(heap), pointers_removed_(0) {}

  void VisitRootPointers(i::Root root, const char* description, i::FullObjectSlot start, i::FullObjectSlot end) override {
    UNREACHABLE();
  }

  void VisitRootPointers(i::Root root, const char* description, i::OffHeapObjectSlot start, i::OffHeapObjectSlot end) override {
    DCHECK_EQ(root, i::Root::kStringTable);
    // Visit all HeapObject pointers in [start, end).
    auto isolate = heap_->isolate();
    for (auto p = start; p < end; ++p) {
      i::Object o = p.load(isolate);
      if (o.IsHeapObject()) {
        auto heap_object = i::HeapObject::cast(o);
        DCHECK(!i::Heap::InYoungGeneration(heap_object));
        if (!is_live_object(reinterpret_cast<void*>(heap_object.address()))) {
          pointers_removed_++;
          // Set the entry to the_hole_value (as deleted).
          p.store(i::StringTable::deleted_element());
        }
      }
    }
  }

  int PointersRemoved() { return pointers_removed_; }

 private:
  i::Heap* heap_;
  int pointers_removed_;
};

class ExternalStringTableCleaner : public i::RootVisitor {
 public:
  explicit ExternalStringTableCleaner(i::Heap* heap) : heap_(heap) {}

  void VisitRootPointers(i::Root root, const char* description, i::FullObjectSlot start, i::FullObjectSlot end) override {
    // Visit all HeapObject pointers in [start, end).
    auto the_hole = i::ReadOnlyRoots(heap_).the_hole_value();
    for (auto p = start; p < end; ++p) {
      i::Object o = *p;
      if (o.IsHeapObject()) {
        auto heap_object = i::HeapObject::cast(o);
        if (!is_live_object(reinterpret_cast<void*>(heap_object.address()))) {
          if (o.IsExternalString()) {
            heap_->FinalizeExternalString(i::String::cast(o));
          } else {
            // The original external string may have been internalized.
            DCHECK(o.IsThinString());
          }
          // Set the entry to the_hole_value (as deleted).
          p.store(the_hole);
        }
      }
    }
  }

 private:
  i::Heap* heap_;
};


class WeakRefs {
 public:
  static v8::internal::Heap* heap() {
    return v8::internal::third_party_heap::v8_heap;
  }

  void ClearNonLiveReferences() {
    {
      // TRACE_GC(heap()->tracer(), GCTracer::Scope::MC_CLEAR_STRING_TABLE);

      // Prune the string table removing all strings only pointed to by the
      // string table.  Cannot use string_table() here because the string
      // table is marked.
      v8::internal::StringTable* string_table = heap()->isolate()->string_table();
      InternalizedStringTableCleaner internalized_visitor(heap());
      string_table->DropOldData();
      string_table->IterateElements(&internalized_visitor);
      string_table->NotifyElementsRemoved(internalized_visitor.PointersRemoved());

      ExternalStringTableCleaner external_visitor(heap());
      heap()->UpdateExternalStringTable(&external_visitor);
    }
    v8::internal::third_party_heap::MMTkWeakObjectRetainer retainer;
    heap()->ProcessAllWeakReferences(&retainer);
  }
};

WeakRefs* global_weakref_processor = new WeakRefs();

}

#endif // MMTK_WEAK_REFS_H