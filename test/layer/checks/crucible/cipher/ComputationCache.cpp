// The compile-time checks of crucible/cipher/ComputationCache.h.

#include <crucible/cipher/ComputationCache.h>

namespace crucible::cipher {

namespace detail::computation_cache_self_test {

inline int p_returning(int) noexcept { return 0; }

static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
              != ::crucible::cipher::computation_cache_key<&p_binary, int, double>);

static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
              != ::crucible::cipher::computation_cache_key<&p_unary, float>);
static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
              != ::crucible::cipher::computation_cache_key<&p_unary, double>);

static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
              == ::crucible::cipher::computation_cache_key<&p_unary, int>);

static_assert(::crucible::cipher::computation_cache_key<&p_binary, int, double>
              != ::crucible::cipher::computation_cache_key<&p_binary, double, int>);

static_assert(::crucible::cipher::computation_cache_key<&p_unary>
              != ::crucible::cipher::computation_cache_key<&p_binary>);

namespace fnames_collision_check {
inline void s_fn_one(int) noexcept {}
inline void s_fn_two(int) noexcept {}
static_assert(::crucible::cipher::computation_cache_key<&s_fn_one>
                  != ::crucible::cipher::computation_cache_key<&s_fn_two>,
              "computation_cache_key MUST distinguish same-signature "
              "different-name functions; otherwise federation aliases "
              "unrelated compiled bodies on the wire.");
static_assert(::crucible::cipher::computation_cache_key<&s_fn_one, int>
              != ::crucible::cipher::computation_cache_key<&s_fn_two, int>);
}  // namespace fnames_collision_check

static_assert(::crucible::cipher::computation_cache_key<&p_unary, int> != 0);
static_assert(::crucible::cipher::computation_cache_key<&p_binary, int, double> != 0);

static_assert(::crucible::cipher::computation_cache_key<&p_void> != 0);
static_assert(::crucible::cipher::computation_cache_key<&p_void>
              != ::crucible::cipher::computation_cache_key<&p_unary>);
static_assert(::crucible::cipher::computation_cache_key<&p_unary>
              != ::crucible::cipher::computation_cache_key<&p_unary, int>);

static_assert(::crucible::cipher::computation_cache_key<&p_throwing, int>
                  != ::crucible::cipher::computation_cache_key<&p_noexcept, int>,
              "noexcept-vs-throwing function-pointer types MUST "
              "produce different cache keys; otherwise the "
              "dispatcher can dispatch a maybe-throwing compiled "
              "body through a noexcept caller and break the "
              "noexcept guarantee.");

static_assert(::crucible::cipher::IsCacheableFunction<&p_unary>);
static_assert(::crucible::cipher::IsCacheableFunction<&p_binary>);
static_assert(::crucible::cipher::IsCacheableFunction<&p_returning>);

inline int s_data_global = 0;
static_assert(!::crucible::cipher::IsCacheableFunction<42>);
static_assert(!::crucible::cipher::IsCacheableFunction<&s_data_global>);

// The literal is refused because its type is not a pointer type at
// all, which is why the constraint needs no comparison of its own.
static_assert(!::crucible::cipher::IsCacheableFunction<nullptr>);

static_assert(::crucible::cipher::IsEffectRow<::foundation::effects::Row<>>);
static_assert(::crucible::cipher::IsEffectRow<::foundation::effects::Row<::foundation::effects::Effect::Bg>>);
static_assert(::crucible::cipher::IsEffectRow<
              ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO>>);

// A caller who reaches for the alias instead of the bare row must
// land in the same slot. Wrapping the alias in anything other than a
// plain alias would break that, and would break here first.
static_assert(::crucible::cipher::IsEffectRow<::foundation::effects::EmptyRow>);
static_assert(::crucible::cipher::computation_cache_key_in_row<&p_unary, ::foundation::effects::EmptyRow, int>
                  == ::crucible::cipher::computation_cache_key_in_row<&p_unary, ::foundation::effects::Row<>, int>,
              "EmptyRow and Row<> must hash to the same key — the alias is "
              "transparent through the cache.");

static_assert(!::crucible::cipher::IsEffectRow<int>);
static_assert(!::crucible::cipher::IsEffectRow<void>);
static_assert(!::crucible::cipher::IsEffectRow<::foundation::effects::Effect>);
static_assert(!::crucible::cipher::IsEffectRow<::foundation::effects::Row<> const>,
              "a qualified row folds to zero, so the fence refuses it.");
static_assert(!::crucible::cipher::IsEffectRow<::foundation::effects::Row<>&>);
static_assert(!::crucible::cipher::IsEffectRow<::crucible::cipher::detail::RowBlind>,
              "no row-aware call may name the row-blind family's slots.");

static_assert(::crucible::cipher::computation_cache_key_in_row<&p_unary, ::foundation::effects::Row<>, int>
                  != ::crucible::cipher::computation_cache_key_in_row<
                      &p_unary, ::foundation::effects::Row<::foundation::effects::Effect::Bg>, int>,
              "the row-aware cache must key the same function and arguments "
              "under different rows to different slots.");

static_assert(
    ::crucible::cipher::computation_cache_key_in_row<&p_unary, ::foundation::effects::Row<>, int>
    != ::crucible::cipher::computation_cache_key_in_row<&p_binary, ::foundation::effects::Row<>, int, double>);

static_assert(::crucible::cipher::computation_cache_key_in_row<&p_unary, ::foundation::effects::Row<>, int>
              == ::crucible::cipher::computation_cache_key_in_row<&p_unary, ::foundation::effects::Row<>, int>);

static_assert(
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary, ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO>, int>
        == ::crucible::cipher::computation_cache_key_in_row<
            &p_unary, ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Bg>,
            int>,
    "the row-aware cache key must not change when the effect pack is "
    "reordered, because the row hash sorts before it folds.");

static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
                  != ::crucible::cipher::computation_cache_key_in_row<&p_unary, ::foundation::effects::Row<>, int>,
              "the row-aware key must differ from the row-blind key even for "
              "an empty row, or the two families would alias each other's "
              "compiled bodies.");

static_assert(::crucible::cipher::computation_cache_key_in_row<&p_void, ::foundation::effects::Row<>> != 0);
static_assert(::crucible::cipher::computation_cache_key_in_row<&p_void, ::foundation::effects::Row<>>
              != ::crucible::cipher::computation_cache_key<&p_void>);

static_assert(std::atomic<::crucible::cipher::CompiledBody*>::is_always_lock_free,
              "ComputationCache slot must be lock-free; a library that fell "
              "back to a lock for this pointer width would put a lock on "
              "every lookup.");

static_assert(sizeof(std::atomic<::crucible::cipher::CompiledBody*>) == sizeof(::crucible::cipher::CompiledBody*),
              "ComputationCache slot must be exactly pointer-sized; a wider "
              "atomic inflates the per-instantiation cost in silence.");

}  // namespace detail::computation_cache_self_test

}  // namespace crucible::cipher
