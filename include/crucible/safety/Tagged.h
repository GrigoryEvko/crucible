#pragma once

// ── crucible::safety::Tagged<T, Tag> ────────────────────────────────
//
// Phantom-type wrapper attaching a compile-time tag to a value.  Used
// for provenance tracking, trust level, access mode, and schema
// version — all zero-cost type discrimination.
//
//   Axiom coverage: TypeSafe (code_guide §II).
//   Runtime cost:   zero.  sizeof(Tagged<T, Tag>) == sizeof(T).
//
// Tag namespaces provided:
//   source::*  — provenance: FromUser, FromDb, FromConfig, FromInternal,
//                External (untrusted input), Sanitized.
//   trust::*   — verification status: Verified, Tested, Unverified,
//                Assumed, External.
//   access::*  — access mode (register / column / field semantics):
//                RW, RO, WO, W1C, W1S, WriteOnce, AppendOnly, Unique,
//                AutoIncrement, Deprecated.
//   version::* — schema version tagging: V1, V2, V3, ...
//
// Retagging is explicit via .retag<NewTag>().  Unrelated Tagged types
// do not implicitly convert, so a function demanding
// Tagged<T, source::Sanitized> will not accept Tagged<T, source::External>.
//
// Pattern: cross every trust boundary with a source:: tag; every
// verified fact with trust::Verified; every schema-versioned structure
// with version::V<N>.

#include <crucible/Platform.h>

#include <type_traits>
#include <utility>

namespace crucible::safety {

namespace source {
    struct FromUser     {};
    struct FromDb       {};
    struct FromConfig   {};
    struct FromInternal {};
    struct External     {};  // raw untrusted input (network, FFI)
    struct Sanitized    {};  // validated, safe to pass to sanitized-only APIs
    // Durable: loaded from on-disk state (Cipher, config, snapshots).
    // Computed: derived at startup / runtime from Durable + inputs.
    // The pair lets a reader distinguish "this came from disk" from "this
    // is a computation result" at the type level — useful when init code
    // mixes both and a reviewer needs to see which is load-bearing.
    struct Durable      {};
    struct Computed     {};
}

namespace trust {
    struct Verified   {};  // proved by SMT / type system / test
    struct Tested     {};  // covered by tests but not formally verified
    struct Unverified {};  // no formal coverage
    struct Assumed    {};  // axiom / mathematical assumption
    struct External   {};  // trust delegated to outside source
}

namespace access {
    struct RW            {};  // unrestricted
    struct RO            {};  // read-only (writes rejected)
    struct WO            {};  // write-only (reads rejected)
    struct W1C           {};  // write-1-to-clear (HW registers)
    struct W1S           {};  // write-1-to-set (HW registers)
    struct WriteOnce     {};  // written exactly once, then read-only
    struct AppendOnly    {};  // add only, never remove
    struct Unique        {};  // globally unique across instances
    struct AutoIncrement {};  // system-assigned (DB columns)
    struct Deprecated    {};  // accessible but warns about removal
}

namespace version {
    template <unsigned N> struct V { static constexpr unsigned number = N; };
}

// Vessel-boundary provenance: values crossing from Python / PyTorch /
// any foreign runtime carry FromPytorch until validated by Vessel-side
// code, at which point they are retagged to Validated.  Internal paths
// that record / compile / replay require Validated at their entry
// points; FromPytorch cannot substitute for Validated — the type system
// rejects the call.
//
// Internal code (tests, synthetic drivers, replay engines that fabricate
// Entry values) may construct Tagged<T, Validated> directly.  Audit by
// grep for `vessel_trust::Validated` — anything outside of validator
// functions or known-trusted internal constructors is a review concern.
namespace vessel_trust {
    struct FromPytorch {};  // raw uint64_t / pointer / scalar from the FFI
    struct Validated   {};  // Vessel-side validation produced a well-formed value
}

template <typename T, typename Tag>
class [[nodiscard]] Tagged {
    T value_;

public:
    constexpr explicit Tagged(T v)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(v)} {}

    Tagged(const Tagged&)            = default;
    Tagged(Tagged&&)                 = default;
    Tagged& operator=(const Tagged&) = default;
    Tagged& operator=(Tagged&&)      = default;
    ~Tagged()                        = default;

    [[nodiscard]] constexpr const T& value() const noexcept { return value_; }
    [[nodiscard]] constexpr T& value_mut() noexcept { return value_; }

    // Retagging is explicit — produces a new Tagged with a new tag.
    template <typename NewTag>
    [[nodiscard]] constexpr Tagged<T, NewTag> retag() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Tagged<T, NewTag>{std::move(value_)};
    }

    // Underlying-value extraction.  Use for re-wrapping or for known
    // trusted internal paths.
    [[nodiscard]] constexpr T into() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(value_);
    }
};

// Zero-cost guarantee: phantom Tag is template parameter, not a member.
static_assert(sizeof(Tagged<int,   source::FromUser>)  == sizeof(int));
static_assert(sizeof(Tagged<void*, trust::Verified>)   == sizeof(void*));
static_assert(sizeof(Tagged<long,  access::AppendOnly>) == sizeof(long));

} // namespace crucible::safety
