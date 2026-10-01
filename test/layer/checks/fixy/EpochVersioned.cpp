// The compile-time checks of fixy/EpochVersioned.h.

#include <fixy/EpochVersioned.h>

#include <foundation/Lifetime.h>

namespace fixy {

namespace detail::epoch_versioned_self_test {

using EV = EpochVersioned<int>;

static_assert(!std::is_default_constructible_v<EV>, "a version must be stated, never defaulted");
static_assert(std::is_copy_constructible_v<EV>);
static_assert(!std::is_trivially_copyable_v<EV> && !::foundation::lifetime::ImplicitLifetimeThroughout<EV>,
              "a versioned value is not built from bytes, so neither bit_cast nor a checked lifetime start forges "
              "a version");
static_assert(!std::is_constructible_v<EV, int, Epoch, Generation>, "a version comes from a stamp, not from counts");
static_assert(!std::is_constructible_v<EV, int, std::uint64_t, std::uint64_t>, "a raw integer is not a version");

// A stamp is a proof: no caller builds one, and no route builds one from
// bytes.  A source neither copies nor moves.
static_assert(!std::is_default_constructible_v<VersionStamp>);
static_assert(!std::is_constructible_v<VersionStamp, Epoch, Generation>);
static_assert(!std::is_trivially_copyable_v<VersionStamp> && !std::is_implicit_lifetime_v<VersionStamp>);
static_assert(!std::is_copy_constructible_v<VersionSource> && !std::is_move_constructible_v<VersionSource>);
static_assert(!std::is_default_constructible_v<VersionSource>);

// A source is minted with an Init context only.
template <typename Ctx>
concept can_mint_source = requires(Ctx const& ctx) { mint_version_source(ctx); };
static_assert(!can_mint_source<::foundation::effects::detail::ctx_witnesses::FgWitness>);
static_assert(!can_mint_source<::foundation::effects::detail::ctx_witnesses::BgBlockWitness>);
static_assert(!can_mint_source<::foundation::effects::detail::ctx_witnesses::TestWitnessCtx>);
static_assert(can_mint_source<::foundation::effects::detail::ctx_witnesses::InitWitness>);

// The grade is carried per instance: the payload plus two 64-bit
// counters, and no more for a payload that needs no padding.
static_assert(sizeof(EpochVersioned<std::uint64_t>) == 24);

// The payload side of the discipline.  The rule itself is SelfContained,
// whose own self-test holds the cases; these pin that the wrapper uses it.
struct HoldsMutable {
    mutable int cache = 0;
};
struct PointsAtValue {
    int const* target = nullptr;
};
template <typename T>
concept can_version = requires { typename EpochVersioned<T>; };
static_assert(can_version<int>);
static_assert(!can_version<int&> && !can_version<int const&>, "a reference payload is refused");
static_assert(!can_version<int*> && !can_version<int const*>, "the referent of a pointer can change under the version");
static_assert(!can_version<PointsAtValue>, "a pointer member reaches out of the value, even to const");
static_assert(!can_version<HoldsMutable>, "a mutable member is writable through peek()");

inline constexpr EV v_genesis = EV::at_genesis(99);
static_assert(v_genesis.epoch() == EpochLattice::bottom());
static_assert(v_genesis.generation() == GenerationLattice::bottom());
static_assert(!v_genesis.is_at_least(EpochBound{1}, GenerationBound{0}));
static_assert(v_genesis.is_at_least(EpochBound{0}, GenerationBound{0}));

// A payload with no equality compares by its members.  A payload whose
// operator== is hand-written is refused, because that equality can hold
// for values a reader tells apart.
struct NoEquality {
    int v{0};
};
struct HandWrittenEquality {
    int v{0};
    [[nodiscard]] constexpr bool operator==(HandWrittenEquality const&) const noexcept { return true; }
};
template <typename T>
concept can_select_rvalues =
    requires(EpochVersioned<T>&& a, EpochVersioned<T>&& b) { select_fresher(std::move(a), std::move(b)); };
template <typename T>
concept can_select_lvalues = requires(EpochVersioned<T> const& a, EpochVersioned<T> const& b) { select_fresher(a, b); };
static_assert(can_select_rvalues<NoEquality> && can_select_lvalues<NoEquality> && can_select_rvalues<int>);
static_assert(!can_select_rvalues<HandWrittenEquality> && !can_select_lvalues<HandWrittenEquality>);

struct MoveOnly {
    int v{0};
    constexpr explicit MoveOnly(int x) : v{x} {}
    constexpr MoveOnly(MoveOnly&& other) noexcept : v{other.v} { other.v = -1; }
    constexpr MoveOnly& operator=(MoveOnly&& other) noexcept {
        v = other.v;
        other.v = -1;
        return *this;
    }
    MoveOnly(MoveOnly const&) = delete;
    MoveOnly& operator=(MoveOnly const&) = delete;
};

static_assert(!std::is_copy_constructible_v<EpochVersioned<MoveOnly>>);
static_assert(std::is_move_constructible_v<EpochVersioned<MoveOnly>>);
static_assert(!can_select_lvalues<MoveOnly>, "the lvalue form copies, so a move-only payload leaves it");

struct Lookalike {
    using value_type = int;
    using version_t = int;
};

static_assert(IsEpochVersioned<EV>);
static_assert(IsEpochVersioned<EV const&>);
static_assert(!IsEpochVersioned<int>);
static_assert(!IsEpochVersioned<Lookalike>);

static_assert(EV::value_type_name().ends_with("int"));
static_assert(EV::lattice_name() == "Product<L1xL2>");

// The cases that need a source, and so an Init context, run in
// test/fixy/test_versioned_budgeted.cpp and its attack file.

}  // namespace detail::epoch_versioned_self_test

}  // namespace fixy
