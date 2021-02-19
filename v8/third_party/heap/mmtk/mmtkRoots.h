#ifndef MMTK_ROOTS_H
#define MMTK_ROOTS_H

#include "mmtk.h"
#include "src/objects/visitors.h"

template <class myType>
class MMTkRootVisitor : public RootVisitor {
 public:
  explicit MMTkRootVisitor(v8::internal::Isolate* isolate)
      : isolate_(isolate) {}

  void VisitRootPointer(Root root, const char* description,
                        FullObjectSlot p) override {
    ;
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot p = start; p < end; ++p)
      VisitRootPointer(root, description, p);
  }

 private:
  Isolate* isolate_;
};

#endif  // MMTK_ROOTS_H