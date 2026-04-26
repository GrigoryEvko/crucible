#pragma once

// ── Mutation-mode wrappers ─────────────────────────────────────────
//
// Make the allowed update discipline part of the type.
//
//   Axiom coverage: MemSafe, DetSafe.
//   Runtime cost:   zero beyond the wrapped container (AppendOnly),
//                   one contract check per advance (Monotonic),
//                   one bool per instance (WriteOnce).
//
// AppendOnly<T, Storage>               — Storage<T> restricted to grow-only.
// OrderedAppendOnly<T, KeyFn, Cmp, St> — AppendOnly + per-emplace key
//                                        monotonicity (nested composition).
// Monotonic<T, Cmp>                    — single value that only advances per Cmp.
// BoundedMonotonic<T, Max, Cmp>        — Monotonic + per-advance bound check
//                                        (nested composition, not a Refined
//                                        alias — see body comment).
// WriteOnce<T>                         — settable exactly once, then read-only.

// ── DEPRECATION-ON-MIGRATE (Phase 2a Graded refactor) ──────────────
// Monotonic and AppendOnly fold into Graded<Modality, Lattice, T>
// aliases once safety/Graded.h ships (misc/25_04_2026.md §2.3).
// Public API preserved; the corresponding implementations in this
// header are removed at migration.
//
//   template <typename T, typename Cmp = std::less<T>>
//   using Monotonic = Graded<Absolute, MonotoneLattice<T, Cmp>, T>;
//
//   template <typename T>
//   using AppendOnly = Graded<Absolute, SeqPrefixLattice, T>;
//
// BoundedMonotonic = Monotonic ⊗ Refined<bounded_above>; under Graded
// it becomes a product-lattice instantiation, no separate template.
// WriteOnce / WriteOnceNonNull stay structural for now (one-shot
// publication — distinct from a graded value).  Do not extend the
// migrating types with new specializations — extend Graded instead.
// ───────────────────────────────────────────────────────────────────

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/MonotoneLattice.h>
#include <crucible/algebra/lattices/SeqPrefixLattice.h>
#include <crucible/safety/Pinned.h>

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible::safety {

// ── Forward declarations + wrapper-stacking traits ──────────────────
//
// These let downstream wrappers reject redundant stacks at instantiation
// time.  The canonical case is AppendOnly<WriteOnce<T>>: AppendOnly
// already guarantees that once an element is emplaced it is never
// mutated, reassigned, or removed — layering WriteOnce inside adds no
// invariant and doubles the per-element storage by one tag byte.
// Catching the pattern structurally keeps the backlog clear of
// "should we also wrap the value?" design questions.

template <typename T> class WriteOnce;                // forward decl
template <typename Ptr> class WriteOnceNonNull;        // forward decl (#77)

template <typename T> struct is_writeonce            : std::false_type {};
template <typename U> struct is_writeonce<WriteOnce<U>> : std::true_type {};
template <typename T>
inline constexpr bool is_writeonce_v = is_writeonce<std::remove_cvref_t<T>>::value;

// WriteOnceNonNull<T*> participates in the same redundancy check
// (#77): AppendOnly<WriteOnceNonNull<T*>> is equally structurally
// redundant with AppendOnly's own immutability guarantee.  Kept
// as a separate trait so AppendOnly can fire a distinct named
// diagnostic for each form, and the pre-existing neg-compile
// regex `"AppendOnly<WriteOnce<T>> is redundant"` stays stable.
template <typename T> struct is_writeoncenonnull                    : std::false_type {};
template <typename Ptr> struct is_writeoncenonnull<WriteOnceNonNull<Ptr>> : std::true_type {};
template <typename T>
inline constexpr bool is_writeoncenonnull_v =
    is_writeoncenonnull<std::remove_cvref_t<T>>::value;

// ── AppendOnly ──────────────────────────────────────────────────────
//
// Default storage is std::vector; users may substitute arena-backed
// or inplace_vector backing by specifying the second template param.
//
// ── MIGRATED to Graded<Absolute, SeqPrefixLattice<T>, Storage<T>> (#466) ─
//
// As of MIGRATE-6 (2026-04-26) AppendOnly<T, Storage> delegates
// storage to the algebraic primitive
//
//   Graded<ModalityKind::Absolute,
//          SeqPrefixLattice<T>,
//          Storage<T>>
//
// per misc/25_04_2026.md §2.3.  The wrapper preserves every existing
// public API (emplace / append / operator[] / front / back / size /
// empty / begin / end / drain).
//
// ZERO STORAGE COST via DERIVED-GRADE specialization
//   Graded ships a third partial specialization (algebra/Graded.h
//   §"derived-grade") that activates when the lattice provides a
//   `static element_type grade_of(T const&)`.  SeqPrefixLattice opts
//   in: grade_of(c) returns Length{c.size()}.  Result: Graded stores
//   ONLY the Storage<T> value; the grade is computed on demand from
//   the vector's own .size().
//
//   sizeof(AppendOnly<T, std::vector>) == sizeof(std::vector<T>)
//   — same as pre-migration.  Arena's `static_assert(sizeof(Arena)
//   == 64)` cache-line invariant holds; AppendOnly<char*> packs
//   into the 64-byte budget exactly as before.
//
// SUBSTRATE COVERAGE NOTE
//   AppendOnly is the canonical caller of the derived-grade
//   specialization.  The substrate now correctly handles all three
//   regimes:
//     - Empty grade (Linear, Refined): EBO collapses, sizeof(T).
//     - Grade == T (Monotonic): single-field collapse, sizeof(T).
//     - Grade derives from T (AppendOnly): single field, computed
//       grade, sizeof(T).
//   No 2× regression in any standard wrapper.

template <typename T, template <typename...> class Storage = std::vector>
class [[nodiscard]] AppendOnly {
    static_assert(!is_writeonce_v<T>,
        "AppendOnly<WriteOnce<T>> is redundant: AppendOnly already guarantees "
        "that emplaced elements are never mutated, reassigned, or removed. "
        "Use AppendOnly<T> directly — the WriteOnce layer adds no invariant "
        "and doubles per-element storage by one std::optional tag byte.");
    // Symmetric rejection for the pointer-specialized wrapper (#77).
    // WriteOnceNonNull<T*> is the pointer-slot form of WriteOnce; it
    // uses nullptr as the "not yet set" sentinel instead of carrying
    // a std::optional tag byte.  Stacking inside AppendOnly adds no
    // invariant for the same reason as WriteOnce<T> — AppendOnly's
    // own immutability promise subsumes it.
    static_assert(!is_writeoncenonnull_v<T>,
        "[AppendOnly_Over_WriteOnceNonNull_Redundant] AppendOnly<WriteOnceNonNull<T*>> "
        "is redundant: AppendOnly already guarantees that emplaced elements "
        "are never mutated, reassigned, or removed, which subsumes "
        "WriteOnceNonNull's single-set guarantee.  Use AppendOnly<T*> directly "
        "when the elements are pointers, and rely on a per-insertion "
        "non-null contract at the call site if that's the real invariant. "
        "(#77, symmetric with the AppendOnly<WriteOnce<T>> rejection above)");

    using lattice_type = ::crucible::algebra::lattices::SeqPrefixLattice<T>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, Storage<T>>;

    graded_type impl_;

public:
    using value_type     = T;
    using storage_type   = Storage<T>;
    using const_iterator = typename Storage<T>::const_iterator;

    // Default-construct: empty Storage; derived grade automatically
    // resolves to Length{0} == bottom.
    AppendOnly() : impl_{Storage<T>{}} {}

    // The only mutation permitted: grow the tail.  Forwards through
    // Graded::peek_mut() (gated on AbsoluteModality<M> in Graded).
    // The lattice grade auto-updates because it's derived from
    // c.size() — no separate field to maintain.
    template <typename... Args>
    void emplace(Args&&... args) {
        impl_.peek_mut().emplace_back(std::forward<Args>(args)...);
    }

    void append(T item) {
        impl_.peek_mut().emplace_back(std::move(item));
    }

    // Read-only access — forwards through Graded::peek().
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept {
        return impl_.peek()[i];
    }
    [[nodiscard]] const T& front() const noexcept { return impl_.peek().front(); }
    [[nodiscard]] const T& back()  const noexcept { return impl_.peek().back(); }
    [[nodiscard]] std::size_t size()  const noexcept { return impl_.peek().size(); }
    [[nodiscard]] bool        empty() const noexcept { return impl_.peek().empty(); }

    [[nodiscard]] const_iterator begin() const noexcept { return impl_.peek().begin(); }
    [[nodiscard]] const_iterator end()   const noexcept { return impl_.peek().end(); }

    // Consuming drain — yield the collected storage and leave *this
    // in a moved-from state.  Forwards through Graded::consume().
    [[nodiscard]] Storage<T> drain() && noexcept(std::is_nothrow_move_constructible_v<Storage<T>>) {
        return std::move(impl_).consume();
    }
};

// Zero-cost guarantee preserved by Graded's derived-grade
// specialization (algebra/Graded.h §"derived-grade").  The lattice
// element (Length<T>, 8 bytes) is computed on demand from the
// container's .size() rather than stored as a separate field —
// no +8 byte regression vs pre-migration storage.
static_assert(sizeof(AppendOnly<char*>)        == sizeof(std::vector<char*>),
              "AppendOnly<char*> must collapse to sizeof(Storage<char*>) — "
              "Graded's derived-grade specialization lets the grade "
              "(Length{size_t}) be computed from c.size() rather than "
              "stored separately.  If this fires, the specialization is "
              "no longer being selected (likely SeqPrefixLattice's "
              "grade_of trait method drifted).");
static_assert(sizeof(AppendOnly<std::uint64_t>) == sizeof(std::vector<std::uint64_t>),
              "AppendOnly<uint64_t> must collapse to sizeof(Storage<T>); "
              "see neighboring assertion for the substrate rationale.");

// ── OrderedAppendOnly ───────────────────────────────────────────────
//
// AppendOnly + per-emplace monotonicity check on a key projection.
// Nested composition of AppendOnly (grow-only) and Monotonic's ordering
// invariant: each appended element's projected key must not go backward
// per Cmp relative to the last appended element's key.
//
// Typical use: an append-only log whose entries carry a monotonically
// non-decreasing step_id / epoch / timestamp whose ordering is relied
// upon by downstream code (e.g. binary search).  Without this wrapper
// the ordering lives as a runtime pre() on the writer plus a doc
// comment on the reader; here it is the log's type.
//
// KeyFn and Cmp must be stateless (matches Monotonic's idiom) so the
// pre-condition can construct them fresh per-call; [[no_unique_address]]
// collapses empty functors to zero layout cost.
//
//   Axiom coverage: MemSafe (inherits AppendOnly) + DetSafe.
//   Runtime cost:   storage of the wrapped Storage<T>, plus one KeyFn
//                   invocation + one Cmp invocation per append under
//                   contract semantic=enforce/observe; zero under
//                   semantic=ignore (hot-path TUs).

template <typename T,
          typename KeyFn = std::identity,
          typename Cmp   = std::less<>,
          template <typename...> class Storage = std::vector>
class [[nodiscard]] OrderedAppendOnly {
    AppendOnly<T, Storage>     inner_;
    [[no_unique_address]] KeyFn key_{};
    [[no_unique_address]] Cmp   cmp_{};

public:
    using value_type     = T;
    using key_type       = std::invoke_result_t<KeyFn, const T&>;
    using key_fn_type    = KeyFn;
    using comparator     = Cmp;
    using storage_type   = Storage<T>;
    using const_iterator = typename AppendOnly<T, Storage>::const_iterator;

    OrderedAppendOnly() = default;

    // The only mutation permitted: grow the tail, and only with a key
    // that does not go backward relative to the last entry.  Contract
    // fires via std::terminate under enforce; collapses to [[assume]]
    // under ignore.  KeyFn/Cmp are stateless — this idiom matches
    // Monotonic::advance's `pre(!Cmp{}(new, old))`.
    void append(T item)
        pre(inner_.empty() || !Cmp{}(KeyFn{}(item), KeyFn{}(inner_.back())))
    {
        inner_.append(std::move(item));
    }

    // Forwarding emplace: constructs the element, then contract-checks
    // via append().  The temp construction is unavoidable — we can't
    // evaluate the key of a not-yet-constructed element.
    template <typename... Args>
    void emplace(Args&&... args) {
        append(T{std::forward<Args>(args)...});
    }

    // Read-only access — delegates to the wrapped AppendOnly.
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept { return inner_[i]; }
    [[nodiscard]] const T& front() const noexcept { return inner_.front(); }
    [[nodiscard]] const T& back()  const noexcept { return inner_.back(); }
    [[nodiscard]] std::size_t size()  const noexcept { return inner_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return inner_.empty(); }

    [[nodiscard]] const_iterator begin() const noexcept { return inner_.begin(); }
    [[nodiscard]] const_iterator end()   const noexcept { return inner_.end(); }

    // Consuming drain — yield the collected storage and leave *this empty.
    [[nodiscard]] Storage<T> drain() && noexcept(std::is_nothrow_move_constructible_v<Storage<T>>) {
        return std::move(inner_).drain();
    }
};

// Zero-cost guarantee: stateless KeyFn + Cmp collapse via [[no_unique_address]],
// so OrderedAppendOnly<T> with default projections has the same layout as
// the wrapped AppendOnly<T>.
static_assert(sizeof(OrderedAppendOnly<std::uint64_t>) == sizeof(AppendOnly<std::uint64_t>),
              "OrderedAppendOnly must collapse empty KeyFn/Cmp to zero layout cost");

// ── Monotonic ───────────────────────────────────────────────────────
//
// Cmp defaults to std::less<T>; advance(v) requires `!(v < current)`,
// i.e. `v >= current`.  Use std::greater for decreasing-only semantics.
//
// ── MIGRATED to Graded<Absolute, MonotoneLattice<T, Cmp>, T> (#465) ─
//
// As of MIGRATE-5 (2026-04-26) Monotonic<T, Cmp> is a thin wrapper
// around the algebraic primitive
//
//   Graded<ModalityKind::Absolute,
//          MonotoneLattice<T, Cmp>,
//          T>
//
// per misc/25_04_2026.md §2.3.  The wrapper preserves every existing
// public API (get / current / advance / try_advance / bump), with
// the precondition rewritten in terms of `lattice_type::leq` for
// algebraic clarity (semantically identical to the original
// `!Cmp{}(new_value, value_)`).
//
// ZERO STORAGE COST: thanks to the Graded partial specialization for
// `T == L::element_type` (algebra/Graded.h §"Partial specialization:
// value type IS the lattice element type"), Graded<Absolute,
// MonotoneLattice<T, Cmp>, T> stores a single T field — the value
// AND the grade collapse to one storage cell.  sizeof(Monotonic<T>)
// == sizeof(T) is preserved, the same zero-overhead contract the
// pre-migration wrapper offered.  Production callers (TraceRing
// head/tail, IterationDetector, Arena::offset_) keep their
// cache-line layout invariants unchanged.
//
// MUTATION SEMANTICS: advance() / try_advance() / bump() use the
// specialization's ergonomic single-arg constructor:
//     impl_ = graded_type{std::move(new_value)};
// One move into the unified storage cell.  The old in-place
// assignment shape (`value_ = std::move(new_value)`) costs the same
// for trivially-movable T (the typical case).
//
// ALGEBRAIC ACCESS: callers can reach the underlying Graded view via
// the .grade() forwarder if they want to compose Monotonic values
// via the lattice's join.  The wrapper's own API never exposes the
// graded_type to keep the discipline consistent with the existing
// production-caller surface.

template <typename T, typename Cmp = std::less<T>>
class [[nodiscard]] Monotonic {
    using lattice_type = ::crucible::algebra::lattices::MonotoneLattice<T, Cmp>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

    graded_type impl_;

public:
    using value_type      = T;
    using comparator_type = Cmp;

    // Construction: pass `initial` once via the Graded specialization's
    // ergonomic single-arg ctor.  One move into the unified storage
    // cell.  No copy, no double-store.
    constexpr explicit Monotonic(T initial)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(initial)} {}

    Monotonic(const Monotonic&)            = default;
    Monotonic(Monotonic&&)                 = default;
    Monotonic& operator=(const Monotonic&) = default;
    Monotonic& operator=(Monotonic&&)      = default;

    [[nodiscard]] constexpr const T& get()     const noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr const T& current() const noexcept { return impl_.peek(); }

    // Advance.  Contract-checks that the new value does not go
    // backward.  The precondition uses lattice_type::leq for
    // algebraic clarity — `lattice_type::leq(current, new_value)` is
    // exactly `!Cmp{}(new_value, current)`, the original predicate.
    constexpr void advance(T new_value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        pre(lattice_type::leq(impl_.peek(), new_value))
    {
        // Reassign impl_ via the specialization's single-arg ctor.
        // Storage is one cell; one move suffices to bring both the
        // value-view (peek) and the grade-view (grade) to new_value.
        impl_ = graded_type{std::move(new_value)};
    }

    // Compare-and-advance.  Returns true iff advanced.  Useful when
    // multiple threads attempt to advance and only the monotonic-
    // valid ones should succeed.
    constexpr bool try_advance(T new_value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        if (!lattice_type::leq(impl_.peek(), new_value)) return false;
        impl_ = graded_type{std::move(new_value)};
        return true;
    }

    // Convenience for integral counters: increment by one.  Contract
    // catches wraparound (the only way an integral counter can
    // violate monotonicity is overflow).  Equivalent to
    // advance(get() + 1).
    constexpr void bump() noexcept
        requires std::integral<T>
        pre(impl_.peek() != std::numeric_limits<T>::max())
    {
        impl_ = graded_type{impl_.peek() + T{1}};
    }
};

// Zero-cost guarantee preserved by Graded's partial specialization
// for `T == L::element_type` (algebra/Graded.h §"Partial
// specialization: value type IS the lattice element type") — Graded
// stores a SINGLE T field when value and grade collapse to the same
// type, exactly the case for Monotonic<T, MonotoneLattice<T, Cmp>>.
// If this fires, the specialization is no longer being selected
// (likely a refactor changed the requires-clause or the lattice's
// element_type drifted).
static_assert(sizeof(Monotonic<uint32_t, std::less<uint32_t>>) == sizeof(uint32_t),
              "Monotonic<T, EmptyCmp> must be zero-cost — Graded's "
              "T==element_type specialization collapses value and grade "
              "to one storage cell.");
static_assert(sizeof(Monotonic<uint64_t, std::less<uint64_t>>) == sizeof(uint64_t),
              "Monotonic<T, EmptyCmp> must be zero-cost — Graded's "
              "T==element_type specialization collapses value and grade "
              "to one storage cell.");

// ── BoundedMonotonic ────────────────────────────────────────────────
//
// Monotonic + compile-time upper bound enforced at every mutation.
// Intended for counters that advance and must not wrap (OpIndex, step_id,
// iteration counters, op_index during replay).
//
// Why this isn't `Refined<bounded_above<Max>, Monotonic<T>>`:
//   Refined checks its predicate ONCE at construction; subsequent
//   advances on the inner Monotonic bypass the check.  Combining the
//   two as a type alias fails silently.  Instead BoundedMonotonic
//   nests a Monotonic and re-applies the predicate at each
//   advance / bump call site, matching Refined's discipline in
//   spirit.  The bound becomes part of the type (compile-time Max),
//   so downstream code can `[[assume]]` the invariant on reads.
//
//   Axiom coverage: DetSafe + TypeSafe (the bound is a type parameter).
//   Runtime cost:   same as Monotonic plus one extra comparison per
//                   mutation (contract-elided under semantic=ignore).

template <typename T, auto Max, typename Cmp = std::less<T>>
class [[nodiscard]] BoundedMonotonic {
    Monotonic<T, Cmp> inner_;

    static constexpr T kMax = T(Max);

public:
    using value_type      = T;
    using comparator_type = Cmp;
    static constexpr T    max() noexcept { return kMax; }

    constexpr explicit BoundedMonotonic(T initial)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        pre(!(T(Max) < initial))        // initial <= Max
        : inner_{std::move(initial)} {}

    BoundedMonotonic(const BoundedMonotonic&)            = default;
    BoundedMonotonic(BoundedMonotonic&&)                 = default;
    BoundedMonotonic& operator=(const BoundedMonotonic&) = default;
    BoundedMonotonic& operator=(BoundedMonotonic&&)      = default;

    [[nodiscard]] constexpr const T& get()     const noexcept { return inner_.get(); }
    [[nodiscard]] constexpr const T& current() const noexcept { return inner_.current(); }

    // Advance — must be both non-decreasing (Monotonic's rule) AND
    // within the bound.  The inner Monotonic's pre() covers the first;
    // this pre() adds the bound.
    constexpr void advance(T new_value) noexcept(std::is_nothrow_move_assignable_v<T>)
        pre(!(T(Max) < new_value))      // new_value <= Max
    {
        inner_.advance(std::move(new_value));
    }

    // try_advance returns false if either the monotonicity OR the bound
    // would be violated; the Monotonic still succeeds on equal values.
    constexpr bool try_advance(T new_value)
        noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        if (T(Max) < new_value) return false;  // bound violation
        return inner_.try_advance(std::move(new_value));
    }

    // Integral bump — advance by one, guarded by the bound.  Mirrors
    // Monotonic::bump but uses the type's Max rather than the domain
    // type's numeric_limits::max.
    constexpr void bump() noexcept
        requires std::integral<T>
        pre(inner_.get() < T(Max))
    {
        inner_.advance(static_cast<T>(inner_.get() + T{1}));
    }
};

