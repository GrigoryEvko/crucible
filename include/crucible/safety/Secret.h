#pragma once

// ── crucible::safety::Secret<T> ─────────────────────────────────────
//
// Classified-by-default wrapper.  Secret data cannot cross a trust
// boundary without an explicit, grep-able declassification.
//
//   Axiom coverage: DetSafe + information-flow discipline.
//   Runtime cost:   zero.  sizeof(Secret<T>) == sizeof(T).
//
// - [[nodiscard]] at class level forces capture at construction.
// - Copy is deleted (classified values cannot silently duplicate).
// - Move is defaulted (explicit transfer).
// - .transform(f) produces Secret<f(T)> — operations stay Secret.
// - .declassify<Policy>() consumes the Secret and returns the raw T.
//   Policy is a class tag; grep "declassify<" for the audit trail.
// - .zeroize() is opt-in secure-zeroization for crypto-key paths.
//
// Pattern: wrap Philox seeds, Cipher keys, credentials, authenticated
// payload blobs.  Declassification requires a secret_policy::* tag
// documenting WHY the data escapes classification.

// ── DEPRECATION-ON-MIGRATE (Phase 2a Graded refactor) ──────────────
// Folds into a Graded<Modality, Lattice, T> alias once safety/Graded.h
// ships (misc/25_04_2026.md §2.3).  Public API preserved; this
// standalone implementation is removed at migration.
//
//   template <typename T>
//   using Secret = Graded<Comonad, ConfLattice, T>;
//
// Comonad form encodes information-flow direction (Abadi-Plotkin /
// Orchard-Liepelt-Eades 2023, arXiv:2309.04324).  declassify<Policy>()
// becomes the named counit out of the Comonad.
// Do not extend with new specializations — extend the Graded algebra.
// ───────────────────────────────────────────────────────────────────

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ConfLattice.h>

#include <concepts>
#include <cstring>
#include <memory>      // std::addressof
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// User-defined policy tags.  Extend in secret_policy:: as needed.
namespace secret_policy {
    struct AuditedLogging {};   // log with audit trail
    struct WireSerialize  {};   // encrypted-channel serialization
    struct HashForCompare {};   // release as hash (not the source)
    struct LengthOnly     {};   // release only size metadata
    struct UserDisplay    {};   // display in UI (e.g., last-4 of card)
}

// Concept: Policy is a non-void class type.  Enforces at the declassify
// call site that the user names an actual policy class.
template <typename Policy>
concept DeclassificationPolicy = std::is_class_v<Policy>;

template <typename T>
class [[nodiscard]] Secret {
public:
    using value_type = T;
    using lattice_type = ::crucible::algebra::lattices::ConfLattice::At<
        ::crucible::algebra::lattices::Conf::Secret>;
    // Modality declaration — Round-4 CHEAT-5; see Linear.h for the
    // rationale.  Secret is Comonad — declassify is the comonadic
    // counit (extract from the classified context).
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Comonad;
    // Public per GRADED-TRAIT-1 — see Linear.h for the rationale.
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Comonad, lattice_type, T>;

private:
    // Empty-lattice grade_type collapses via [[no_unique_address]] in
    // Graded; impl_ is sizeof(T).  Wrapper adds no other state.
    graded_type impl_;

public:

    constexpr explicit Secret(T v)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    // In-place construction — avoids moving the secret through a temporary.
    // Constructs T directly from args, then moves it into impl_; since
    // T is now moved exactly once (into impl_'s storage), there is no
    // intermediate temporary that survives.  For trivially-movable T
    // the elision is complete; for move-elidable T paths it is a
    // single move.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Secret(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    Secret(const Secret&)            = delete("Secret<T> cannot be silently duplicated");
    Secret& operator=(const Secret&) = delete("Secret<T> cannot be silently duplicated");
    Secret(Secret&&)                  = default;
    Secret& operator=(Secret&&)       = default;
    ~Secret()                         = default;

