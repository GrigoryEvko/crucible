// The layout note of the two vessel libraries (the note of layout_note.h).
//
// Shared layout
//   The digest folds the layout of each type that the two libraries share by
//   value or through the Vigil, and the name and the type of each C entry of
//   vessel_api.h and dispatch_api.h:
//     - crucible::Vigil, which both libraries compile from its header, and
//       the types that it reaches through a pointer: the ring, the metadata
//       log and a region with its ops
//     - crucible::TensorMeta, the record of a tensor
//     - the four structs of the C ABI that ctypes mirrors: CrucibleMeta,
//       CrucibleDispatchResult, CrucibleSlot and CrucibleBoundScalar
//   A new type that the two libraries share goes into the list of
//   kSharedLayoutDigest.  The level probe keeps its own ABI stamp, because
//   the level of the host of the test launcher reads it too.

#include "layout_note.h"

#include "dispatch_api.h"
#include "vessel_api.h"

#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Vigil.h>
#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace crucible::vessel {
namespace {

constexpr std::uint64_t kSharedLayoutDigest = ::foundation::reflect::combine_ids(
    ::foundation::reflect::layout_digest_of<crucible::Vigil, crucible::TraceRing, crucible::MetaLog,
                                            crucible::RegionNode, crucible::TraceEntry, crucible::TensorMeta,
                                            CrucibleMeta, CrucibleDispatchResult, CrucibleSlot, CrucibleBoundScalar>(),
    ::foundation::reflect::c_entry_digest<^^::>("crucible_"));

[[gnu::section(".note.crucible"), gnu::used, gnu::retain]] alignas(kLayoutNoteAlignment) constinit LayoutNote const
    kLayoutNoteOfLibrary = layout_note_of(kSharedLayoutDigest);

}  // namespace

std::uint64_t library_layout_digest() noexcept { return kSharedLayoutDigest; }

}  // namespace crucible::vessel
