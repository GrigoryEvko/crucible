#ifndef CRUCIBLE_SAFETY_FN_COLLISION_CATALOG_INTEGRATION
#include <crucible/safety/Fn.h>
#else
#ifndef CRUCIBLE_SAFETY_COLLISION_CATALOG_BODY
#define CRUCIBLE_SAFETY_COLLISION_CATALOG_BODY

// ── crucible::safety — CollisionCatalog.h (GAPS-005..018) ───────────
//
// Compile-time collision rules for safety::fn::Fn<...>.  Fn is the
// 19-axis product surface; this catalog rejects cross-axis
// compositions that are unsound even when each axis is individually
// well-formed.
//
// The current C++ substrate does not yet carry a full Fixy body IR, so
// flow-sensitive rules are represented by explicit opt-in marker
// traits (`marks_async`, `marks_fail`, `marks_runtime_ghost_use`, ...).
// That keeps Phase 0 honest: source-visible annotations can trigger
// the rejection today, while future compiler passes can specialize the
// same traits from analyzed bodies without changing Fn's ABI.

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/Diagnostic.h>

#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace crucible::safety::fn::collision {

enum class RuleCode : std::uint8_t {
    I002 = 0,
    L002 = 1,
    E044 = 2,
    I003 = 3,
    M012 = 4,
    P002 = 5,
    I004 = 6,
    N002 = 7,
    L003 = 8,
    M011 = 9,
    S010 = 10,
    S011 = 11,
    None = 255,
};

struct I002_ClassifiedFailPayload : diag::tag_base {
    static constexpr std::string_view name = "I002_ClassifiedFailPayload";
};
struct L002_BorrowAsync : diag::tag_base {
    static constexpr std::string_view name = "L002_BorrowAsync";
};
struct E044_ConstantTimeAsync : diag::tag_base {
    static constexpr std::string_view name = "E044_ConstantTimeAsync";
};
struct I003_ConstantTimeFailOnSecret : diag::tag_base {
    static constexpr std::string_view name = "I003_ConstantTimeFailOnSecret";
};
struct M012_MonotonicConcurrentNoAtomic : diag::tag_base {
    static constexpr std::string_view name = "M012_MonotonicConcurrentNoAtomic";
};
struct P002_GhostRuntimeUse : diag::tag_base {
    static constexpr std::string_view name = "P002_GhostRuntimeUse";
};
struct I004_ClassifiedAsyncSession : diag::tag_base {
    static constexpr std::string_view name = "I004_ClassifiedAsyncSession";
};
struct N002_DecimalOverflowWrap : diag::tag_base {
    static constexpr std::string_view name = "N002_DecimalOverflowWrap";
};
struct L003_BorrowUnscopedSpawn : diag::tag_base {
    static constexpr std::string_view name = "L003_BorrowUnscopedSpawn";
};
struct M011_LinearFailNoCleanup : diag::tag_base {
    static constexpr std::string_view name = "M011_LinearFailNoCleanup";
};
struct S010_StalenessConstantTime : diag::tag_base {
    static constexpr std::string_view name = "S010_StalenessConstantTime";
};
struct S011_CapabilityReplay : diag::tag_base {
    static constexpr std::string_view name = "S011_CapabilityReplay";
};

using Catalog = std::tuple<
    I002_ClassifiedFailPayload,
    L002_BorrowAsync,
    E044_ConstantTimeAsync,
    I003_ConstantTimeFailOnSecret,
    M012_MonotonicConcurrentNoAtomic,
    P002_GhostRuntimeUse,
    I004_ClassifiedAsyncSession,
    N002_DecimalOverflowWrap,
    L003_BorrowUnscopedSpawn,
    M011_LinearFailNoCleanup,
    S010_StalenessConstantTime,
    S011_CapabilityReplay
>;

inline constexpr std::size_t catalog_size = std::tuple_size_v<Catalog>;
static_assert(catalog_size == 12);

