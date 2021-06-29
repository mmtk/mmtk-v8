// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mmtk.h"
#include "src/heap/heap-inl.h"
#include "log.h"

namespace v8 {
namespace internal {
namespace third_party_heap {



// Data structure required for Rust-MMTK

v8::internal::Heap* v8_heap = nullptr;


base::AddressRegion code_range_;

thread_local BumpAllocator* tph_mutator_ = nullptr;

std::vector<TPHData*>* tph_data_list = new std::vector<TPHData*>();

extern V8_Upcalls mmtk_upcalls;

TPHData* get_tph_data(Heap* tph) {
  for (size_t i = 0; i < tph_data_list->size(); i++)
  {
    TPHData* tph_data_ = reinterpret_cast<TPHData*>((*tph_data_list)[i]);
    if (tph_data_->v8_tph() == tph) {
      return tph_data_;
    }
  }
  UNREACHABLE();
}

std::vector<BumpAllocator*>* all_mutators = new std::vector<BumpAllocator*>();

thread_local bool is_mutator = false;

inline void CheckMutator(Heap* tph) {
  TPHData* tph_data_ = get_tph_data(tph);
  if (tph_mutator_ == nullptr) {
    tph_mutator_ = reinterpret_cast<BumpAllocator*>(
      bind_mutator(tph_data_->mmtk_heap(), &tph_mutator_));
    tph_mutator_->tph_data = tph_data_;
    all_mutators->push_back(tph_mutator_);
  }
  is_mutator = true;
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
  MMTk_Heap new_heap = v8_new_heap(&mmtk_upcalls, FIXED_HEAP_SIZE);
  // FIXME
  code_range_ = base::AddressRegion(0x60000000, (0xb0000000- 0x60000000)); // isolate->AddCodeRange(code_range_.begin(), code_range_.size());
  auto v8_tph = std::make_unique<Heap>();
  TPHData* tph_data = new TPHData(v8_tph.get(), new_heap, isolate, tph_archive_new());
  tph_data_list->push_back(tph_data);
  return v8_tph;
}

v8::internal::Isolate* Heap::GetIsolate(Address object_pointer) {
  return v8_heap->isolate();
}


// Address space in Rust is statically from 0x60000000 - 0xb0000000
AllocationResult Heap::Allocate(size_t size_in_bytes, AllocationType type, AllocationAlignment align) {
  CheckMutator(this);
  if (!initialization_finished_ && type == AllocationType::kOld) {
    type = AllocationType::kMap;
  }
  TPHData* tph_data_ = get_tph_data(this);
  bool large_object = size_in_bytes > kMaxRegularHeapObjectSize;
  size_t align_bytes = (type == AllocationType::kCode) ? kCodeAlignment : (align == kWordAligned) ? kSystemPointerSize : (align == kDoubleAligned) ? kDoubleSize : kSystemPointerSize;
  auto mmtk_allocator = mmtk::GetAllocationSemanticForV8AllocationType(type, large_object);
  Address result =
      reinterpret_cast<Address>(alloc(tph_mutator_, size_in_bytes, align_bytes, 0, (int) mmtk_allocator));
  tph_archive_insert(tph_data_->archive(), reinterpret_cast<void*>(result), tph_data_->isolate());
  HeapObject rtn = HeapObject::FromAddress(result);
  return rtn;
}

Address Heap::GetObjectFromInnerPointer(Address inner_pointer) {
  TPHData* tph_data_ = get_tph_data(this);
  return reinterpret_cast<Address>(
      tph_archive_inner_to_obj(tph_data_->archive(),
                               reinterpret_cast<void*>(inner_pointer)));
}

const v8::base::AddressRegion& Heap::GetCodeRange() {
  return code_range_;
}

bool Heap::CollectGarbage() {
  v8_heap->increase_gc_count();
  v8_heap->SetGCState(v8::internal::Heap::MARK_COMPACT);
  handle_user_collection_request(get_tph_data(this)->mmtk_heap(), (void*) 0);
  v8_heap->SetGCState(v8::internal::Heap::NOT_IN_GC);
  return true;
}

bool Heap::InSpace(Address address, AllocationSpace v8_space) {
  auto mmtk_space = mmtk::GetAllocationSemanticForV8Space(v8_space);
  auto mmtk = get_tph_data(v8_heap->tp_heap_.get())->mmtk_heap();
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
  TPHData* tph_data_ = get_tph_data(this);
  tph_archive_iter_reset(tph_data_->archive());
}

HeapObject Heap::NextObject() {
  TPHData* tph_data_ = get_tph_data(this);
  void* obj_addr = tph_archive_iter_next(tph_data_->archive());
  if (obj_addr != nullptr) {
    return HeapObject::FromAddress(reinterpret_cast<Address>(obj_addr));
  } else {
    return HeapObject();
  }
}

}
}  // namespace internal
}  // namespace v8