// Zero-cost: stateless Cmp collapses, inner Monotonic is same size as T.
static_assert(sizeof(BoundedMonotonic<std::uint32_t, 1024U>) == sizeof(std::uint32_t),
              "BoundedMonotonic must collapse to underlying T");

// ── WriteOnce ───────────────────────────────────────────────────────
//
// Settable exactly once.  Subsequent attempts contract-fail.  After
// set, the value is immutable.  Reads before set contract-fail.
//
// Runtime cost: one bool per instance (implicit in std::optional tag).
// Use for init-time constants whose value is discovered at startup.

template <typename T>
class [[nodiscard]] WriteOnce {
    std::optional<T> value_;

public:
    using value_type = T;

    constexpr WriteOnce() = default;

    WriteOnce(const WriteOnce&)            = default;
    WriteOnce(WriteOnce&&)                 = default;
    WriteOnce& operator=(const WriteOnce&) = default;
    WriteOnce& operator=(WriteOnce&&)      = default;

    // Set exactly once.  Contract-checks that value has not been set.
    constexpr void set(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        pre(!value_.has_value())
    {
        value_.emplace(std::move(v));
    }

    // Try-set — returns true iff this was the first set.
    constexpr bool try_set(T v) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (value_) return false;
        value_.emplace(std::move(v));
        return true;
    }