template <RuleCode R>
struct rule_tag;

template <> struct rule_tag<RuleCode::I002> { using type = I002_ClassifiedFailPayload; };
template <> struct rule_tag<RuleCode::L002> { using type = L002_BorrowAsync; };
template <> struct rule_tag<RuleCode::E044> { using type = E044_ConstantTimeAsync; };
template <> struct rule_tag<RuleCode::I003> { using type = I003_ConstantTimeFailOnSecret; };
template <> struct rule_tag<RuleCode::M012> { using type = M012_MonotonicConcurrentNoAtomic; };
template <> struct rule_tag<RuleCode::P002> { using type = P002_GhostRuntimeUse; };
template <> struct rule_tag<RuleCode::I004> { using type = I004_ClassifiedAsyncSession; };
template <> struct rule_tag<RuleCode::N002> { using type = N002_DecimalOverflowWrap; };
template <> struct rule_tag<RuleCode::L003> { using type = L003_BorrowUnscopedSpawn; };
template <> struct rule_tag<RuleCode::M011> { using type = M011_LinearFailNoCleanup; };
template <> struct rule_tag<RuleCode::S010> { using type = S010_StalenessConstantTime; };
template <> struct rule_tag<RuleCode::S011> { using type = S011_CapabilityReplay; };

template <RuleCode R>
using rule_tag_t = typename rule_tag<R>::type;

template <typename Tag>
struct rule_code_of;

template <> struct rule_code_of<I002_ClassifiedFailPayload> {
    static constexpr RuleCode value = RuleCode::I002;
};
template <> struct rule_code_of<L002_BorrowAsync> {
    static constexpr RuleCode value = RuleCode::L002;
};
template <> struct rule_code_of<E044_ConstantTimeAsync> {
    static constexpr RuleCode value = RuleCode::E044;
};
template <> struct rule_code_of<I003_ConstantTimeFailOnSecret> {
    static constexpr RuleCode value = RuleCode::I003;
};
template <> struct rule_code_of<M012_MonotonicConcurrentNoAtomic> {
    static constexpr RuleCode value = RuleCode::M012;
};
template <> struct rule_code_of<P002_GhostRuntimeUse> {
    static constexpr RuleCode value = RuleCode::P002;
};
template <> struct rule_code_of<I004_ClassifiedAsyncSession> {
    static constexpr RuleCode value = RuleCode::I004;
};
template <> struct rule_code_of<N002_DecimalOverflowWrap> {
    static constexpr RuleCode value = RuleCode::N002;
};
template <> struct rule_code_of<L003_BorrowUnscopedSpawn> {
    static constexpr RuleCode value = RuleCode::L003;
};
template <> struct rule_code_of<M011_LinearFailNoCleanup> {
    static constexpr RuleCode value = RuleCode::M011;
};
template <> struct rule_code_of<S010_StalenessConstantTime> {
    static constexpr RuleCode value = RuleCode::S010;
};
template <> struct rule_code_of<S011_CapabilityReplay> {
    static constexpr RuleCode value = RuleCode::S011;
};

template <typename Tag>
inline constexpr RuleCode rule_code_of_v = rule_code_of<Tag>::value;

template <RuleCode R>
inline constexpr bool rule_bijection_v = rule_code_of_v<rule_tag_t<R>> == R;

template <typename F, RuleCode R>
struct CollisionDiagnosticByRule;

template <typename F>
struct CollisionDiagnosticByRule<F, RuleCode::None> {
    [[nodiscard]] static consteval RuleCode category() noexcept { return RuleCode::None; }
    [[nodiscard]] static consteval std::string_view rule_code() noexcept { return "OK"; }
    [[nodiscard]] static consteval std::string_view goal() noexcept {
        return "Fn grade composition satisfies every registered collision rule";
    }
    [[nodiscard]] static consteval std::string_view gap() noexcept {
        return "none";
    }
    [[nodiscard]] static consteval std::string_view suggestion() noexcept {
        return "no remediation required";
    }
    [[nodiscard]] static consteval std::string_view reference() noexcept {
        return "fixy.md §24.2";
    }
};

