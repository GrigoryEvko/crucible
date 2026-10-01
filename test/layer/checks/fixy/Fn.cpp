// The compile-time checks of fixy/Fn.h.

#include <fixy/Fn.h>

namespace fixy {

namespace detail::fn_self_test {

// The shortest honest binding.  It names no axis and means strictest on
// every one of them.
static_assert(sizeof(fn<int>) == sizeof(int), "the wrapper adds no storage");
static_assert(alignof(fn<int>) == alignof(int));
static_assert(std::is_trivially_copyable_v<fn<int>>);
static_assert(fn<int>::atom_count == 0);

// The payload is its own grade on the Type axis, because no default
// function type exists.
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Type>, int>);
static_assert(std::is_same_v<fn<double>::grade_on<Axis::Type>, double>);

// An unmentioned axis takes its strict pole.  A dozen axes, drawn from
// each shape the table carries, rather than one.
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Usage>, typename axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Effect>, typename axis_traits<Axis::Effect>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Refinement>, typename axis_traits<Axis::Refinement>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Security>, typename axis_traits<Axis::Security>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Mutation>, typename axis_traits<Axis::Mutation>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Reentrancy>, typename axis_traits<Axis::Reentrancy>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Regime>, typename axis_traits<Axis::Regime>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::MemoryScope>, typename axis_traits<Axis::MemoryScope>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::SimdIsa>, typename axis_traits<Axis::SimdIsa>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Stdio>, typename axis_traits<Axis::Stdio>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Version>, typename axis_traits<Axis::Version>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Staleness>, typename axis_traits<Axis::Staleness>::strict>);

// Every axis resolves, for the empty pack and for a pack that mentions
// one axis.  A sample of twelve proves twelve; this proves the table.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
template <class Binding>
[[nodiscard]] consteval bool every_axis_resolves() noexcept {
    bool all_resolved = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^Axis))) {
        constexpr Axis axis = [:axis_member:];
        all_resolved = all_resolved && !std::is_void_v<typename Binding::template grade_on<axis>>;
    }
    return all_resolved;
}
#pragma GCC diagnostic pop

static_assert(every_axis_resolves<fn<int>>());
static_assert(every_axis_resolves<fn<int, atom::copy>>());

// One atom relaxes exactly its own axis and leaves the rest strict.
using CopyBinding = fn<int, atom::copy>;
static_assert(std::is_same_v<CopyBinding::grade_on<Axis::Usage>, atom::copy>);
static_assert(!std::is_same_v<CopyBinding::grade_on<Axis::Usage>, typename axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<CopyBinding::grade_on<Axis::Effect>, typename axis_traits<Axis::Effect>::strict>);
static_assert(std::is_same_v<CopyBinding::grade_on<Axis::Type>, int>);
static_assert(CopyBinding::mentions_axis<Axis::Usage>);
static_assert(!CopyBinding::mentions_axis<Axis::Effect>);
static_assert(CopyBinding::atom_count == 1);

// Atoms on different axes do not interfere.
using MixedBinding = fn<int, atom::affine, atom::mut_append, atom::reentrant>;
static_assert(std::is_same_v<MixedBinding::grade_on<Axis::Usage>, atom::affine>);
static_assert(std::is_same_v<MixedBinding::grade_on<Axis::Mutation>, atom::mut_append>);
static_assert(std::is_same_v<MixedBinding::grade_on<Axis::Reentrancy>, atom::reentrant>);
static_assert(std::is_same_v<MixedBinding::grade_on<Axis::Security>, typename axis_traits<Axis::Security>::strict>);
static_assert(MixedBinding::atom_count == 3);

// The gate refuses what it should, at the concept rather than only in
// the class body, so a mint call reports at the call site.
struct not_an_atom {};
static_assert(IsAccepted<int>);
static_assert(IsAccepted<int, atom::copy>);
static_assert(IsAccepted<int, atom::copy, atom::mut_append>);
static_assert(!IsAccepted<int, atom::copy, atom::affine>, "two atoms on the Usage axis");
static_assert(!IsAccepted<int, not_an_atom>);
static_assert(!IsAccepted<void, atom::copy>);
static_assert(!IsAccepted<int&>);

// The duplicate diagnostic names the axis rather than the atoms, which
// is what a reader needs to fix it.
static_assert(std::is_same_v<duplicate_tag_or_void_t<atom::copy, atom::affine>, duplicate_atom_on<Axis::Usage>>);
static_assert(duplicate_tag_or_void_t<atom::copy, atom::affine>::name == "DuplicateAtomOnUsage");
static_assert(std::is_same_v<duplicate_tag_or_void_t<atom::copy, atom::mut_append>, void>);

// The door carries the value through and the type records the pack.
[[nodiscard]] consteval bool door_carries_the_value() noexcept {
    const auto bound = mint_fn<int, atom::copy>(42);
    return bound.value() == 42;
}
static_assert(door_carries_the_value());
static_assert(std::is_same_v<decltype(mint_fn<int, atom::copy>(0)), fn<int, atom::copy>>);

// The value constructor is not a public door.  A caller can still
// default-construct, which carries no authority, but cannot wrap a
// value without naming a mint.
static_assert(!std::is_constructible_v<fn<int, atom::copy>, int>,
              "the value constructor is private; mint_fn is the only door");
