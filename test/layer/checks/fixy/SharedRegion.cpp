// The compile-time checks of fixy/SharedRegion.h.

#include <fixy/SharedRegion.h>

namespace fixy {

namespace detail::shared_region_self_test {

struct probe_tag {
    using permission_row = ::foundation::effects::Row<>;
};
struct io_tag {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

using Fg = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;
using Erased = ::foundation::brand::DefaultBrand;
struct probe_brand {};

// The gate admits a pure tag under a foreground context and refuses an
// IO tag there, which is the effect axis on its own.
static_assert(CtxFitsSharedRead<int, probe_tag, probe_brand, Fg>);
static_assert(!CtxFitsSharedRead<int, probe_tag, Erased, Fg>, "the erased brand pairs any guard with any region");
static_assert(!CtxFitsSharedRead<int, io_tag, probe_brand, Fg>, "a foreground context does not admit an IO region");
static_assert(!CtxFitsSharedRead<int, probe_tag, probe_brand, int>, "a context is a context, not any type");
static_assert(!CtxFitsSharedRead<void, probe_tag, probe_brand, Fg>, "a region of void has no elements to read");

// The read is a View and nothing else: the 16 bytes of the span that it held
// before.  It lends its elements only to a body, and it lives only where its
// mint made it.
using Read = SharedRead<int, probe_tag>;
static_assert(sizeof(Read) == sizeof(::foundation::core::View<int const>) && sizeof(Read) == 16);
static_assert(alignof(Read) == alignof(::foundation::core::View<int const>) && alignof(Read) == alignof(int*));
static_assert(!std::is_default_constructible_v<Read>, "a read nobody minted proves nothing");
static_assert(!std::is_copy_constructible_v<Read> && !std::is_copy_assignable_v<Read>,
              "a copy of a read could outlive the share of its guard");
static_assert(!std::is_move_constructible_v<Read> && !std::is_move_assignable_v<Read>,
              "a move of a read could carry it past the share of its guard");
static_assert(!std::is_trivially_copyable_v<Read> && !::foundation::lifetime::ImplicitLifetimeThroughout<Read>,
              "std::bit_cast and std::start_lifetime_as must not build a read that no share stands behind");
static_assert(std::is_same_v<Read::brand_type, Erased>);

// A shared region holds its address, so it is neither copied nor moved.
//
// The door's claim is spelled at a named brand rather than at the
// erased one.  A region spelled with no brand argument is DefaultBrand,
// which the rule erased-brand counts and does not let a new file add, and
// the claim reads the same at either brand.
using Shared = SharedRegion<int, probe_tag, probe_brand>;
static_assert(!std::is_copy_constructible_v<Shared>);
static_assert(!std::is_move_constructible_v<Shared>);
static_assert(std::is_constructible_v<Shared, OwnedRegion<int, probe_tag, probe_brand>&&>,
              "the door consumes the exclusive");
static_assert(!std::is_constructible_v<Shared, OwnedRegion<int, probe_tag, probe_brand>&>,
              "and consumes it, rather than borrowing");

}  // namespace detail::shared_region_self_test

}  // namespace fixy
