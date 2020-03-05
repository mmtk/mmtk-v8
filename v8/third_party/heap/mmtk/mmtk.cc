// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/third-party/heap-api.h"
#include "src/base/address-region.h"
#include "src/heap/heap.h"
#include "src/execution/isolate.h"
#include "mmtk.h"
#include <cassert>
#include <set>
#include <iterator>

namespace v8 {
namespace internal {
namespace third_party_heap {

// Data structure required for Rust-MMTK
class BumpAllocator {
 public:
  v8::internal::Isolate* isolate;
  uintptr_t cursor;
  uintptr_t limit;
  void* space;
};


base::AddressRegion code_range_;

BumpAllocator* tph_mutator_;

std::unique_ptr<Heap> Heap::New(v8::internal::Isolate* isolate) {
  // MMTK current default maximum heap size is 1GB.
  const size_t GB = 1u << 30;
  openjdk_gc_init(nullptr, GB);    
  tph_mutator_ = reinterpret_cast<BumpAllocator*>(bind_mutator(isolate));
  tph_mutator_->isolate = isolate;
  code_range_ = base::AddressRegion(0x60000000, (0xb0000000- 0x60000000)); // FIXME
  isolate->AddCodeRange(code_range_.begin(), code_range_.size());
  return std::make_unique<Heap>();
}

v8::internal::Isolate* Heap::GetIsolate(uintptr_t address) {
  return tph_mutator_->isolate;
}

// Address space in Rust is statically from 0x60000000 - 0xb0000000
AllocationResult Heap::Allocate(size_t size_in_bytes, AllocationType type, AllocationAlignment align) {
  // API call to rust
  size_t align_bytes = (align == kCodeAligned) ? kCodeAlignment : (align == kWordAligned) ? kSystemPointerSize : (align == kDoubleAligned) ? kDoubleSize : 0;
  int space = (type == AllocationType::kCode) ? 7 : (type == AllocationType::kReadOnly) ? 9 : 0;
  Address result =
      reinterpret_cast<Address>(alloc(tph_mutator_, size_in_bytes, align_bytes, 0, space));
  // Verify that cursor is less than max heap size in Rust
  // if (type == AllocationType::kCode)
  //   printf("AC %lx %ld\n", result, align_bytes);
  // else if (type == AllocationType::kReadOnly) 
  //   printf("AR %lx %ld\n", result, align_bytes);
  // else
  //   printf("AD %lx %ld\n", result, align_bytes);

  assert(tph_mutator_->cursor <= 0xb0000000);
  // printf("A %lx %lu\n",result, size_in_bytes);
  HeapObject rtn = HeapObject::FromAddress(result);
  all_objects_.emplace(rtn);
  return rtn;
}

Address Heap::GetObjectFromInnerPointer(Address inner_pointer) {
    return reinterpret_cast<Address>(
        get_object_head_address(reinterpret_cast<void*>(inner_pointer)));
}

const v8::base::AddressRegion& Heap::GetCodeRange() {
  return code_range_;
}

bool Heap::CollectGarbage() {
  return true;
}

bool Heap::InCodeSpace(Address address) {
  return is_in_code_space(reinterpret_cast<void*>(address));
}
bool Heap::InReadOnlySpace(Address address) {
   return is_in_read_only_space(reinterpret_cast<void*>(address));
}
bool Heap::IsValidHeapObject(HeapObject object) {
  return is_valid_ref(reinterpret_cast<void*>(object.address()));
}

void Heap::ResetIterator() {
  object_iterator_ = all_objects_.begin();
}

HeapObject Heap::NextObject() {
  return *object_iterator_;
}

}
}  // namespace internal
}  // namespace v8
