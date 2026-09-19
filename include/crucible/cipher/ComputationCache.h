#pragma once

// Storage is one atomic variable per template instantiation. It is
// inline, so the linker folds it and every translation unit that
// instantiates the same arguments reads the same atomic.
//
// The instantiation is the key, which is what makes a collision
// impossible: two slots cannot alias unless they name the same
// function and the same argument types, and then they should. The
// price is that nothing can enumerate the slots, so nothing can
// evict them.
//
// The key is stable bit for bit within one build and no further.
// Reflection renders a name in an implementation-specific way, so a
// key computed under a different toolchain is a different key for the
// same computation. Two peers that exchange keys must either share a
// toolchain or fold a discriminator for it into the key.

#include <crucible/safety/diag/_StableName.h>
#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/effects/_EffectRow.h>

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <meta>

namespace crucible::cipher {

// This type is never defined here and never dereferenced. A consumer
// defines it in its own translation unit and casts the stored pointer
// back at lookup.

struct CompiledBody;

// Unconstrained, the parameter would take any structural value at
// all: an integer, a pointer to a global, a pointer to a member
// function. Each of those compiles into a slot that means nothing. A
// pointer to a member function is refused in particular because it
// needs an object argument that the key cannot represent.
//
// The constraint deliberately omits a comparison against nullptr,
// because such a comparison on a function-pointer template argument
// is not reliably a constant expression. The structural check is what
// carries the weight, and a null function pointer has to be cast into
// existence on purpose.

template <auto FnPtr>
concept IsCacheableFunction =
    std::is_pointer_v<decltype(FnPtr)> && std::is_function_v<std::remove_pointer_t<decltype(FnPtr)>>;

// Unfenced, the row parameter would take any type. A type that is not
// a row folds to a zero contribution, which silently produces a key
// indistinguishable from the row-blind one. And a caller who meant
// the first argument type would find it bound to the row position
// instead, with no diagnostic.

namespace detail {

template <typename R>
inline constexpr bool is_effect_row_v = false;

template <::crucible::effects::Effect... Es>
inline constexpr bool is_effect_row_v<::crucible::effects::Row<Es...>> = true;

}  // namespace detail

template <typename R>
concept IsEffectRow = detail::is_effect_row_v<R>;

namespace detail {

// The seed folds the function's name and its type, and the name is
// what does the real work. The type-derived identifier alone hashes
// the signature, so two different functions of the same signature
// collide on it. The storage slot does not care, because it is keyed
// on the template argument itself, but a key that travels does: two
// unrelated compiled bodies would meet in one slot on the far side.
// The type is folded in as well, so that two functions of the same
// name in different scopes stay apart.
template <auto FnPtr, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr>
[[nodiscard]] consteval std::uint64_t computation_cache_key_impl() noexcept {
    std::uint64_t k =
        ::crucible::safety::diag::detail::hash_name(std::meta::display_string_of(std::meta::reflect_constant(FnPtr)));
    k = ::crucible::safety::diag::detail::combine_ids(k, ::crucible::safety::diag::stable_function_id<FnPtr>);
    ((k = ::crucible::safety::diag::detail::combine_ids(k, ::crucible::safety::diag::stable_type_id<Args>)), ...);
    return k;
}

}  // namespace detail

template <auto FnPtr, typename... Args>
    requires IsCacheableFunction<FnPtr>
inline constexpr std::uint64_t computation_cache_key = detail::computation_cache_key_impl<FnPtr, Args...>();

namespace detail {

template <auto FnPtr, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr>
inline std::atomic<CompiledBody*> compiled_body_slot{nullptr};

}  // namespace detail

template <auto FnPtr, typename... Args>
    requires IsCacheableFunction<FnPtr>
[[nodiscard]] CompiledBody* lookup_computation_cache() noexcept {
    return detail::compiled_body_slot<FnPtr, Args...>.load(std::memory_order_acquire);
}

// A null body would be indistinguishable from a miss, so it is
// refused. The first writer wins and a later one is discarded in
// silence, which means a caller that needs to know what is actually
// cached looks it up again afterwards.

template <auto FnPtr, typename... Args>
    requires IsCacheableFunction<FnPtr>
void insert_computation_cache(CompiledBody* body) noexcept pre(body != nullptr) {
    CompiledBody* expected = nullptr;
    detail::compiled_body_slot<FnPtr, Args...>.compare_exchange_strong(expected, body, std::memory_order_acq_rel,
                                                                       std::memory_order_acquire);
}

// The row-aware calls sit beside the row-blind ones rather than
// replacing them. Threading the row through the existing names is not
// available: a new parameter without a default breaks every call
// site, and one with a default silently re-reads an existing call,
// binding the first argument type to the row position. So the two
// families stay separate, and their slots never alias, not even for
// an empty row.

namespace detail {

template <auto FnPtr, typename Row, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr> && ::crucible::cipher::IsEffectRow<Row>
[[nodiscard]] consteval std::uint64_t computation_cache_key_in_row_impl() noexcept {
    std::uint64_t k =
        ::crucible::safety::diag::detail::hash_name(std::meta::display_string_of(std::meta::reflect_constant(FnPtr)));
    k = ::crucible::safety::diag::detail::combine_ids(k, ::crucible::safety::diag::stable_function_id<FnPtr>);
    // The combiner is order-sensitive, so this fold differs from the
    // row-blind one by the position of this step alone. Even a row
    // contributing zero would still key elsewhere.
    k = ::crucible::safety::diag::detail::combine_ids(k, ::crucible::safety::diag::row_hash_contribution_v<Row>);
    ((k = ::crucible::safety::diag::detail::combine_ids(k, ::crucible::safety::diag::stable_type_id<Args>)), ...);
    return k;
}

}  // namespace detail

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
inline constexpr std::uint64_t computation_cache_key_in_row =
    detail::computation_cache_key_in_row_impl<FnPtr, Row, Args...>();

namespace detail {

template <auto FnPtr, typename Row, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr> && ::crucible::cipher::IsEffectRow<Row>
inline std::atomic<CompiledBody*> compiled_body_slot_in_row{nullptr};

}  // namespace detail

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
[[nodiscard]] CompiledBody* lookup_computation_cache_in_row() noexcept {
    return detail::compiled_body_slot_in_row<FnPtr, Row, Args...>.load(std::memory_order_acquire);
}

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
void insert_computation_cache_in_row(CompiledBody* body) noexcept pre(body != nullptr) {
    CompiledBody* expected = nullptr;
    detail::compiled_body_slot_in_row<FnPtr, Row, Args...>.compare_exchange_strong(
        expected, body, std::memory_order_acq_rel, std::memory_order_acquire);
}

// This evicts nothing. A slot lives as long as the program does, and
// there is no registry through which to reach one.

inline void drain_computation_cache([[maybe_unused]] std::chrono::seconds max_age) noexcept {}

namespace detail::computation_cache_self_test {

inline void p_unary(int) noexcept {}
inline void p_binary(int, double) noexcept {}
inline int p_returning(int) noexcept { return 0; }

inline void p_void() noexcept {}

inline void p_throwing(int) {}
inline void p_noexcept(int) noexcept {}

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

static_assert(::crucible::cipher::IsEffectRow<::crucible::effects::Row<>>);
static_assert(::crucible::cipher::IsEffectRow<::crucible::effects::Row<::crucible::effects::Effect::Bg>>);
static_assert(::crucible::cipher::IsEffectRow<
              ::crucible::effects::Row<::crucible::effects::Effect::Bg, ::crucible::effects::Effect::IO>>);

// A caller who reaches for the alias instead of the bare row must
// land in the same slot. Wrapping the alias in anything other than a
// plain alias would break that, and would break here first.
static_assert(::crucible::cipher::IsEffectRow<::crucible::effects::EmptyRow>);
static_assert(::crucible::cipher::computation_cache_key_in_row<&p_unary, ::crucible::effects::EmptyRow, int>
                  == ::crucible::cipher::computation_cache_key_in_row<&p_unary, ::crucible::effects::Row<>, int>,
              "EmptyRow and Row<> must hash to the same key — the alias is "
              "transparent through the cache.");

static_assert(!::crucible::cipher::IsEffectRow<int>);
static_assert(!::crucible::cipher::IsEffectRow<void>);
static_assert(!::crucible::cipher::IsEffectRow<::crucible::effects::Effect>);

static_assert(::crucible::cipher::computation_cache_key_in_row<&p_unary, ::crucible::effects::Row<>, int>
                  != ::crucible::cipher::computation_cache_key_in_row<
                      &p_unary, ::crucible::effects::Row<::crucible::effects::Effect::Bg>, int>,
              "the row-aware cache must key the same function and arguments "
              "under different rows to different slots.");

static_assert(::crucible::cipher::computation_cache_key_in_row<&p_unary, ::crucible::effects::Row<>, int>
              != ::crucible::cipher::computation_cache_key_in_row<&p_binary, ::crucible::effects::Row<>, int, double>);

static_assert(::crucible::cipher::computation_cache_key_in_row<&p_unary, ::crucible::effects::Row<>, int>
              == ::crucible::cipher::computation_cache_key_in_row<&p_unary, ::crucible::effects::Row<>, int>);

static_assert(
    ::crucible::cipher::computation_cache_key_in_row<
        &p_unary, ::crucible::effects::Row<::crucible::effects::Effect::Bg, ::crucible::effects::Effect::IO>, int>
        == ::crucible::cipher::computation_cache_key_in_row<
            &p_unary, ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Bg>, int>,
    "the row-aware cache key must not change when the effect pack is "
    "reordered, because the row hash sorts before it folds.");

static_assert(::crucible::cipher::computation_cache_key<&p_unary, int>
                  != ::crucible::cipher::computation_cache_key_in_row<&p_unary, ::crucible::effects::Row<>, int>,
              "the row-aware key must differ from the row-blind key even for "
              "an empty row, or the two families would alias each other's "
              "compiled bodies.");

static_assert(::crucible::cipher::computation_cache_key_in_row<&p_void, ::crucible::effects::Row<>> != 0);
static_assert(::crucible::cipher::computation_cache_key_in_row<&p_void, ::crucible::effects::Row<>>
              != ::crucible::cipher::computation_cache_key<&p_void>);

static_assert(std::atomic<::crucible::cipher::CompiledBody*>::is_always_lock_free,
              "ComputationCache slot must be lock-free; a library that fell "
              "back to a lock for this pointer width would put a lock on "
              "every lookup.");

static_assert(sizeof(std::atomic<::crucible::cipher::CompiledBody*>) == sizeof(::crucible::cipher::CompiledBody*),
              "ComputationCache slot must be exactly pointer-sized; a wider "
              "atomic inflates the per-instantiation cost in silence.");

}  // namespace detail::computation_cache_self_test

// This runs once per process and refuses to run twice. The slots it
// reads are program-lifetime globals, so the checks that a lookup
// misses before its insert hold on the first call only. A second call
// would find those slots already full, so it reports failure rather
// than let a miss quietly become a hit.

inline bool computation_cache_smoke_test() noexcept {
    using namespace detail::computation_cache_self_test;

    static std::atomic<int> call_counter{0};
    if (call_counter.fetch_add(1, std::memory_order_relaxed) > 0) {
        return false;
    }

    // These addresses are never dereferenced. The cache holds a
    // pointer opaquely, so all the test needs is distinct non-null
    // bit patterns.
    auto* body_a = std::bit_cast<CompiledBody*>(static_cast<std::uintptr_t>(0x1));
    auto* body_b = std::bit_cast<CompiledBody*>(static_cast<std::uintptr_t>(0x2));

    bool ok = true;

    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>() == nullptr);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, float>() == nullptr);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_binary, int, double>() == nullptr);

    ::crucible::cipher::insert_computation_cache<&p_unary, int>(body_a);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>() == body_a);

    ::crucible::cipher::insert_computation_cache<&p_unary, int>(body_b);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>() == body_a);

    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, float>() == nullptr);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_binary, int, double>() == nullptr);

    ::crucible::cipher::drain_computation_cache(std::chrono::seconds{0});
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>() == body_a);

    // An empty argument pack must still reach a slot of its own, and
    // must still instantiate as a template rather than collapse onto
    // a plain function.
    auto* body_c = std::bit_cast<CompiledBody*>(static_cast<std::uintptr_t>(0x3));
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_void>() == nullptr);
    ::crucible::cipher::insert_computation_cache<&p_void>(body_c);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_void>() == body_c);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_unary, int>() == body_a);

    // Only the throwing slot is filled. The noexcept one must still
    // miss, because the two function-pointer types are distinct and
    // must not be folded onto one symbol.
    auto* body_d = std::bit_cast<CompiledBody*>(static_cast<std::uintptr_t>(0x4));
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_throwing, int>() == nullptr);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_noexcept, int>() == nullptr);
    ::crucible::cipher::insert_computation_cache<&p_throwing, int>(body_d);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_throwing, int>() == body_d);
    ok = ok && (::crucible::cipher::lookup_computation_cache<&p_noexcept, int>() == nullptr);

    return ok;
}

}  // namespace crucible::cipher
