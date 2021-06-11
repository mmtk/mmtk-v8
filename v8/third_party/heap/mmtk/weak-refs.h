#ifndef MMTK_WEAK_REFS_H
#define MMTK_WEAK_REFS_H

#include "src/heap/heap.h"
#include "src/objects/string-table.h"
#include "src/objects/visitors.h"
#include "src/objects/transitions-inl.h"

namespace v8 {
namespace internal {
namespace third_party_heap {

extern v8::internal::Heap* v8_heap;

}
}
}

namespace mmtk {

namespace i = v8::internal;
namespace base = v8::base;
namespace tph = v8::internal::third_party_heap;

bool is_live(i::HeapObject o) {
  return is_live_object(reinterpret_cast<void*>(o.address()));
}

i::MaybeObject to_weakref(i::HeapObject o) {
  DCHECK(o.IsStrong());
  return i::MaybeObject::MakeWeak(i::MaybeObject::FromObject(o));
}

base::Optional<i::HeapObject> get_forwarded_ref(i::HeapObject o) {
  DCHECK(o.IsStrong());
  auto f = mmtk_get_forwarded_object(o);
  if (f != nullptr) {
    auto x = i::HeapObject::cast(i::Object((i::Address) f));
    DCHECK(o.IsStrong());
    return x;
  }
  return base::nullopt;
}

class MMTkWeakObjectRetainer: public i::WeakObjectRetainer {
 public:
  virtual i::Object RetainAs(i::Object object) override final {
    // LOG("RetainAs %p\n", (void*) object.ptr());
    if (object == i::Object()) return object;
    auto heap_object = i::HeapObject::cast(object);
    if (tph::Heap::IsValidHeapObject(heap_object)) {
      auto f = mmtk_get_forwarded_object(heap_object);
      if (f != nullptr) {
        // LOG("%p -> %p\n", (void*) object.ptr(), (void*) f);
        return i::Object((i::Address) f);
      }
      // LOG("%p is dead 1 \n", (void*) object.ptr());
      return object;
    } else {
      // LOG("%p is dead 2 \n", (void*) object.ptr());
      return i::Object();
    }
  }
};

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
        if (!is_live(heap_object)) {
          pointers_removed_++;
          // Set the entry to the_hole_value (as deleted).
          p.store(i::StringTable::deleted_element());
        } else {
          auto forwarded = get_forwarded_ref(heap_object);
          if (forwarded) p.store(*forwarded);
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
        if (!is_live(heap_object)) {
          if (o.IsExternalString()) {
            heap_->FinalizeExternalString(i::String::cast(o));
          } else {
            // The original external string may have been internalized.
            DCHECK(o.IsThinString());
          }
          // Set the entry to the_hole_value (as deleted).
          p.store(the_hole);
        } else {
          auto forwarded = get_forwarded_ref(heap_object);
          if (forwarded) p.store(*forwarded);
        }
      }
    }
  }

 private:
  i::Heap* heap_;
};


class WeakRefs {
  static constexpr int kMainThreadTask = 0;
  i::WeakObjects weak_objects_;