    // Transform — operations on Secret produce Secret.  The callable
    // `f` receives the classified payload as T&& and must return a
    // VALUE (not a reference, not void).  Two classes of "capture
    // leak" are closed at compile time (#151):
    //
    //   1. Reference-return — `f` returning `T&`, `const T&`, or a
    //      pointer-to-moved-from-storage would produce `Secret<T&>`
    //      whose referent is either (a) the moved-from internal
    //      `value_` (UAF on the next Secret dtor), or (b) a member
    //      of `f`'s closure that the caller can still mutate.
    //      Either way classified data escapes via ordinary pointer
    //      aliasing, bypassing the `declassify<Policy>` audit trail.
    //
    //   2. Void-return — `f` returning void produces `Secret<void>`,
    //      which is meaningless.  The likely intent is a side-
    //      effecting observation on the classified payload — that
    //      belongs in `declassify<Policy>()` where the audit trail
    //      survives review.
    //
    // `f` is TRUSTED for the duration of the call; transform does
    // NOT replace `declassify<Policy>` — it produces a DIFFERENT
    // classified value from the original.  Stateless transformations
    // (decode, parse, hash-fold) are the intended use.
    //
    // `requires std::invocable<F, T&&>` turns the "f isn't callable"
    // case into a clean concept-failure at the call site rather than
    // a deduction-failure inside `invoke_result_t`.
    template <typename F>
        requires std::invocable<F, T&&>
    [[nodiscard]] constexpr auto transform(F&& f) &&
        noexcept(std::is_nothrow_invocable_v<F, T&&>)
        -> Secret<std::invoke_result_t<F, T&&>>
    {
        using R = std::invoke_result_t<F, T&&>;
        static_assert(!std::is_reference_v<R>,
            "[Capture_Leak_Reference_Return] Secret::transform(f): f"
            " must return by value.  A reference return aliases either"
            " the moved-from secret storage (UAF) or a member of f's"
            " closure (silent declassification bypassing"
            " declassify<Policy>).  Change f's return type to a value,"
            " or — if the intent is to observe classified data — call"
            " declassify<Policy>() first to leave an audit trail."
            " (#151, Secret.h transform())");
        static_assert(!std::is_void_v<R>,
            "[Capture_Leak_Void_Return] Secret::transform(f): f must"
            " return a value.  void → Secret<void> is meaningless; the"
            " likely intent is a side-effecting observation on the"
            " classified payload — that belongs in declassify<Policy>(),"
            " not transform()."
            " (#151, Secret.h transform())");
        return Secret<R>{
            std::forward<F>(f)(std::move(impl_).consume())
        };
    }

    // Length-preserving accessor — compiles only when T has .size().
    // Forwards through Graded::peek().
    [[nodiscard]] constexpr auto size() const noexcept
        requires requires(const T& t) { t.size(); }
    {
        return impl_.peek().size();
    }

    // Declassify — consumes the Secret and returns T.  Requires a
    // policy tag; the call site `declassify<SomePolicy>` is the audit
    // trail.  Forwards to Graded::extract() (the Comonad counit).
    template <DeclassificationPolicy Policy>
    [[nodiscard]] constexpr T declassify() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).extract();
    }

    // Secure zeroization — overwrites the internal storage with zero
    // bytes before destruction.  Opt-in: call explicitly for crypto-key
    // paths.  Uses volatile writes to prevent the compiler from eliding
    // the clear.
    //
    // Requires T to be trivially copyable (can be safely overwritten).
    // Accesses impl_'s mutable storage via Graded::peek_mut() — admitted
    // by the refined gate `(AbsoluteModality || empty grade)` because
    // ConfLattice::At<Conf::Secret>::element_type is empty even though
    // Secret is Comonad modality.  Pre-refinement, this would have
    // required const_cast on impl_.peek() or a Graded friendship hack.
    void zeroize() noexcept
        requires std::is_trivially_copyable_v<T>
    {
        // Prevent compiler from optimizing away the memset before dtor.
        volatile auto* p = reinterpret_cast<volatile unsigned char*>(
            std::addressof(impl_.peek_mut()));
        for (std::size_t i = 0; i < sizeof(T); ++i) p[i] = 0;
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via reflection (P2996R13).
    //
    // lattice_name(): "ConfLattice::At<Secret>" — the confidentiality
    // comonad lattice's secret tier.  Distinct from the runtime
    // declassify<Policy> audit trail; this is purely for diagnostic
    // emission ("which lattice is this Secret graded by?").
    //
    // Audit-Tier-2 cross-wrapper parity — every migrated wrapper
    // ships these two consteval forwarders.  See Linear.h for
    // full rationale.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};

template <typename T>
Secret(T) -> Secret<T>;

// Factory: construct Secret<T> by forwarding to T's constructor.
template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Secret<T> make_secret(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return Secret<T>{std::in_place, std::forward<Args>(args)...};
}

// Zero-cost guarantee.
static_assert(sizeof(Secret<int>)               == sizeof(int));
static_assert(sizeof(Secret<unsigned long long>) == sizeof(unsigned long long));

} // namespace crucible::safety
