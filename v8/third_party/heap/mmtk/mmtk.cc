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

static std::atomic_bool IsolateCreated { false };

#define GB (1ull << 30)
#define FIXED_HEAP_SIZE (1ull * GB)

size_t Heap::Capacity() {
  return FIXED_HEAP_SIZE;
}

std::unique_ptr<Heap> Heap::New(v8::internal::Isolate* isolate) {
  // MMTK current default maximum heap size is 1GB.
  auto isolate_created = IsolateCreated.exchange(true);
  DCHECK_WITH_MSG(!isolate_created, "Multiple isolates are not supported.");
  fprintf(stderr, "New Isolate: %lx\n", (unsigned long) isolate);
  MMTk_Heap new_heap = v8_new_heap(&mmtk_upcalls, FIXED_HEAP_SIZE);
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
  // Get MMTk space that the object should be allocated to.
  Address result =
      reinterpret_cast<Address>(alloc(tph_mutator_, size_in_bytes, align_bytes, 0, 0));
  // Remember the V8 internal `AllocationSpace` for this object.
  // This is required to pass various V8 internal space checks.
  // TODO(wenyuzhao): Use MMTk's vm-specific spaces for allocation instead of remembering the `AllocationSpace`s.
  AllocationSpace allocation_space;
  if (type == AllocationType::kCode) {
    allocation_space = large_object ? CODE_LO_SPACE : CODE_SPACE;
  } else if (type == AllocationType::kReadOnly) {
    allocation_space = RO_SPACE;
  } else {
    allocation_space = large_object ? LO_SPACE : OLD_SPACE;
  }
  tph_archive_insert(tph_data_->archive(), reinterpret_cast<void*>(result), tph_data_->isolate(), uint8_t(allocation_space));
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

// Uninitialized space tag
constexpr AllocationSpace kNoSpace = AllocationSpace(255);

// Checks whether the address is *logically* in the allocation_space.
// This does not related the real MMTk space that contains the address,
// but the V8 internal space expected by the runtime.
//
// TODO: Currently we record the space tag for each object. In the future we
// need to link each allocation_space to a real MMTk space.
bool Heap::InSpace(Address address, AllocationSpace allocation_space) {
  for (auto tph_data : *tph_data_list) {
    auto space = AllocationSpace(tph_archive_obj_to_space(tph_data->archive(), reinterpret_cast<void*>(address)));
    if (space == kNoSpace) continue;
    return space == allocation_space;
  }
  UNREACHABLE();
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