  void FlushBytecodeFromSFI(i::SharedFunctionInfo shared_info) {
    DCHECK(shared_info.HasBytecodeArray());
    // Retain objects required for uncompiled data.
    auto inferred_name = shared_info.inferred_name();
    int start_position = shared_info.StartPosition();
    int end_position = shared_info.EndPosition();

    shared_info.DiscardCompiledMetadata(isolate(), [](i::HeapObject object, i::ObjectSlot slot, i::HeapObject target) {
      // RecordSlot(object, slot, target);
    });

    // The size of the bytecode array should always be larger than an
    // UncompiledData object.
    STATIC_ASSERT(i::BytecodeArray::SizeFor(0) >= i::UncompiledDataWithoutPreparseData::kSize);

    // Replace bytecode array with an uncompiled data array.
    auto compiled_data = shared_info.GetBytecodeArray(isolate());
    // auto compiled_data_start = compiled_data.address();
    int compiled_data_size = compiled_data.Size();
    // auto chunk = MemoryChunk::FromAddress(compiled_data_start);

    // Clear any recorded slots for the compiled data as being invalid.
    // DCHECK_NULL(chunk->sweeping_slot_set());
    // RememberedSet<OLD_TO_NEW>::RemoveRange(
    //     chunk, compiled_data_start, compiled_data_start + compiled_data_size,
    //     SlotSet::FREE_EMPTY_BUCKETS);
    // RememberedSet<OLD_TO_OLD>::RemoveRange(
    //     chunk, compiled_data_start, compiled_data_start + compiled_data_size,
    //     SlotSet::FREE_EMPTY_BUCKETS);

    // Swap the map, using set_map_after_allocation to avoid verify heap checks
    // which are not necessary since we are doing this during the GC atomic pause.
    compiled_data.set_map_after_allocation(
        i::ReadOnlyRoots(heap()).uncompiled_data_without_preparse_data_map(),
        i::SKIP_WRITE_BARRIER);

    // Create a filler object for any left over space in the bytecode array.
    if (!heap()->IsLargeObject(compiled_data)) {
      heap()->CreateFillerObjectAt(
          compiled_data.address() + i::UncompiledDataWithoutPreparseData::kSize,
          compiled_data_size - i::UncompiledDataWithoutPreparseData::kSize,
          i::ClearRecordedSlots::kNo);
    }

    // Initialize the uncompiled data.
    auto uncompiled_data = i::UncompiledData::cast(compiled_data);
    uncompiled_data.InitAfterBytecodeFlush(
        inferred_name, start_position, end_position,
        [](i::HeapObject object, i::ObjectSlot slot, i::HeapObject target) {
          // RecordSlot(object, slot, target);
        });

    // Mark the uncompiled data as black, and ensure all fields have already been
    // marked.
    DCHECK(is_live(inferred_name));
    DCHECK(is_live(uncompiled_data));

    auto forwarded = get_forwarded_ref(uncompiled_data);
    if (forwarded) uncompiled_data = i::UncompiledData::cast(*forwarded);

    // Use the raw function data setter to avoid validity checks, since we're
    // performing the unusual task of decompiling.
    shared_info.set_function_data(uncompiled_data, v8::kReleaseStore);
    DCHECK(!shared_info.is_compiled());
  }

  void ClearOldBytecodeCandidates() {
    DCHECK(i::FLAG_flush_bytecode || weak_objects_.bytecode_flushing_candidates.IsEmpty());
    i::SharedFunctionInfo flushing_candidate;
    while (weak_objects_.bytecode_flushing_candidates.Pop(kMainThreadTask, &flushing_candidate)) {
      // If the BytecodeArray is dead, flush it, which will replace the field with
      // an uncompiled data object.
      if (!is_live(flushing_candidate.GetBytecodeArray(heap()->isolate()))) {
        FlushBytecodeFromSFI(flushing_candidate);
      } else {
        DCHECK(!get_forwarded_ref(flushing_candidate.GetBytecodeArray(heap()->isolate())));
      }
      // Now record the slot, which has either been updated to an uncompiled data,
      // or is the BytecodeArray which is still alive.
      // auto slot =
      //     flushing_candidate.RawField(SharedFunctionInfo::kFunctionDataOffset);
      // RecordSlot(flushing_candidate, slot, HeapObject::cast(*slot));
    }
  }

  void ClearFlushedJsFunctions() {
    DCHECK(i::FLAG_flush_bytecode || weak_objects_.flushed_js_functions.IsEmpty());
    i::JSFunction flushed_js_function;
    while (weak_objects_.flushed_js_functions.Pop(kMainThreadTask, &flushed_js_function)) {
      auto gc_notify_updated_slot = [](i::HeapObject object, i::ObjectSlot slot, i::Object target) {
        // RecordSlot(object, slot, HeapObject::cast(target));
      };
      flushed_js_function.ResetIfBytecodeFlushed(gc_notify_updated_slot);
      UNREACHABLE();
    }
  }