    // Read the set value.  Contract-fails if not yet set.
    [[nodiscard]] constexpr const T& get() const noexcept
        pre(value_.has_value())
    {
        return *value_;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return value_.has_value(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_.has_value(); }
};

// ── WriteOnceNonNull<T*> ───────────────────────────────────────────
//
// Pointer-specialized single-set slot — the nullptr sentinel replaces
// WriteOnce<T>'s std::optional tag byte, collapsing storage to exactly
// sizeof(T*).  Semantics:
//
//   - set(p)    : contract fires on double-set OR on null input.
//                 null is reserved as the unset sentinel; publishing
//                 null would be indistinguishable from "never set."
//   - try_set(p): returns false on double-set or null input; no
//                 contract fire.  Idempotent.
//   - get()     : contract fires if not yet set.  Returns T*.
//   - has_value / operator bool : exposes set-vs-unset for defensive
//                 code paths.
//
// Design choice — NAMED PARTIAL SPECIALIZATION on the pointer type
// rather than a generic `WriteOnceNonNull<T>` with `T*` implied: the
// spelling `WriteOnceNonNull<TraceRing*>` matches the type of the
// stored value and `WriteOnceNonNull<int>` fires a named static_assert
// at the primary template rather than silently producing a single-set
// integer slot with surprising null-sentinel semantics.
//
// Semantic overlap with Once.h's SetOnce<T>: identical storage
// strategy.  SetOnce uses `T` (pointee) as the template parameter;
// WriteOnceNonNull uses `T*` (pointer) as the template parameter for
// naming symmetry with WriteOnce<T>.  Both ship so callers can pick
// the spelling that matches their surrounding idiom.
//
//   Axiom coverage: InitSafe + NullSafe + MemSafe + DetSafe.
//   Runtime cost:   zero — sizeof(WriteOnceNonNull<T*>) == sizeof(T*).
//                   One contract check per set/get under semantic=
//                   enforce; zero under semantic=ignore.

template <typename Ptr>
class WriteOnceNonNull {
    static_assert(std::is_pointer_v<Ptr>,
        "[WriteOnceNonNull_NonPointer_Type] WriteOnceNonNull requires "
        "a pointer type argument (e.g. WriteOnceNonNull<MyClass*>). "
        "The nullptr-sentinel strategy that makes this primitive "
        "zero-overhead is meaningful only for pointer types; for "
        "single-set slots over non-pointer values use WriteOnce<T> "
        "(sizeof(T) + 1 byte std::optional tag).  (#77)");
};

template <typename T>
class [[nodiscard]] WriteOnceNonNull<T*> {
    T* ptr_ = nullptr;

public:
    using value_type   = T*;
    using pointee_type = T;