#define CRUCIBLE_COLLISION_DIAGNOSTIC(rule, code_text, goal_text, gap_text, suggestion_text, ref_text) \
    template <typename F>                                                                             \
    struct CollisionDiagnosticByRule<F, RuleCode::rule> {                                             \
        [[nodiscard]] static consteval RuleCode category() noexcept { return RuleCode::rule; }         \
        [[nodiscard]] static consteval std::string_view rule_code() noexcept { return code_text; }     \
        [[nodiscard]] static consteval std::string_view goal() noexcept { return goal_text; }          \
        [[nodiscard]] static consteval std::string_view gap() noexcept { return gap_text; }            \
        [[nodiscard]] static consteval std::string_view suggestion() noexcept { return suggestion_text; } \
        [[nodiscard]] static consteval std::string_view reference() noexcept { return ref_text; }      \
    }

CRUCIBLE_COLLISION_DIAGNOSTIC(I002, "I002", "classified error flow preserves secrecy",
    "classified value flows through Fail(E) with a non-secret error payload",
    "declare Fail(secret E), declassify explicitly, or remove Fail from the classified region",
    "fixy.md §24.2 I002");
CRUCIBLE_COLLISION_DIAGNOSTIC(L002, "L002", "borrow lifetime does not bridge await",
    "borrow capture is combined with async suspension",
    "scope the borrow before await, capture by value, or use structured tasks",
    "fixy.md §24.2 L002");
CRUCIBLE_COLLISION_DIAGNOSTIC(E044, "E044", "constant-time region has deterministic timing",
    "constant-time marker is combined with async scheduling",
    "run the CT core synchronously and wrap only the boundary in async",
    "fixy.md §24.2 E044");
CRUCIBLE_COLLISION_DIAGNOSTIC(I003, "I003", "secret-dependent failure is not observable",
    "constant-time function can fail on a secret-dependent condition",
    "use ct_select inside the secret region and fail after declassification",
    "fixy.md §24.2 I003");
CRUCIBLE_COLLISION_DIAGNOSTIC(M012, "M012", "concurrent monotonic update is atomic or merged",
    "monotonic mutation is used in a concurrent context without atomic representation",
    "use ReprKind::Atomic or safety::AtomicMonotonic<T, Cmp>",
    "fixy.md §24.2 M012");
CRUCIBLE_COLLISION_DIAGNOSTIC(P002, "P002", "ghost data stays erased",
    "ghost value is used by runtime code",
    "compute a runtime value separately or move the whole branch into ghost code",
    "fixy.md §24.2 P002");
CRUCIBLE_COLLISION_DIAGNOSTIC(I004, "I004", "classified session sends do not leak timing",
    "classified async session is declared without CT discipline",
    "wrap the classified send in a synchronous CT region or declassify before send",
    "fixy.md §24.2 I004");
CRUCIBLE_COLLISION_DIAGNOSTIC(N002, "N002", "decimal overflow mode is meaningful",
    "exact decimal type is combined with modular wrap overflow",
    "use trap, saturate, or widen for exact decimal arithmetic",
    "fixy.md §24.2 N002");
CRUCIBLE_COLLISION_DIAGNOSTIC(L003, "L003", "borrowed captures cannot outlive their scope",
    "borrow capture is combined with unscoped spawn",
    "use task_group, permission_fork, or move ownership into the closure",
    "fixy.md §24.2 L003");
CRUCIBLE_COLLISION_DIAGNOSTIC(M011, "M011", "linear resources are cleaned on fail paths",
    "linear value is live across Fail without cleanup",
    "register defer/errdefer, use RAII cleanup, or fail before acquiring the resource",
    "fixy.md §24.2 M011");
