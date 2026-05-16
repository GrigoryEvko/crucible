#pragma once

// ── crucible::fixy — Channel.h (FIXY-G14 extract) ─────────────────────
//
// Channel types factored out of Flow.h to break the include cycle
// between Flow.h (channels + mint_flow) and theory/Seam.h (channels +
// seam pattern detection that mint_flow consumes).
//
// **Surface (extracted from Flow.h FIXY-G1).**
//
//   fixy::channel::{Identity, Persist, Serialize, Federate, Reshard}
//                          — channel-shape tag types.
//   fixy::ChannelType<Ch>  — concept gating the 5 shapes.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §8 G14 keystone
//   fixy/Flow.h                            — mint_flow factory
//   fixy/theory/Seam.h                     — seam pattern matcher

#include <type_traits>

namespace crucible::fixy::channel {

// Identity — value passes through unchanged.  Used for in-process
// short-circuit composition (producer and consumer share an arena).
struct Identity {
    static constexpr const char* name = "Identity";
};

// Persist — value crosses a durability boundary (Cipher / disk /
// snapshot).  Staleness degrades to Stale<bound>; other axes preserve.
struct Persist {
    static constexpr const char* name = "Persist";
};

// Serialize — value crosses an encoded boundary (wire format,
// process-process IPC).  No semantic transform; identity for grade.
struct Serialize {
    static constexpr const char* name = "Serialize";
};

// Federate — value crosses an organization / trust boundary.  Trust
// degrades; Provenance accumulates External tag.  Vendor pins are
// disjoint (different fleet can't honor producer's vendor).
struct Federate {
    static constexpr const char* name = "Federate";
};

// Reshard — value crosses a region-rearrangement boundary (FSDP
// reshard, fleet resize).  Lifetime::In<X> is rejected — regions
// don't survive reshard.
struct Reshard {
    static constexpr const char* name = "Reshard";
};

}  // namespace crucible::fixy::channel

namespace crucible::fixy {

template <typename Ch>
concept ChannelType =
    std::is_same_v<Ch, channel::Identity>  ||
    std::is_same_v<Ch, channel::Persist>   ||
    std::is_same_v<Ch, channel::Serialize> ||
    std::is_same_v<Ch, channel::Federate>  ||
    std::is_same_v<Ch, channel::Reshard>;

}  // namespace crucible::fixy
