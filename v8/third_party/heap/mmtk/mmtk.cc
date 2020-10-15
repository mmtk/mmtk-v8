// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mmtk.h"

namespace v8 {
namespace internal {
namespace third_party_heap {

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

// Data structure required for Rust-MMTK
class BumpAllocator {
 public:
  TPHData* tph_data;
  uintptr_t cursor;
  uintptr_t limit;
  void* space;
};


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

inline void CheckMutator(Heap* tph) {
  TPHData* tph_data_ = get_tph_data(tph);
  if (tph_mutator_ == nullptr) {
    tph_mutator_ = reinterpret_cast<BumpAllocator*>(
      bind_mutator(tph_data_->mmtk_heap(), &tph_mutator_));
    tph_mutator_->tph_data = tph_data_;
  }
}

MMTk_Heap GetMMTkHeap(Address object_pointer) {
  for (size_t i = 0; i < tph_data_list->size(); i++)
  {
    TPHData* tph_data_ = reinterpret_cast<TPHData*>((*tph_data_list)[i]);
    void* ptr = tph_archive_obj_to_isolate(
          tph_data_->archive(), reinterpret_cast<void*>(object_pointer));
    if (ptr != nullptr) {
      return tph_data_->mmtk_heap();
    }
  }
  UNREACHABLE();
}

std::unique_ptr<Heap> Heap::New(v8::internal::Isolate* isolate) {
  // MMTK current default maximum heap size is 1GB.
  printf("New Isolate: %lx\n", (unsigned long) isolate);
  const size_t GB = 1u << 30;
  MMTk_Heap new_heap = v8_new_heap(&mmtk_upcalls, GB);    
  tph_mutator_ = reinterpret_cast<BumpAllocator*>(bind_mutator(new_heap, &tph_mutator_));
  // FIXME
  code_range_ = base::AddressRegion(0x60000000, (0xb0000000- 0x60000000)); // isolate->AddCodeRange(code_range_.begin(), code_range_.size());
  auto v8_tph = std::make_unique<Heap>();
  TPHData* tph_data = new TPHData(v8_tph.get(), new_heap, isolate, tph_archive_new());
  tph_mutator_->tph_data = tph_data;
  tph_data_list->push_back(tph_data);
  return v8_tph;
}

v8::internal::Isolate* Heap::GetIsolate(Address object_pointer) {
  for (size_t i = 0; i < tph_data_list->size(); i++)
  {
    TPHData* tph_data_ = reinterpret_cast<TPHData*>((*tph_data_list)[i]);
    void* ptr = tph_archive_obj_to_isolate(
          tph_data_->archive(), reinterpret_cast<void*>(object_pointer));
    if (ptr != nullptr) {
      return reinterpret_cast<v8::internal::Isolate*>(ptr);
    }
  }
  UNREACHABLE();
}


// Address space in Rust is statically from 0x60000000 - 0xb0000000
AllocationResult Heap::Allocate(size_t size_in_bytes, AllocationType type, AllocationAlignment align) {
  CheckMutator(this);
  TPHData* tph_data_ = get_tph_data(this);
  bool large_object = size_in_bytes > kMaxRegularHeapObjectSize;
  size_t align_bytes = (type == AllocationType::kCode) ? kCodeAlignment : (align == kWordAligned) ? kSystemPointerSize : (align == kDoubleAligned) ? kDoubleSize : kSystemPointerSize;
  int space = (type == AllocationType::kCode) ? 3 : (type == AllocationType::kReadOnly) ? 4 : (large_object) ? 2 : 0;
  Address result =
      reinterpret_cast<Address>(alloc(tph_mutator_, size_in_bytes, align_bytes, 0, space));
  tph_archive_insert(tph_data_->archive(), reinterpret_cast<void*>(result), tph_data_->isolate(), uint8_t(space));
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
  return true;
}

bool Heap::InCodeSpace(Address address) {
  for (size_t i = 0; i < tph_data_list->size(); i++)
  {
    TPHData* tph_data_ = reinterpret_cast<TPHData*>((*tph_data_list)[i]);
    uint8_t space = tph_archive_obj_to_space(
          tph_data_->archive(), reinterpret_cast<void*>(address));
    if (space == 255) continue;
    if (space == 3) return true;
    else return false;
  }
  UNREACHABLE();
}

bool Heap::InReadOnlySpace(Address address) {
  for (size_t i = 0; i < tph_data_list->size(); i++)
  {
    TPHData* tph_data_ = reinterpret_cast<TPHData*>((*tph_data_list)[i]);
    uint8_t space = tph_archive_obj_to_space(
          tph_data_->archive(), reinterpret_cast<void*>(address));
    if (space == 255) continue;
    if (space == 4) return true;
    else return false;
  }
  UNREACHABLE();
}

bool Heap::InLargeObjectSpace(Address address) {
  for (size_t i = 0; i < tph_data_list->size(); i++)
  {
    TPHData* tph_data_ = reinterpret_cast<TPHData*>((*tph_data_list)[i]);
    uint8_t space = tph_archive_obj_to_space(
          tph_data_->archive(), reinterpret_cast<void*>(address));
    if (space == 255) continue;
    if (space == 2) return true;
    else return false;
  }
  UNREACHABLE();
}

bool Heap::IsValidHeapObject(HeapObject object) {
  return is_live_object(reinterpret_cast<void*>(object.address()));
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