CRUCIBLE_COLLISION_DIAGNOSTIC(S010, "S010", "constant-time code has no freshness branch",
    "non-fresh staleness policy is combined with CT",
    "require stale::Fresh in CT code or remove the CT guarantee",
    "fixy.md §24.2 S010");
CRUCIBLE_COLLISION_DIAGNOSTIC(S011, "S011", "replay-stable code has reconstructable resources",
    "ephemeral capability is used in replay-required code without a stable handle",
    "use content-addressed handles or remove replay eligibility",
    "fixy.md §24.2 S011");

#undef CRUCIBLE_COLLISION_DIAGNOSTIC

template <typename F> struct marks_async                  : std::false_type {};
template <typename F> struct marks_ct                     : std::false_type {};
template <typename F> struct marks_fail                   : std::false_type {};
template <typename F> struct marks_fail_error_secret      : std::false_type {};
template <typename F> struct marks_fail_on_secret         : std::false_type {};
template <typename F> struct marks_concurrent_context     : std::false_type {};
template <typename F> struct marks_runtime_ghost_use      : std::false_type {};
template <typename F> struct marks_borrow_capture         : std::false_type {};
template <typename F> struct marks_unscoped_spawn         : std::false_type {};
template <typename F> struct marks_linear_uncleaned_fail  : std::false_type {};
template <typename F> struct marks_replay_required        : std::false_type {};
template <typename F> struct marks_replay_stable          : std::false_type {};

template <typename T> struct is_exact_decimal             : std::false_type {};

template <typename T> struct is_borrowed_carrier          : std::false_type {};
template <typename T> struct is_borrowed_carrier<BorrowedRef<T>> : std::true_type {};
template <typename T, typename Source>
struct is_borrowed_carrier<Borrowed<T, Source>> : std::true_type {};

template <typename Row, effects::Effect E>
inline constexpr bool row_has_effect_v = effects::row_contains_v<Row, E>;

template <typename F>
inline constexpr bool has_async_v = marks_async<F>::value;

template <typename F>
inline constexpr bool has_ct_v = marks_ct<F>::value;

template <typename F>
inline constexpr bool has_fail_v = marks_fail<F>::value;

template <typename F>
inline constexpr bool fail_error_secret_v = marks_fail_error_secret<F>::value;

template <typename F>
inline constexpr bool classified_v =
    F::security_v == SecLevel::Classified || F::security_v == SecLevel::Secret;

template <typename F>
inline constexpr bool has_borrow_capture_v =
    F::usage_v == UsageMode::Borrow ||
    is_borrowed_carrier<std::remove_cvref_t<typename F::type_t>>::value ||
    marks_borrow_capture<F>::value;

template <typename F>
inline constexpr bool concurrent_context_v =
    has_async_v<F> ||
    row_has_effect_v<typename F::effect_row_t, effects::Effect::Bg> ||
    marks_concurrent_context<F>::value;

template <typename F>
inline constexpr bool session_protocol_v =
    !std::is_same_v<typename F::protocol_t, proto::None>;

template <typename F>
concept I002_OK = !(classified_v<F> && has_fail_v<F> && !fail_error_secret_v<F>);

template <typename F>
concept L002_OK = !(has_borrow_capture_v<F> && has_async_v<F>);

template <typename F>
concept E044_OK = !(has_ct_v<F> && has_async_v<F>);

template <typename F>
concept I003_OK = !(has_ct_v<F> && has_fail_v<F> && marks_fail_on_secret<F>::value);

template <typename F>
concept M012_OK = !(F::mutation_v == MutationMode::Monotonic &&
                    concurrent_context_v<F> &&
                    F::repr_v != ReprKind::Atomic);

template <typename F>
concept P002_OK = !(F::usage_v == UsageMode::Ghost && marks_runtime_ghost_use<F>::value);

