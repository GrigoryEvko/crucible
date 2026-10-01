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

#include <foundation/diag/RowHash.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Hash.h>

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

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

// A key that travels must be a function of the computation alone.  The
// key folds the printed name of the function and of each argument type,
// and a closure prints a name that depends on its translation unit.  So
// a function reached through a closure's static invoker, or a signature
// or an argument type that names a closure or an unnamed class, has no
// key.  foundation/reflect/Hash.h reads the structure of each type.
template <auto FnPtr, typename... Args>
concept HasStableKeyIdentity =
    IsCacheableFunction<FnPtr> && ::foundation::reflect::function_has_stable_identity_v<FnPtr>
    && ::foundation::reflect::HasStableIdentity<std::remove_pointer_t<decltype(FnPtr)>>
    && (::foundation::reflect::HasStableIdentity<Args> && ...);

namespace detail {

inline constexpr std::string_view kUnstableKeyIdentity =
    "computation_cache_key: the function, its signature or an argument type has no stable identity.  The key "
    "folds each printed name, and a closure or an unnamed class prints a name that differs between translation "
    "units or is shared by different types.  Key a named function over named types.";

}  // namespace detail

// Unfenced, the row parameter would take any type. A type that is not
// a row folds to a zero contribution, which silently produces a key
// indistinguishable from the row-blind one. And a caller who meant
// the first argument type would find it bound to the row position
// instead, with no diagnostic.
//
// The fence is exact. The effects concept strips cv-qualifiers and
// references, but the row fold matches the bare row type alone, so a
// qualified row would pass that concept and fold to zero.

template <typename R>
concept IsEffectRow = std::is_same_v<R, std::remove_cvref_t<R>> && ::foundation::effects::IsEffectRow<R>;

// The row-aware calls sit beside the row-blind ones rather than
// replacing them. Threading the row through the existing names is not
// available: a new parameter without a default breaks every call
// site, and one with a default silently re-reads an existing call,
// binding the first argument type to the row position. So the two
// families keep separate names, and their slots never alias, not even
// for an empty row.
//
// Both families share one key fold and one slot template.  The row
// position of the row-blind family holds a marker that is not an
// effect row, so no row-aware call can name a row-blind slot.

namespace detail {

struct RowBlind {};

// The seed folds the function's name and its type, and the name is
// what does the real work. The type-derived identifier alone hashes
// the signature, so two different functions of the same signature
// collide on it. The storage slot does not care, because it is keyed
// on the template argument itself, but a key that travels does: two
// unrelated compiled bodies would meet in one slot on the far side.
// The type is folded in as well, so that two functions of the same
// name in different scopes stay apart.
//
// The combiner is order-sensitive, so the row step moves every
// row-aware key away from the row-blind key, even for a row that
// contributes zero.
template <auto FnPtr, typename RowOrBlind, typename... Args>
[[nodiscard]] consteval std::uint64_t cache_key_fold() noexcept {
    static_assert(::crucible::cipher::HasStableKeyIdentity<FnPtr, Args...>, kUnstableKeyIdentity);
    std::uint64_t k = ::foundation::reflect::stable_function_name_id<FnPtr>;
    k = ::foundation::reflect::combine_ids(k, ::foundation::reflect::stable_function_id<FnPtr>);
    if constexpr (!std::is_same_v<RowOrBlind, RowBlind>) {
        k = ::foundation::reflect::combine_ids(k, ::foundation::diag::row_hash_contribution_v<RowOrBlind>);
    }
    ((k = ::foundation::reflect::combine_ids(k, ::foundation::reflect::stable_type_id<Args>)), ...);
    return k;
}

template <auto FnPtr, typename RowOrBlind, typename... Args>
inline std::atomic<CompiledBody*> compiled_body_slot{nullptr};

template <auto FnPtr, typename RowOrBlind, typename... Args>
[[nodiscard]] CompiledBody* load_slot() noexcept {
    return compiled_body_slot<FnPtr, RowOrBlind, Args...>.load(std::memory_order_acquire);
}

// The first writer wins and a later one is discarded in silence.
template <auto FnPtr, typename RowOrBlind, typename... Args>
void publish_slot(CompiledBody* body) noexcept {
    CompiledBody* expected = nullptr;
    compiled_body_slot<FnPtr, RowOrBlind, Args...>.compare_exchange_strong(expected, body, std::memory_order_acq_rel,
                                                                           std::memory_order_acquire);
}

}  // namespace detail

template <auto FnPtr, typename... Args>
    requires IsCacheableFunction<FnPtr>
inline constexpr std::uint64_t computation_cache_key = detail::cache_key_fold<FnPtr, detail::RowBlind, Args...>();

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
inline constexpr std::uint64_t computation_cache_key_in_row = detail::cache_key_fold<FnPtr, Row, Args...>();

template <auto FnPtr, typename... Args>
    requires IsCacheableFunction<FnPtr>
[[nodiscard]] CompiledBody* lookup_computation_cache() noexcept {
    return detail::load_slot<FnPtr, detail::RowBlind, Args...>();
}

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
[[nodiscard]] CompiledBody* lookup_computation_cache_in_row() noexcept {
    return detail::load_slot<FnPtr, Row, Args...>();
}

// A null body would be indistinguishable from a miss, so it is
// refused. A caller that needs to know what is actually cached looks
// it up again after its insert.

template <auto FnPtr, typename... Args>
    requires IsCacheableFunction<FnPtr>
void insert_computation_cache(CompiledBody* body) noexcept pre(body != nullptr) {
    detail::publish_slot<FnPtr, detail::RowBlind, Args...>(body);
}

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
void insert_computation_cache_in_row(CompiledBody* body) noexcept pre(body != nullptr) {
    detail::publish_slot<FnPtr, Row, Args...>(body);
}

// This evicts nothing. A slot lives as long as the program does, and
// there is no registry through which to reach one.

inline void drain_computation_cache([[maybe_unused]] std::chrono::seconds max_age) noexcept {}

namespace detail {

// The probe functions of computation_cache_smoke_test, which the check
// file of this header also keys.  Each signature differs, so each probe
// keys a slot of its own.
inline void p_unary(int) noexcept {}
inline void p_binary(int, double) noexcept {}
inline void p_void() noexcept {}
inline void p_throwing(int) {}
inline void p_noexcept(int) noexcept {}

}  // namespace detail

// This runs once per process and refuses to run twice. The slots it
// reads are program-lifetime globals, so the checks that a lookup
// misses before its insert hold on the first call only. A second call
// would find those slots already full, so it reports failure rather
// than let a miss quietly become a hit.

inline bool computation_cache_smoke_test() noexcept {
    using detail::p_binary;
    using detail::p_noexcept;
    using detail::p_throwing;
    using detail::p_unary;
    using detail::p_void;

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
