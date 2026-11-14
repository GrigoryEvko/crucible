#pragma once

// The arrays of a traced op, for a test that names only the slot ids.
//
// Each array of a TraceEntry holds the count of its kind, so the inputs take
// their metadata, their producers and their slot ids together, and the
// outputs take their metadata and their slot ids together.  A test of the
// replay or of the pool reads only the slot ids.  These helpers give the
// other arrays of the count as zero values in the arena of the test.

#include <crucible/MerkleDag.h>
#include <fixy/Core.h>

namespace crucible::test {

// The inputs of op: slot_ids, with the zero metadata and no producer for
// each input.  Complexity: O(count).
template <class Marker>
void set_input_slots(Marker alloc, ::fixy::BumpArena& arena, TraceEntry& op, ::fixy::View<SlotId> slot_ids) noexcept {
    op.set_inputs(arena.array(alloc, slot_ids.size(), TensorMeta{}), arena.array(alloc, slot_ids.size(), OpIndex{}),
                  slot_ids);
}

// The outputs of op: slot_ids, with the zero metadata for each output.
// Complexity: O(count).
template <class Marker>
void set_output_slots(Marker alloc, ::fixy::BumpArena& arena, TraceEntry& op, ::fixy::View<SlotId> slot_ids) noexcept {
    op.set_outputs(arena.array(alloc, slot_ids.size(), TensorMeta{}), slot_ids);
}

}  // namespace crucible::test