template <typename F>
concept I004_OK = !(classified_v<F> && has_async_v<F> &&
                    session_protocol_v<F> && !has_ct_v<F>);

template <typename F>
concept N002_OK = !(is_exact_decimal<std::remove_cvref_t<typename F::type_t>>::value &&
                    F::overflow_v == OverflowMode::Wrap);

template <typename F>
concept L003_OK = !(has_borrow_capture_v<F> && marks_unscoped_spawn<F>::value);

template <typename F>
concept M011_OK = !(F::usage_v == UsageMode::Linear &&
                    has_fail_v<F> &&
                    marks_linear_uncleaned_fail<F>::value);

template <typename F>
concept S010_OK = !(has_ct_v<F> &&
                    !std::is_same_v<typename F::staleness_t, stale::Fresh>);

template <typename F>
concept S011_OK = !(F::usage_v == UsageMode::Capability &&
                    marks_replay_required<F>::value &&
                    !marks_replay_stable<F>::value);

template <typename F>
concept AllRulesOK =
    I002_OK<F> && L002_OK<F> && E044_OK<F> && I003_OK<F> &&
    M012_OK<F> && P002_OK<F> && I004_OK<F> && N002_OK<F> &&
    L003_OK<F> && M011_OK<F> && S010_OK<F> && S011_OK<F>;

template <typename F>
[[nodiscard]] consteval RuleCode first_failure() noexcept {
    if constexpr (!I002_OK<F>) {
        return RuleCode::I002;
    } else if constexpr (!L002_OK<F>) {
        return RuleCode::L002;
    } else if constexpr (!E044_OK<F>) {
        return RuleCode::E044;
    } else if constexpr (!I003_OK<F>) {
        return RuleCode::I003;
    } else if constexpr (!M012_OK<F>) {
        return RuleCode::M012;
    } else if constexpr (!P002_OK<F>) {
        return RuleCode::P002;
    } else if constexpr (!I004_OK<F>) {
        return RuleCode::I004;
    } else if constexpr (!N002_OK<F>) {
        return RuleCode::N002;
    } else if constexpr (!L003_OK<F>) {
        return RuleCode::L003;
    } else if constexpr (!M011_OK<F>) {
        return RuleCode::M011;
    } else if constexpr (!S010_OK<F>) {
        return RuleCode::S010;
    } else if constexpr (!S011_OK<F>) {
        return RuleCode::S011;
    } else {
        return RuleCode::None;
    }
}

template <typename F>
inline constexpr RuleCode first_failure_v = first_failure<F>();

template <typename F>
struct CollisionDiagnostic
    : CollisionDiagnosticByRule<F, first_failure_v<F>> {};

}  // namespace crucible::safety::fn::collision