  void ClearFullMapTransitions() {
    i::TransitionArray array;
    while (weak_objects_.transition_arrays.Pop(kMainThreadTask, &array)) {
      int num_transitions = array.number_of_entries();
      if (num_transitions > 0) {
        i::Map map;
        // The array might contain "undefined" elements because it's not yet
        // filled. Allow it.
        if (array.GetTargetIfExists(0, isolate(), &map)) {
          DCHECK(!map.is_null());  // Weak pointers aren't cleared yet.
          auto constructor_or_back_pointer = map.constructor_or_back_pointer();
          if (constructor_or_back_pointer.IsSmi()) {
            DCHECK(isolate()->has_active_deserializer());
            DCHECK_EQ(constructor_or_back_pointer, i::Deserializer::uninitialized_field_value());
            continue;
          }
          auto parent = i::Map::cast(map.constructor_or_back_pointer());
          bool parent_is_alive = is_live(parent);
          if (parent_is_alive) {
            DCHECK(!get_forwarded_ref(parent));
          }
          auto descriptors = parent_is_alive ? parent.instance_descriptors(isolate()) : i::DescriptorArray();
          bool descriptors_owner_died = CompactTransitionArray(parent, array, descriptors);
          if (descriptors_owner_died) {
            TrimDescriptorArray(parent, descriptors);
          }
        }
      }
    }
  }

  bool TransitionArrayNeedsCompaction(i::TransitionArray transitions, int num_transitions) {
    for (int i = 0; i < num_transitions; ++i) {
      i::MaybeObject raw_target = transitions.GetRawTarget(i);
      if (raw_target.IsSmi()) {
        // This target is still being deserialized,
        DCHECK(isolate()->has_active_deserializer());
        DCHECK_EQ(raw_target.ToSmi(), i::Deserializer::uninitialized_field_value());
        return false;
      } else if (!is_live(i::TransitionsAccessor::GetTargetFromRaw(raw_target))) {
        return true;
      } else {
        DCHECK(!get_forwarded_ref(i::TransitionsAccessor::GetTargetFromRaw(raw_target)));
      }
    }
    return false;
  }

  bool CompactTransitionArray(i::Map map, i::TransitionArray transitions, i::DescriptorArray descriptors) {
    DCHECK(!map.is_prototype_map());
    int num_transitions = transitions.number_of_entries();
    if (!TransitionArrayNeedsCompaction(transitions, num_transitions)) {
      return false;
    }
    bool descriptors_owner_died = false;
    int transition_index = 0;
    // Compact all live transitions to the left.
    for (int i = 0; i < num_transitions; ++i) {
      i::Map target = transitions.GetTarget(i);
      DCHECK_EQ(target.constructor_or_back_pointer(), map);
      if (!is_live(target)) {
        if (!descriptors.is_null() &&
            target.instance_descriptors(isolate()) == descriptors) {
          DCHECK(!target.is_prototype_map());
          descriptors_owner_died = true;
        }
      } else {
        DCHECK(!get_forwarded_ref(target));
        if (i != transition_index) {
          i::Name key = transitions.GetKey(i);
          transitions.SetKey(transition_index, key);
          // i::HeapObjectSlot key_slot = transitions.GetKeySlot(transition_index);
          // RecordSlot(transitions, key_slot, key);
          i::MaybeObject raw_target = transitions.GetRawTarget(i);
          transitions.SetRawTarget(transition_index, raw_target);
          // i::HeapObjectSlot target_slot =
          //     transitions.GetTargetSlot(transition_index);
          // RecordSlot(transitions, target_slot, raw_target->GetHeapObject());
        }
        transition_index++;
      }
    }
    // If there are no transitions to be cleared, return.
    if (transition_index == num_transitions) {
      DCHECK(!descriptors_owner_died);
      return false;
    }
    // Note that we never eliminate a transition array, though we might right-trim
    // such that number_of_transitions() == 0. If this assumption changes,
    // TransitionArray::Insert() will need to deal with the case that a transition
    // array disappeared during GC.
    int trim = transitions.Capacity() - transition_index;
    if (trim > 0) {
      heap()->RightTrimWeakFixedArray(transitions, trim * i::TransitionArray::kEntrySize);
      transitions.SetNumberOfTransitions(transition_index);
    }
    return descriptors_owner_died;
  }

