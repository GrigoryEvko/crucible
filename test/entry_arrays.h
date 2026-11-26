#pragma once

// The arrays of a traced op, for a test that names only the slot ids.
//
// Each array of a TraceEntry holds the count of its kind, so the inputs take
// their metadata, their producers and their slot ids together, and the
// outputs take their metadata and their slot ids together.  A test of the
// replay or of the pool reads only the slot ids.  These helpers give the
// other arrays of the count as zero values in the arena of the frame of the
// op, because only a store of that frame fills the arrays of the op.

#include <crucible/MerkleDag.h>
#include <fixy/Core.h>

namespace crucible::test {

// A copy of the slot ids in a run of the frame, for a test that keeps the
// slot ids of its ops outside the frame.  Complexity: O(count).
template <class Marker, class Fresh>
[[nodiscard]] DagFrameRun<SlotId, Fresh> slot_run_of(Marker alloc, DagFrame<Fresh>& frame,
                                                     ::fixy::View<SlotId const> slot_ids) noexcept {
    return frame.template build_array<SlotId>(
        alloc, ::fixy::ElementCount{slot_ids.size()}, [slot_ids](::fixy::RunStart at) noexcept {
            return *slot_ids.get(at.value().raw()).expect("a position is below the count of the slot ids");
        });
}

// The inputs of the op that op holds: slot_ids, with the zero metadata and no
// producer for each input, in the arena of the frame.  Complexity: O(count).
template <class Marker, class Fresh, class Holder>
void set_input_slots(Marker alloc, DagFrame<Fresh>& frame, Holder const& op,
                     std::type_identity_t<DagFrameRun<SlotId const, Fresh>> slot_ids) noexcept {
    TraceEntry::set_inputs(frame, op, frame.array(alloc, slot_ids.size(), TensorMeta{}),
                           frame.array(alloc, slot_ids.size(), OpIndex{}), slot_ids);
}

// The outputs of the op that op holds: slot_ids, with the zero metadata for
// each output, in the arena of the frame.  Complexity: O(count).
template <class Marker, class Fresh, class Holder>
void set_output_slots(Marker alloc, DagFrame<Fresh>& frame, Holder const& op,
                      std::type_identity_t<DagFrameRun<SlotId const, Fresh>> slot_ids) noexcept {
    TraceEntry::set_outputs(frame, op, frame.array(alloc, slot_ids.size(), TensorMeta{}), slot_ids);
}

}  // namespace crucible::test