namespace crucible::safety::fn {

template <typename F>
struct CollisionRules {
    static constexpr bool valid = true;
};

template <
    typename       Type,
    typename       Refinement,
    UsageMode      Usage,
    typename       EffectRow,
    SecLevel       Security,
    typename       Protocol,
    typename       Lifetime,
    typename       Source,
    typename       Trust,
    ReprKind       Repr,
    typename       Cost,
    typename       Precision,
    typename       Space,
    OverflowMode   Overflow,
    MutationMode   Mutation,
    ReentrancyMode Reentrancy,
    typename       Size,
    std::uint32_t  Version,
    typename       Staleness
>
struct CollisionRules<Fn<Type, Refinement, Usage, EffectRow, Security,
                         Protocol, Lifetime, Source, Trust, Repr, Cost,
                         Precision, Space, Overflow, Mutation, Reentrancy,
                         Size, Version, Staleness>> {
    using F = Fn<Type, Refinement, Usage, EffectRow, Security,
                 Protocol, Lifetime, Source, Trust, Repr, Cost,
                 Precision, Space, Overflow, Mutation, Reentrancy,
                 Size, Version, Staleness>;

    static constexpr bool classified =
        Security == SecLevel::Classified || Security == SecLevel::Secret;
    static constexpr bool async = collision::marks_async<F>::value;
    static constexpr bool ct = collision::marks_ct<F>::value;
    static constexpr bool fail = collision::marks_fail<F>::value;
    static constexpr bool fail_secret = collision::marks_fail_error_secret<F>::value;
    static constexpr bool borrow_capture =
        Usage == UsageMode::Borrow ||
        collision::is_borrowed_carrier<std::remove_cvref_t<Type>>::value ||
        collision::marks_borrow_capture<F>::value;
    static constexpr bool concurrent =
        async ||
        collision::row_has_effect_v<EffectRow, effects::Effect::Bg> ||
        collision::marks_concurrent_context<F>::value;
    static constexpr bool session_protocol = !std::is_same_v<Protocol, proto::None>;
    static constexpr bool stale_nonfresh = !std::is_same_v<Staleness, stale::Fresh>;

    [[nodiscard]] static consteval bool validate() noexcept {
        static_assert(!(classified && fail && !fail_secret),
            "I002: classified value cannot flow through Fail(E) with "
            "non-secret error payload. Declare Fail(secret E), declassify "
            "explicitly, or remove Fail from the classified region.");
        static_assert(!(borrow_capture && async),
            "L002: borrow x Async incompatible. Borrow lifetime cannot "
            "bridge await suspension; scope the borrow before await or "
            "capture by value.");
        static_assert(!(ct && async),
            "E044: CT x Async incompatible. Async scheduling defeats "
            "the constant-time guarantee; keep the CT core synchronous.");
        static_assert(!(ct && fail && collision::marks_fail_on_secret<F>::value),
            "I003: CT x Fail-on-secret incompatible. Do not expose a "
            "secret-dependent branch through failure; use ct_select first.");
        static_assert(!(Mutation == MutationMode::Monotonic &&
                        concurrent &&
                        Repr != ReprKind::Atomic),
            "M012: monotonic x concurrent without atomic representation. "
            "Use ReprKind::Atomic or safety::AtomicMonotonic<T, Cmp>.");
        static_assert(!(Usage == UsageMode::Ghost &&
                        collision::marks_runtime_ghost_use<F>::value),
            "P002: ghost x runtime incompatible. Ghost values are erased "
            "and cannot drive runtime branches, indexes, or returns.");
        static_assert(!(classified && async && session_protocol && !ct),
            "I004: classified x async x session without CT leaks timing. "
            "Use a synchronous CT send region or explicitly declassify.");
        static_assert(!(collision::is_exact_decimal<std::remove_cvref_t<Type>>::value &&
                        Overflow == OverflowMode::Wrap),
            "N002: decimal x overflow(wrap) is invalid. Exact decimal "
            "types must use trap, saturate, or widen semantics.");
        static_assert(!(borrow_capture && collision::marks_unscoped_spawn<F>::value),
            "L003: borrow x unscoped spawn incompatible. Use task_group, "
            "permission_fork, or move ownership into the spawned closure.");
        static_assert(!(Usage == UsageMode::Linear &&
                        fail &&
                        collision::marks_linear_uncleaned_fail<F>::value),
            "M011: linear x Fail without cleanup leaks a linear resource. "
            "Register defer/errdefer, use RAII cleanup, or fail before acquire.");
        static_assert(!(ct && stale_nonfresh),
            "S010: Staleness x CT incompatible. Runtime freshness checks "
            "defeat constant-time timing; require stale::Fresh or drop CT.");
        static_assert(!(Usage == UsageMode::Capability &&
                        collision::marks_replay_required<F>::value &&
                        !collision::marks_replay_stable<F>::value),
            "S011: Capability x Replay incompatible. Ephemeral capabilities "
            "must not enter replay-stable code without content-addressed handles.");
        return !(classified && fail && !fail_secret) &&
               !(borrow_capture && async) &&
               !(ct && async) &&
               !(ct && fail && collision::marks_fail_on_secret<F>::value) &&
               !(Mutation == MutationMode::Monotonic && concurrent &&
                 Repr != ReprKind::Atomic) &&
               !(Usage == UsageMode::Ghost &&
                 collision::marks_runtime_ghost_use<F>::value) &&
               !(classified && async && session_protocol && !ct) &&
               !(collision::is_exact_decimal<std::remove_cvref_t<Type>>::value &&
                 Overflow == OverflowMode::Wrap) &&
               !(borrow_capture && collision::marks_unscoped_spawn<F>::value) &&
               !(Usage == UsageMode::Linear && fail &&
                 collision::marks_linear_uncleaned_fail<F>::value) &&
               !(ct && stale_nonfresh) &&
               !(Usage == UsageMode::Capability &&
                 collision::marks_replay_required<F>::value &&
                 !collision::marks_replay_stable<F>::value);
    }

    static constexpr bool valid = validate();
};

namespace detail::collision_catalog_self_test {

struct FreshCtMarker {};
using FreshCtCarrier = Fn<FreshCtMarker>;

}  // namespace detail::collision_catalog_self_test

namespace collision {

template <>
struct marks_ct<detail::collision_catalog_self_test::FreshCtCarrier> : std::true_type {};

}  // namespace collision

namespace detail::collision_catalog_self_test {

using DefaultFn = Fn<int>;
static_assert(ValidComposition<DefaultFn>);
static_assert(collision::catalog_size == 12);
static_assert(std::is_same_v<
    collision::rule_tag_t<collision::RuleCode::I002>,
    collision::I002_ClassifiedFailPayload>);
static_assert(collision::rule_code_of_v<collision::I002_ClassifiedFailPayload>
              == collision::RuleCode::I002);
static_assert(collision::rule_bijection_v<collision::RuleCode::I002>);
static_assert(collision::rule_bijection_v<collision::RuleCode::L002>);
static_assert(collision::rule_bijection_v<collision::RuleCode::E044>);
static_assert(collision::rule_bijection_v<collision::RuleCode::I003>);
static_assert(collision::rule_bijection_v<collision::RuleCode::M012>);
static_assert(collision::rule_bijection_v<collision::RuleCode::P002>);
static_assert(collision::rule_bijection_v<collision::RuleCode::I004>);
static_assert(collision::rule_bijection_v<collision::RuleCode::N002>);
static_assert(collision::rule_bijection_v<collision::RuleCode::L003>);
static_assert(collision::rule_bijection_v<collision::RuleCode::M011>);
static_assert(collision::rule_bijection_v<collision::RuleCode::S010>);
static_assert(collision::rule_bijection_v<collision::RuleCode::S011>);
static_assert(collision::CollisionDiagnosticByRule<DefaultFn, collision::RuleCode::I002>::rule_code()
              == std::string_view{"I002"});
static_assert(collision::CollisionDiagnostic<DefaultFn>::category()
              == collision::RuleCode::None);
static_assert(collision::CollisionDiagnostic<DefaultFn>::rule_code()
              == std::string_view{"OK"});

using ConcurrentAtomic = Fn<int, pred::True, UsageMode::Linear,
                            effects::Row<effects::Effect::Bg>,
                            SecLevel::Public, proto::None, lifetime::Static,
                            source::FromInternal, trust::Verified,
                            ReprKind::Atomic, cost::Unstated,
                            precision::Exact, space::Zero,
                            OverflowMode::Trap, MutationMode::Monotonic>;
static_assert(ValidComposition<ConcurrentAtomic>);

static_assert(ValidComposition<FreshCtCarrier>);

}  // namespace detail::collision_catalog_self_test

}  // namespace crucible::safety::fn

#endif  // CRUCIBLE_SAFETY_COLLISION_CATALOG_BODY
#endif  // CRUCIBLE_SAFETY_FN_COLLISION_CATALOG_INTEGRATION