    constexpr WriteOnceNonNull() noexcept = default;

    WriteOnceNonNull(const WriteOnceNonNull&)            = default;
    WriteOnceNonNull(WriteOnceNonNull&&)                 = default;
    WriteOnceNonNull& operator=(const WriteOnceNonNull&) = default;
    WriteOnceNonNull& operator=(WriteOnceNonNull&&)      = default;

    // Set exactly once.  Contract fires on double-set (ptr_ already
    // non-null) and on null input (publishing null is indistinguishable
    // from "never set" in the sentinel model — always a caller bug).
    constexpr void set(T* p) noexcept
        pre (p != nullptr)
        pre (ptr_ == nullptr)
    {
        ptr_ = p;
    }

    // Try-set — returns true iff this was the first non-null set.
    // No contract fire; null input and double-set both become no-ops
    // returning false.
    [[nodiscard]] constexpr bool try_set(T* p) noexcept {
        if (ptr_ != nullptr || p == nullptr) return false;
        ptr_ = p;
        return true;
    }

    // Read the set pointer.  Contract fires if not yet set.
    [[nodiscard]] constexpr T* get() const noexcept
        pre (ptr_ != nullptr)
    {
        return ptr_;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept {
        return ptr_ != nullptr;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    // Dereference — contract fires if not yet set.  Member function
    // template with a defaulted U=T parameter so the return type
    // `U&` is only instantiated when the operator is actually called,
    // letting `WriteOnceNonNull<void*>` still satisfy other member
    // uses (get, has_value) without triggering "forming reference to
    // void" at class-instantiation time.
    template <typename U = T>
        requires (!std::is_void_v<U>)
    [[nodiscard]] constexpr U& operator*() const noexcept
        pre (ptr_ != nullptr)
    {
        return *ptr_;
    }

    template <typename U = T>
        requires (!std::is_void_v<U>)
    [[nodiscard]] constexpr U* operator->() const noexcept
        pre (ptr_ != nullptr)
    {
        return ptr_;
    }
};

// Zero-cost guarantee: nullptr-sentinel collapses storage to exactly
// sizeof(T*), saving the optional tag byte + padding of WriteOnce<T*>.
// Both typed and void-pointee forms are supported — operator* and
// operator-> are SFINAE'd away for the void pointee.
static_assert(sizeof(WriteOnceNonNull<int*>)  == sizeof(int*));
static_assert(sizeof(WriteOnceNonNull<void*>) == sizeof(void*));

// ── AtomicMonotonic<T> ─────────────────────────────────────────────
//
// Thread-safe Monotonic: multiple threads may observe and advance.
// Loads are acquire; advances are acq_rel.
//
// sizeof(AtomicMonotonic<uint64_t>) == sizeof(atomic<uint64_t>).
//
//   Axiom coverage: ThreadSafe + DetSafe.
//   Runtime cost:   one fetch_max / fetch_min per advance for the
//                   canonical std::less / std::greater comparators on
//                   integral T (P0493R5, GCC 16).  ARMv8.1+ LSE emits
//                   one-cycle LDUMAX / LDUMIN; x86-64 emits an
//                   equivalent CAS loop in libstdc++.  Falls back to a
//                   hand-rolled CAS loop for arbitrary Cmp or
//                   non-integral T.
//
// Pinned<>: the atomic IS the channel identity; movement would fork
// the monotonic sequence across two atomics.

template <typename T, typename Cmp = std::less<T>>
    requires std::is_trivially_copyable_v<T>
class [[nodiscard]] AtomicMonotonic : Pinned<AtomicMonotonic<T, Cmp>> {
    std::atomic<T> value_;

    // Canonical comparator detection: handle both the explicitly-typed
    // (std::less<T>) and the transparent (std::less<>) forms.  The
    // transparent form lets future call sites use heterogeneous compares
    // without giving up the fetch_max fast path.
    static constexpr bool kIsLess =
        std::is_same_v<Cmp, std::less<T>> ||
        std::is_same_v<Cmp, std::less<>>;
    static constexpr bool kIsGreater =
        std::is_same_v<Cmp, std::greater<T>> ||
        std::is_same_v<Cmp, std::greater<>>;
    static constexpr bool kFastPathEligible =
        std::integral<T> && (kIsLess || kIsGreater);

public:
    using value_type      = T;
    using comparator_type = Cmp;

    constexpr explicit AtomicMonotonic(T initial) noexcept : value_{initial} {}

    [[nodiscard]] T get() const noexcept {
        return value_.load(std::memory_order_acquire);
    }

    // Try to advance to new_value.  Returns true iff this call moved the
    // value forward (per Cmp); false if the atomic already held a value
    // that is at-or-past new_value.
    //
    // Fast path (canonical Cmp + integral T): one fetch_max / fetch_min.
    // Slow path: CAS loop preserving the original semantics.
    [[nodiscard]] bool try_advance(T new_value) noexcept {
        if constexpr (kFastPathEligible && kIsLess) {
            const T prev = value_.fetch_max(new_value, std::memory_order_acq_rel);
            return prev < new_value;
        } else if constexpr (kFastPathEligible && kIsGreater) {
            const T prev = value_.fetch_min(new_value, std::memory_order_acq_rel);
            return prev > new_value;
        } else {
            Cmp cmp;
            T observed = value_.load(std::memory_order_acquire);
            while (cmp(observed, new_value)) {
                if (value_.compare_exchange_weak(
                        observed, new_value,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                    return true;
            }
            return false;
        }
    }

    // Strict advance: contract fires if new_value would go backward.
    void advance(T new_value) noexcept
        pre (Cmp{}(value_.load(std::memory_order_acquire), new_value))
    {
        value_.store(new_value, std::memory_order_release);
    }
};

// ── MaxObserved<T> ─────────────────────────────────────────────────
//
// AtomicMonotonic<T, std::less<T>> alias expressing "max seen so far".
// Semantically identical, name makes intent obvious at call site:
//   MaxObserved<uint64_t> high_water{0};
//   high_water.try_advance(new_size);  // track peak
//
// Using the named alias rather than a new class preserves the
// underlying implementation and keeps sizeof collapse.

template <typename T>
using MaxObserved = AtomicMonotonic<T, std::less<T>>;

// ── MonotonicClock ─────────────────────────────────────────────────
//
// Thin wrapper around std::chrono::steady_clock::now() that enforces
// monotonicity even across the (rare) implementation glitches where
// steady_clock goes briefly backward on Linux (occurs on some VM
// migration / CLOCK_MONOTONIC_RAW races).
//
// The underlying clock is already monotonic by spec; this layer
// defends against platform/kernel bugs that Crucible's bit-exact
// replay cannot tolerate.  now() returns AtomicMonotonic-managed
// counter — never regresses.
//
// Cost: one atomic load + (rarely) one CAS per now().  Used for
// step-id generation and iteration timing in hot paths that record,
// not per-op.

class MonotonicClock : Pinned<MonotonicClock> {
    AtomicMonotonic<uint64_t> last_ns_{0};

public:
    MonotonicClock() noexcept = default;

    // Returns a nanosecond timestamp that never regresses across
    // threads.  If steady_clock::now() goes backward, returns the
    // previously-observed value instead — detection of the clock bug
    // is the responsibility of the host monitoring layer.
    [[nodiscard]] uint64_t now_ns() noexcept {
        const uint64_t raw = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        // try_advance returns false if raw ≤ observed; in that case
        // we return the observed value (same-or-forward).  The caller
        // sees a monotonic sequence regardless of underlying regress.
        (void)last_ns_.try_advance(raw);
        const uint64_t observed = last_ns_.get();
        return observed > raw ? observed : raw;
    }
};

} // namespace crucible::safety