  void RightTrimDescriptorArray(i::DescriptorArray array, int descriptors_to_trim) {
    int old_nof_all_descriptors = array.number_of_all_descriptors();
    int new_nof_all_descriptors = old_nof_all_descriptors - descriptors_to_trim;
    DCHECK_LT(0, descriptors_to_trim);
    DCHECK_LE(0, new_nof_all_descriptors);
    auto start = array.GetDescriptorSlot(new_nof_all_descriptors).address();
    auto end = array.GetDescriptorSlot(old_nof_all_descriptors).address();
    // MemoryChunk* chunk = MemoryChunk::FromHeapObject(array);
    // DCHECK_NULL(chunk->sweeping_slot_set());
    // RememberedSet<OLD_TO_NEW>::RemoveRange(chunk, start, end,
    //                                        SlotSet::FREE_EMPTY_BUCKETS);
    // RememberedSet<OLD_TO_OLD>::RemoveRange(chunk, start, end,
    //                                        SlotSet::FREE_EMPTY_BUCKETS);
    heap()->CreateFillerObjectAt(start, static_cast<int>(end - start), i::ClearRecordedSlots::kNo);
    array.set_number_of_all_descriptors(new_nof_all_descriptors);
  }

  void TrimDescriptorArray(i::Map map, i::DescriptorArray descriptors) {
    int number_of_own_descriptors = map.NumberOfOwnDescriptors();
    if (number_of_own_descriptors == 0) {
      DCHECK(descriptors == i::ReadOnlyRoots(heap()).empty_descriptor_array());
      return;
    }
    int to_trim = descriptors.number_of_all_descriptors() - number_of_own_descriptors;
    if (to_trim > 0) {
      descriptors.set_number_of_descriptors(number_of_own_descriptors);
      RightTrimDescriptorArray(descriptors, to_trim);
      TrimEnumCache(map, descriptors);
      descriptors.Sort();
    }
    DCHECK(descriptors.number_of_descriptors() == number_of_own_descriptors);
    map.set_owns_descriptors(true);
  }

  void TrimEnumCache(i::Map map, i::DescriptorArray descriptors) {
    int live_enum = map.EnumLength();
    if (live_enum == i::kInvalidEnumCacheSentinel) {
      live_enum = map.NumberOfEnumerableProperties();
    }
    if (live_enum == 0) return descriptors.ClearEnumCache();
    auto enum_cache = descriptors.enum_cache();

    auto keys = enum_cache.keys();
    int to_trim = keys.length() - live_enum;
    if (to_trim <= 0) return;
    heap()->RightTrimFixedArray(keys, to_trim);

    auto indices = enum_cache.indices();
    to_trim = indices.length() - live_enum;
    if (to_trim <= 0) return;
    heap()->RightTrimFixedArray(indices, to_trim);
  }

  void ClearWeakCollections() {
    i::EphemeronHashTable table;
    while (weak_objects_.ephemeron_hash_tables.Pop(kMainThreadTask, &table)) {
      for (i::InternalIndex i : table.IterateEntries()) {
        auto key = i::HeapObject::cast(table.KeyAt(i));
        if (!is_live(key)) {
          table.RemoveEntry(i);
        } else {
          DCHECK(!get_forwarded_ref(key));
        }
      }
    }
    for (auto it = heap()->ephemeron_remembered_set_.begin(); it != heap()->ephemeron_remembered_set_.end();) {
      if (!is_live(it->first)) {
        it = heap()->ephemeron_remembered_set_.erase(it);
      } else {
        DCHECK(!get_forwarded_ref(it->first));
        ++it;
      }
    }
  }

  void ClearWeakReferences() {
    std::pair<i::HeapObject, i::HeapObjectSlot> slot;
    auto cleared_weak_ref = i::HeapObjectReference::ClearedValue(isolate());
    while (weak_objects_.weak_references.Pop(kMainThreadTask, &slot)) {
      i::HeapObject value;
      // The slot could have been overwritten, so we have to treat it
      // as MaybeObjectSlot.
      i::MaybeObjectSlot location(slot.second);
      if ((*location)->GetHeapObjectIfWeak(&value)) {
        DCHECK(!value.IsCell());
        if (is_live(value)) {
          // The value of the weak reference is alive.
          // RecordSlot(slot.first, HeapObjectSlot(location), value);
          auto forwarded = get_forwarded_ref(value);
          if (forwarded) location.store(to_weakref(*forwarded));
        } else {
          if (value.IsMap()) {
            // The map is non-live.
            ClearPotentialSimpleMapTransition(i::Map::cast(value));
          }
          location.store(cleared_weak_ref);
        }
      }
    }
  }