static_assert(std::is_default_constructible_v<fn<int, atom::copy>>);

static_assert(is_fn_v<fn<int>>);
static_assert(is_fn_v<const fn<int, atom::copy>&>);
static_assert(!is_fn_v<int>);

// ---------------------------------------------------------------------
// What the fold over the axes owes the federation cache.

namespace fd_ = ::foundation::diag;
namespace fe_ = ::foundation::effects;

// A binding is off the zero slot, which is where the primary template
// leaves anything it does not recognise.  A binding that landed there
// would share one slot with every bare type in the tree.
static_assert(fd_::row_hash_contribution_v<fn<int>> != 0);
static_assert(fd_::row_hash_contribution_v<int> == 0);
static_assert(fd_::row_hash_contribution_v<fn<int>> != fd_::row_hash_contribution_v<int>);

// The binding does not satisfy the graded fold's shape, and must not: it
// publishes a grade per axis rather than one lattice and one modality.
// This is the assertion that would red if someone gave it a synthetic
// product lattice to make the generic fold apply.
static_assert(!fd_::GradedShaped<fn<int>>);
static_assert(!fd_::GradedShaped<fn<int, atom::copy>>);

// One atom on one axis moves the key, and two grades on one axis take
// separate slots.  Between them these say the fold reads the resolved
// grade and not merely the pack's length.
static_assert(fd_::row_hash_contribution_v<fn<int, atom::copy>> != fd_::row_hash_contribution_v<fn<int>>);
static_assert(fd_::row_hash_contribution_v<fn<int, atom::affine>> != fd_::row_hash_contribution_v<fn<int, atom::copy>>);
static_assert(fd_::row_hash_contribution_v<fn<int, atom::affine, atom::mut_append>>
              != fd_::row_hash_contribution_v<fn<int, atom::affine>>);

// The Effect axis folds the row, so the key is a function of the effect
// SET.  These two spellings are distinct types naming one set, and one
// slot for both is the whole reason the axis does not fold its grade's
// type.
static_assert(
    fd_::row_hash_contribution_v<fn<int, atom::with<fe_::Effect::Bg, fe_::Effect::Alloc>, atom::as_public>>
        == fd_::row_hash_contribution_v<fn<int, atom::with<fe_::Effect::Alloc, fe_::Effect::Bg>, atom::as_public>>,
    "the Effect axis must fold as a set: two orderings of one row are one discipline and "
    "belong in one slot");

// A stated empty row and the strict pole it leaves behind are the same
// claim, which fixy/Atom.h says of them, so they take one slot.  This is
// also where the fold states that it ignores whether the pack MENTIONED
// an axis: one of these two mentions Effect and the other does not, and
// their resolved grades agree.
static_assert(fn<int, atom::with<>>::mentions_axis<Axis::Effect>);
static_assert(!fn<int>::mentions_axis<Axis::Effect>);
static_assert(fd_::row_hash_contribution_v<fn<int, atom::with<>>> == fd_::row_hash_contribution_v<fn<int>>,
              "an axis mentioned and an axis defaulted to the same grade are one discipline; "
              "mentioning must not split the slot");

// The key is blind to a bare payload, deliberately.  Payload identity
// belongs to the content hash, the other half of a cache key, and the
// Type axis recurses so that a payload which does carry a row reaches the
// key instead of being dropped.
static_assert(fd_::row_hash_contribution_v<fn<int>> == fd_::row_hash_contribution_v<fn<double>>,
              "the row hash discriminates disciplines, not payload types");

// A parametric atom is discriminated by its argument, so an emission
// under one policy does not reuse the slot of an emission under another.
static_assert(fd_::row_hash_contribution_v<fn<int, atom::declassify<tags::secret_policy::WireSerialize>>>
                  != fd_::row_hash_contribution_v<fn<int, atom::declassify<tags::secret_policy::AuditedLogging>>>,
              "a declassification's policy is part of its identity");

// The row a binding requires joins its Effect grade with the lifts of its
// atoms.  A futex call requires Block although the Effect grade stays at
// the strict pole.
using FutexBinding = fn<int, atom::syscall::per<atom::syscall::SyscallId::futex>>;
using StatedBlockBinding = fn<int, atom::with<fe_::Effect::Block>>;
static_assert(std::is_same_v<FutexBinding::grade_on<Axis::Effect>, typename axis_traits<Axis::Effect>::strict>);
static_assert(std::is_same_v<binding_row_t<FutexBinding>, fe_::Row<fe_::Effect::Block>>);
static_assert(std::is_same_v<binding_row_t<fn<int>>, fe_::Row<>>);

// The key folds the grade on each axis, and not the row a binding
// requires.  These two bindings require one row and state it on two
// different axes, so they take two slots.
static_assert(std::is_same_v<binding_row_t<FutexBinding>, binding_row_t<StatedBlockBinding>>);
static_assert(fd_::row_hash_contribution_v<FutexBinding> != fd_::row_hash_contribution_v<StatedBlockBinding>,
              "the key folds the axis grades, and the row of the binding is not one of them");

}  // namespace detail::fn_self_test

}  // namespace fixy
