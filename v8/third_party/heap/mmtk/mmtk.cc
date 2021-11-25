// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mmtk.h"
#include "src/heap/heap-inl.h"
#include "log.h"

namespace mmtk {
thread_local MMTk_Mutator mutator = nullptr;
}

namespace v8 {
namespace internal {
namespace third_party_heap {

v8::internal::Heap* v8_heap = nullptr;

base::AddressRegion code_range_;

extern V8_Upcalls mmtk_upcalls;

inline void CheckMutator(Heap* tph) {
  if (mmtk::mutator == nullptr) {
    mmtk::mutator = bind_mutator(mmtk::get_mmtk_instance(tph), &mmtk::mutator);
    tph->impl()->mutators_.push_back(mmtk::mutator);
  }
}

static std::atomic_bool IsolateCreated { false };

#define GB (1ull << 30)
#define FIXED_HEAP_SIZE (4ull * GB)

size_t Heap::Capacity() {
  return FIXED_HEAP_SIZE;
}

std::unique_ptr<Heap> Heap::New(v8::internal::Isolate* isolate) {
  DCHECK(!v8_heap);
  v8_heap = isolate->heap();
  // MMTK current default maximum heap size is 1GB.
  auto isolate_created = IsolateCreated.exchange(true);
  DCHECK_WITH_MSG(!isolate_created, "Multiple isolates are not supported.");
  MMTK_LOG("New Isolate: %lx\n", (unsigned long) isolate);
  MMTk_Heap mmtk_heap = v8_new_heap(&mmtk_upcalls, FIXED_HEAP_SIZE);
  // FIXME
  code_range_ = base::AddressRegion(0x60000000, (0xb0000000- 0x60000000)); // isolate->AddCodeRange(code_range_.begin(), code_range_.size());
  auto v8_tph = std::make_unique<Heap>();
  v8_tph->impl_ = new Impl(mmtk_heap, isolate, tph_archive_new());
  return v8_tph;
}

v8::internal::Isolate* Heap::GetIsolate(Address object_pointer) {
  return v8_heap->isolate();
}


// Address space in Rust is statically from 0x60000000 - 0xb0000000
AllocationResult Heap::Allocate(size_t size_in_bytes, AllocationType type, AllocationAlignment align) {
  CheckMutator(this);
  if (!v8_heap->deserialization_complete() && type == AllocationType::kOld) {
    type = AllocationType::kMap;
  }
  bool large_object = size_in_bytes > kMaxRegularHeapObjectSize;
  size_t align_bytes = (type == AllocationType::kCode) ? kCodeAlignment : (align == kWordAligned) ? kSystemPointerSize : (align == kDoubleAligned) ? kDoubleSize : kSystemPointerSize;
  auto mmtk_allocator = mmtk::GetAllocationSemanticForV8AllocationType(type, large_object);
  Address result =
      reinterpret_cast<Address>(alloc(mmtk::mutator, size_in_bytes, align_bytes, 0, (int) mmtk_allocator));
  tph_archive_insert(mmtk::get_object_archive(this), reinterpret_cast<void*>(result),mmtk::get_isolate(this));
  HeapObject rtn = HeapObject::FromAddress(result);
  return rtn;
}

bool Heap::IsPendingAllocation(HeapObject object) {
  return false;
}

Address Heap::GetObjectFromInnerPointer(Address inner_pointer) {
  return reinterpret_cast<Address>(
      tph_archive_inner_to_obj(mmtk::get_object_archive(this),
                               reinterpret_cast<void*>(inner_pointer)));
}

const v8::base::AddressRegion& Heap::GetCodeRange() {
  return code_range_;
}

bool Heap::CollectGarbage() {
  v8_heap->gc_count_++;
  v8_heap->SetGCState(v8::internal::Heap::MARK_COMPACT);
  handle_user_collection_request(mmtk::get_mmtk_instance(this), (void*) 0);
  v8_heap->SetGCState(v8::internal::Heap::NOT_IN_GC);
  return true;
}

bool Heap::InSpace(Address address, AllocationSpace v8_space) {
  auto mmtk_space = mmtk::GetAllocationSemanticForV8Space(v8_space);
  // TODO(wenyuzhao): Infer isolate from address. May involves consulting the SFT.
  auto mmtk = mmtk::get_mmtk_instance(v8_heap);
  return mmtk_in_space(mmtk, (void*) address, (size_t) mmtk_space) != 0;
}

bool Heap::IsImmovable(HeapObject object) {
  return mmtk_is_movable(object) == 0;
}

bool Heap::InOldSpace(Address address) {
  return InSpace(address, OLD_SPACE);
}


bool Heap::InCodeSpace(Address address) {
  return InSpace(address, CODE_SPACE);
}

bool Heap::InReadOnlySpace(Address address) {
  return InSpace(address, RO_SPACE);
}

bool Heap::InLargeObjectSpace(Address address) {
  return InSpace(address, LO_SPACE);
}

bool Heap::IsValidHeapObject(HeapObject object) {
  return mmtk_object_is_live(reinterpret_cast<void*>(object.address())) != 0;
}

void Heap::ResetIterator() {
  tph_archive_iter_reset(mmtk::get_object_archive(this));
}

HeapObject Heap::NextObject() {
  void* obj_addr = tph_archive_iter_next(mmtk::get_object_archive(this));
  if (obj_addr != nullptr) {
    return HeapObject::FromAddress(reinterpret_cast<Address>(obj_addr));
  } else {
    return HeapObject();
  }
}

}
}  // namespace internal
}  // namespace v8