  void ClearPotentialSimpleMapTransition(i::Map dead_target) {
    DCHECK(!is_live(dead_target));
    auto potential_parent = dead_target.constructor_or_back_pointer();
    if (potential_parent.IsMap()) {
      auto parent = i::Map::cast(potential_parent);
      i::DisallowGarbageCollection no_gc_obviously;
      if (is_live(parent)) {
          DCHECK(!get_forwarded_ref(parent));
      }
      if (is_live(parent) && i::TransitionsAccessor(isolate(), parent, &no_gc_obviously).HasSimpleTransitionTo(dead_target)) {
        ClearPotentialSimpleMapTransition(parent, dead_target);
      }
    }
  }

  void ClearPotentialSimpleMapTransition(i::Map map, i::Map dead_target) {
    DCHECK(!map.is_prototype_map());
    DCHECK(!dead_target.is_prototype_map());
    DCHECK_EQ(map.raw_transitions(), i::HeapObjectReference::Weak(dead_target));
    // Take ownership of the descriptor array.
    int number_of_own_descriptors = map.NumberOfOwnDescriptors();
    auto descriptors = map.instance_descriptors(isolate());
    if (descriptors == dead_target.instance_descriptors(isolate()) && number_of_own_descriptors > 0) {
      TrimDescriptorArray(map, descriptors);
      DCHECK(descriptors.number_of_descriptors() == number_of_own_descriptors);
    }
  }

  void ClearJSWeakRefs() {
    i::JSWeakRef weak_ref;
    while (weak_objects_.js_weak_refs.Pop(kMainThreadTask, &weak_ref)) {
      auto target = i::HeapObject::cast(weak_ref.target());
      if (!is_live(target)) {
        weak_ref.set_target(i::ReadOnlyRoots(isolate()).undefined_value());
      } else {
        auto forwarded = get_forwarded_ref(weak_ref.target());
        if (forwarded) weak_ref.set_target(*forwarded);
        // The value of the JSWeakRef is alive.
        // i::ObjectSlot slot = weak_ref.RawField(JSWeakRef::kTargetOffset);
        // RecordSlot(weak_ref, slot, target);
      }
    }
    i::WeakCell weak_cell;
    while (weak_objects_.weak_cells.Pop(kMainThreadTask, &weak_cell)) {
      auto gc_notify_updated_slot = [](i::HeapObject object, i::ObjectSlot slot, i::Object target) {
        // if (target.IsHeapObject()) {
        //   RecordSlot(object, slot, HeapObject::cast(target));
        // }
      };
      auto target = i::HeapObject::cast(weak_cell.target());
      if (!is_live(target)) {
        DCHECK(!target.IsUndefined());
        // The value of the WeakCell is dead.
        auto finalization_registry = i::JSFinalizationRegistry::cast(weak_cell.finalization_registry());
        if (!finalization_registry.scheduled_for_cleanup()) {
          heap()->EnqueueDirtyJSFinalizationRegistry(finalization_registry, gc_notify_updated_slot);
        }
        // We're modifying the pointers in WeakCell and JSFinalizationRegistry
        // during GC; thus we need to record the slots it writes. The normal write
        // barrier is not enough, since it's disabled before GC.
        weak_cell.Nullify(isolate(), gc_notify_updated_slot);
        DCHECK(finalization_registry.NeedsCleanup());
        DCHECK(finalization_registry.scheduled_for_cleanup());
      } else {
        DCHECK(!get_forwarded_ref(target));
        // The value of the WeakCell is alive.
        // i::ObjectSlot slot = weak_cell.RawField(WeakCell::kTargetOffset);
        // RecordSlot(weak_cell, slot, HeapObject::cast(*slot));
      }

      auto unregister_token = weak_cell.unregister_token();
      if (!is_live(unregister_token)) {
        // The unregister token is dead. Remove any corresponding entries in the
        // key map. Multiple WeakCell with the same token will have all their
        // unregister_token field set to undefined when processing the first
        // WeakCell. Like above, we're modifying pointers during GC, so record the
        // slots.
        auto undefined = i::ReadOnlyRoots(isolate()).undefined_value();
        auto finalization_registry = i::JSFinalizationRegistry::cast(weak_cell.finalization_registry());
        finalization_registry.RemoveUnregisterToken(
            i::JSReceiver::cast(unregister_token), isolate(),
            [undefined](i::WeakCell matched_cell) {
              matched_cell.set_unregister_token(undefined);
            },
            gc_notify_updated_slot);
      } else {
        DCHECK(!get_forwarded_ref(unregister_token));
        // The unregister_token is alive.
        // ObjectSlot slot = weak_cell.RawField(WeakCell::kUnregisterTokenOffset);
        // RecordSlot(weak_cell, slot, HeapObject::cast(*slot));
      }
    }
    heap()->PostFinalizationRegistryCleanupTaskIfNeeded();
  }

