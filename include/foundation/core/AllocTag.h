#pragma once

// The allocation capability Alloc, and the gate of each allocation door of
// the base.  foundation/effects/Effect.h builds the tag into its contexts and
// includes only this header, so a header that reads a context does not read
// the allocation door of foundation/core/Heap.h or the Layout of
// foundation/alloc/Layout.h.

#include <concepts>

// The owner of the bare value atoms.  foundation/effects/Effect.h defines it,
// and the rule home admits its definition only in that header.
namespace foundation::effects::host {
struct AtomOwner;
}  // namespace foundation::effects::host

namespace foundation::core {

// The allocation capability: the tag that shows an allocation at a call
// site.  foundation/effects gives it the name cap::Alloc with an alias, and
// the contexts Bg, Init and Test hold it, so ::foundation::effects::Alloc
// and bg.alloc name this one type.  The tag is empty and costs nothing.
//
// The tag is an authority, so no code makes one from nothing.  The default
// constructor is private, and its one friend is the owner of the bare value
// atoms in foundation/effects/Effect.h.  That owner builds the tag into a
// context and into the trade of a spent Capability for its bare tag.  So a
// holder of a tag got it from a context, or from a capability that a context
// minted.  The copy stays public, so a holder can pass the tag along.
//
// Each constructor is user-provided, and not defaulted.  A tag with a
// trivial copy is trivially copyable, and std::bit_cast then builds one from
// any byte.  A tag with a trivial constructor is an implicit-lifetime type,
// and std::start_lifetime_as then builds one over a buffer.  Neither route
// meets the access check.  The explicit noexcept on each special member
// turns a future throwing body into a compile error.
class Alloc {
public:
    constexpr Alloc(Alloc const&) noexcept {}
    constexpr Alloc& operator=(Alloc const&) noexcept { return *this; }
    ~Alloc() = default;

private:
    constexpr Alloc() noexcept {}

    friend struct ::foundation::effects::host::AtomOwner;
};

// The gate of each allocation door of the base: the tag is Alloc and no
// other type.  A tag of a different name does not pass, so each allocation
// shows the word Alloc at its call site.
template <class Marker>
concept AllocationCapability = std::same_as<Marker, Alloc>;

}  // namespace foundation::core
