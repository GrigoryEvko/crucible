#pragma once

// The arrays of a traced op, for a test that names only the slot ids.
//
// Each array of a TraceEntry holds the count of its kind, so the inputs take
// their metadata, their producers and their slot ids together, and the
// outputs take their metadata and their slot ids together.  A test of the
// replay or of the pool reads only the slot ids.  These helpers give the
// other arrays of the count as zero values in the arena of the test.  The
// arena is a BumpArena of any brand, or the Epoch of an epoch arena.

#include <crucible/MerkleDag.h>
#include <fixy/Core.h>

#include <cstddef>

namespace crucible::test {

// An arena that gives an array of op indexes: a BumpArena or an Epoch.
template <class Arena, class Marker>
concept OpArrayArena = requires(Arena& arena, Marker alloc) { arena.array(alloc, std::size_t{1}, OpIndex{}); };

// The inputs of op: slot_ids, with the zero metadata and no producer for
// each input.  Complexity: O(count).
template <class Marker, class Arena>
    requires OpArrayArena<Arena, Marker>
void set_input_slots(Marker alloc, Arena& arena, TraceEntry& op, ::fixy::View<SlotId> slot_ids) noexcept {
    auto const metas = arena.array(alloc, slot_ids.size(), TensorMeta{});
    auto const producers = arena.array(alloc, slot_ids.size(), OpIndex{});
    // A plain View of an arena run: the View carries no brand of its arena.
    op.set_inputs(metas.view(), producers.view(), slot_ids);
}

// The outputs of op: slot_ids, with the zero metadata for each output.
// Complexity: O(count).
template <class Marker, class Arena>
    requires OpArrayArena<Arena, Marker>
void set_output_slots(Marker alloc, Arena& arena, TraceEntry& op, ::fixy::View<SlotId> slot_ids) noexcept {
    auto const metas = arena.array(alloc, slot_ids.size(), TensorMeta{});
    // A plain View of an arena run: the View carries no brand of its arena.
    op.set_outputs(metas.view(), slot_ids);
}

}  // namespace crucible::test