  void MarkDependentCodeForDeoptimization() {
    std::pair<i::HeapObject, i::Code> weak_object_in_code;
    while (weak_objects_.weak_objects_in_code.Pop(kMainThreadTask, &weak_object_in_code)) {
      auto object = weak_object_in_code.first;
      auto code = weak_object_in_code.second;
      if (!is_live(object) && !code.embedded_objects_cleared()) {
        if (!code.marked_for_deoptimization()) {
          code.SetMarkedForDeoptimization("weak objects");
          have_code_to_deoptimize_ = true;
        }
        code.ClearEmbeddedObjects(heap());
        DCHECK(code.embedded_objects_cleared());
      } else if (is_live(object)) {
        DCHECK(!get_forwarded_ref(object));
      }
    }
  }

  bool have_code_to_deoptimize_ = false;

  void Flush() {
    for (int i = 0; i < 8; i++) {
      weak_objects_.transition_arrays.FlushToGlobal(i);
      weak_objects_.ephemeron_hash_tables.FlushToGlobal(i);
      weak_objects_.current_ephemerons.FlushToGlobal(i);
      weak_objects_.next_ephemerons.FlushToGlobal(i);
      weak_objects_.discovered_ephemerons.FlushToGlobal(i);
      weak_objects_.weak_references.FlushToGlobal(i);
      weak_objects_.js_weak_refs.FlushToGlobal(i);
      weak_objects_.weak_cells.FlushToGlobal(i);
      weak_objects_.weak_objects_in_code.FlushToGlobal(i);
      weak_objects_.bytecode_flushing_candidates.FlushToGlobal(i);
      weak_objects_.flushed_js_functions.FlushToGlobal(i);
    }
  }

 public:
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
    Flush();
    have_code_to_deoptimize_ = false;
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
    ClearOldBytecodeCandidates();
    ClearFlushedJsFunctions();
    {
      MMTkWeakObjectRetainer retainer;
      heap()->ProcessAllWeakReferences(&retainer);
    }
    ClearFullMapTransitions();
    ClearWeakReferences();
    ClearWeakCollections();
    ClearJSWeakRefs();

    MarkDependentCodeForDeoptimization();

    DCHECK(weak_objects_.transition_arrays.IsEmpty());
    DCHECK(weak_objects_.weak_references.IsEmpty());
    DCHECK(weak_objects_.weak_objects_in_code.IsEmpty());
    DCHECK(weak_objects_.js_weak_refs.IsEmpty());
    DCHECK(weak_objects_.weak_cells.IsEmpty());
    DCHECK(weak_objects_.bytecode_flushing_candidates.IsEmpty());
    DCHECK(weak_objects_.flushed_js_functions.IsEmpty());

    if (have_code_to_deoptimize_) {
      // Some code objects were marked for deoptimization during the GC.
      i::Deoptimizer::DeoptimizeMarkedCode(isolate());
      have_code_to_deoptimize_ = false;
    }
  }
};

WeakRefs* global_weakref_processor = new WeakRefs();

}

#endif // MMTK_WEAK_REFS_H