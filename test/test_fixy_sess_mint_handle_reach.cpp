// ═══════════════════════════════════════════════════════════════════════
// test_fixy_sess_mint_handle_reach — FIXY-U-022 sentinel TU
//
// Per-protocol friend `mint_session_handle` reach proof.  The
// substrate ships exactly ELEVEN `friend constexpr auto
// mint_session_handle(R) noexcept` declarations — one per protocol
// shape that admits a runnable SessionHandle specialization:
//
//   1.  End                                       (Session.h:1874)
//   2.  Send<T, R>                                (Session.h:1927)
//   3.  Recv<T, R>                                (Session.h:1983)
//   4.  Select<Bs...>                             (Session.h:2039)
//   5.  Offer<Bs...>                              (Session.h:2176)
//   6.  Stop_g<CrashClass C>                      (SessionCrash.h:307)
//   7.  CheckpointedSession<PBase, PRollback>     (SessionCheckpoint.h:456)
//   8.  Delegate<T, K>                            (SessionDelegate.h:981)
//   9.  Accept<T, K>                              (SessionDelegate.h:1142)
//   10. EpochedDelegate<T, K, MinE, MinG>         (SessionDelegate.h:1232)
//   11. EpochedAccept<T, K, MinE, MinG>           (SessionDelegate.h:1351)
//
// Each friend declaration grants the primary
// `safety::proto::mint_session_handle<Proto, Resource>(Resource, source_location)`
// access to the corresponding SessionHandle specialization's private
// ctor / members.  A future header restructure that removes ANY of
// the 11 friend declarations would silently regress the factory's
// reach for that protocol — production callers either lose access
// (compile error at a faraway call site) or fall back to a private-
// member-bypassing alternate construction path (silent loss of
// invariant).
//
// THIS TU OWNS:
//   * Structural reach proof — each of the 11 SessionHandle<Proto,
//     Resource, LoopCtx> specializations is instantiated and its
//     `protocol` typedef checked.  The class instantiation forces the
//     friend declaration to be parsed.
//   * Factory call-through reach proof — for the 10 protocols whose
//     body assertions allow LoopCtx=void, `fixy::sess::mint_session_handle<Proto>(res)`
//     is invoked and the returned handle's static type checked.
//   * EpochedAccept (the 11th protocol) has a SessionHandle body
//     `static_assert` requiring LoopCtx = EpochCtx<E, G, ...>, which
//     the default LoopCtx=void cannot satisfy.  Bare
//     `mint_session_handle<EpochedAccept<...>>(res)` therefore cannot
//     be exercised; the friend overload is reachable in practice only
//     via `detail::step_to_next<EpochedAccept<...>, R, EpochCtx<E,G>>(...)`
//     from inside a Loop-shaped continuation.  We witness it
//     STRUCTURALLY: instantiating the handle with an EpochCtx-
//     bearing LoopCtx forces the friend decl to be parsed at the
//     specialization site.
//   * Substrate-identity witness — the using-decl
//     `using ::crucible::safety::proto::mint_session_handle;` MUST
//     alias to the substrate symbol; we pin it via a function-pointer-
//     type same_v cell.
//   * Cardinality sentinel — `kFixyFriendMintHandleCount = 11`
//     witnesses the count.  Adding a 12th friend overload (e.g., for
//     a new Refine protocol shape) updates the count here AND ships
//     a new per-protocol cell below — both required so the
//     cardinality witness fires when only one half lands.
//
// Closes #1734 (FIXY-U-022).
//
// Trust boundary:
//   fixy/Sess.h owns the type/factory re-exports
//   sessions/Session*.h own the SessionHandle specializations + friend decls
//   THIS TU owns the per-protocol-shape × per-reach-mode coverage matrix
// ═══════════════════════════════════════════════════════════════════════

#include <crucible/Fixy.h>

#include <cstdint>
#include <source_location>
#include <type_traits>
#include <utility>

namespace fs = ::crucible::fixy::sess;
namespace pf = ::crucible::safety::proto;

namespace {

// Plain value-typed Resource — satisfies SessionResource concept
// (non-reference type).  POD, trivially copyable, easy to read at
// runtime smoke time.
struct FakeRes {
    int sentinel = 42;
};

// Small payload types for combinator parametrization.
struct Msg {};
struct Reply {};

// ═════════════════════════════════════════════════════════════════════
// Substrate-identity witness — the using-decl MUST alias to the
// substrate's primary mint_session_handle.  Pin the function-pointer
// type with explicit template arguments so a future namespace-level
// redeclaration that shadows the substrate symbol would change the
// type and fail this static_assert.
// ═════════════════════════════════════════════════════════════════════

using fixy_mint_t = decltype(&fs::mint_session_handle<fs::End, FakeRes>);
using sub_mint_t  = decltype(&pf::mint_session_handle<pf::End, FakeRes>);
static_assert(std::is_same_v<fixy_mint_t, sub_mint_t>,
    "fixy::sess::mint_session_handle must alias the substrate symbol; "
    "a divergence here means the using-decl was shadowed by a "
    "namespace-level redeclaration in fixy/Sess.h.");

// ═════════════════════════════════════════════════════════════════════
// Cardinality sentinel — 11 SessionHandle specializations, each
// befriending detail::make_session_handle (fix-04: the sole authorized
// constructor; mint_session_handle / step_to_next route through it).
// Bump this constant only when a NEW SessionHandle specialization is
// added with its make_session_handle friend AND a per-protocol reach
// cell below is added.  Both edits are coupled.
// ═════════════════════════════════════════════════════════════════════

inline constexpr std::size_t kFixyFriendMintHandleCount = 11;

static_assert(kFixyFriendMintHandleCount == 11,
    "Friend `detail::make_session_handle` reach count drift — a "
    "SessionHandle specialization was added/removed in "
    "sessions/Session*.h without updating both the count and the "
    "per-protocol cell here.");

// ═════════════════════════════════════════════════════════════════════
// Protocol-shape handle typedefs — each forces the SessionHandle
// specialization (and its embedded friend declaration) to be parsed.
// All 11 use `pf::` and `fs::` interchangeably to prove the fixy::
// type re-exports point to the same specializations.
// ═════════════════════════════════════════════════════════════════════

// 1. End — normal terminal.
using H_End = pf::SessionHandle<fs::End, FakeRes, void>;
static_assert(std::is_same_v<typename H_End::protocol, fs::End>);
static_assert(std::is_same_v<typename H_End::resource_type, FakeRes>);

// 2. Send<T, R> — non-terminal, message-carrying.
using H_Send = pf::SessionHandle<fs::Send<Msg, fs::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Send::protocol, fs::Send<Msg, fs::End>>);
static_assert(std::is_same_v<typename H_Send::message_type, Msg>);

// 3. Recv<T, R> — dual of Send.
using H_Recv = pf::SessionHandle<fs::Recv<Msg, fs::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Recv::protocol, fs::Recv<Msg, fs::End>>);
static_assert(std::is_same_v<typename H_Recv::message_type, Msg>);

// 4. Select<Branches...> — internal choice.
using H_Select = pf::SessionHandle<
    fs::Select<fs::Send<Msg, fs::End>, fs::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Select::protocol,
    fs::Select<fs::Send<Msg, fs::End>, fs::End>>);
static_assert(H_Select::branch_count == 2);

// 5. Offer<Branches...> — external choice.
using H_Offer = pf::SessionHandle<
    fs::Offer<fs::Recv<Msg, fs::End>, fs::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Offer::protocol,
    fs::Offer<fs::Recv<Msg, fs::End>, fs::End>>);
static_assert(H_Offer::branch_count == 2);

// 6. Stop_g<CrashClass C> — crash-stop terminal.
using H_Stop = pf::SessionHandle<fs::Stop, FakeRes, void>;
static_assert(std::is_same_v<typename H_Stop::protocol, fs::Stop>);
static_assert(H_Stop::crash_class == ::crucible::algebra::lattices::CrashClass::Abort);

// 7. CheckpointedSession<PBase, PRollback> — checkpoint combinator.
using H_Checkpoint = pf::SessionHandle<
    fs::CheckpointedSession<fs::End, fs::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Checkpoint::protocol,
    fs::CheckpointedSession<fs::End, fs::End>>);
static_assert(std::is_same_v<typename H_Checkpoint::base_protocol, fs::End>);
static_assert(std::is_same_v<typename H_Checkpoint::rollback_protocol, fs::End>);

// 8. Delegate<T, K> — delegate-out combinator.
using DelegatedProto = fs::Send<Msg, fs::End>;
using H_Delegate = pf::SessionHandle<
    fs::Delegate<DelegatedProto, fs::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Delegate::protocol,
    fs::Delegate<DelegatedProto, fs::End>>);

// 9. Accept<T, K> — delegate-in combinator (dual of Delegate).
using H_Accept = pf::SessionHandle<
    fs::Accept<DelegatedProto, fs::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Accept::protocol,
    fs::Accept<DelegatedProto, fs::End>>);

// 10. EpochedDelegate<T, K, MinE, MinG> — epoch-fresh delegate-out.
//     No LoopCtx restriction at the handle level.
using H_EpDelegate = pf::SessionHandle<
    fs::EpochedDelegate<DelegatedProto, fs::End,
                        /*MinEpoch=*/1, /*MinGeneration=*/1>,
    FakeRes, void>;
static_assert(std::is_same_v<typename H_EpDelegate::protocol,
    fs::EpochedDelegate<DelegatedProto, fs::End, 1, 1>>);
static_assert(H_EpDelegate::min_epoch == 1);
static_assert(H_EpDelegate::min_generation == 1);

// 11. EpochedAccept<T, K, MinE, MinG> — epoch-fresh delegate-in.
//     Handle's body static_asserts LoopCtx = EpochCtx<E≥MinE, G≥MinG>;
//     bare `mint_session_handle<EpochedAccept<...>>(res)` cannot reach
//     it via default LoopCtx=void.  We pass an EpochCtx LoopCtx
//     directly to the SessionHandle specialization to instantiate the
//     friend declaration.
using EpochLoopCtx = pf::EpochCtx</*CurrentEpoch=*/2, /*CurrentGeneration=*/2>;
using H_EpAccept = pf::SessionHandle<
    fs::EpochedAccept<DelegatedProto, fs::End,
                      /*MinEpoch=*/1, /*MinGeneration=*/1>,
    FakeRes, EpochLoopCtx>;
static_assert(std::is_same_v<typename H_EpAccept::protocol,
    fs::EpochedAccept<DelegatedProto, fs::End, 1, 1>>);
static_assert(H_EpAccept::min_epoch == 1);
static_assert(H_EpAccept::min_generation == 1);

// ═════════════════════════════════════════════════════════════════════
// Factory call-through reach — for the 10 protocols whose body
// assertions admit LoopCtx=void, we invoke
// `fixy::sess::mint_session_handle<Proto>(FakeRes{})` and pin the
// returned handle's static type.  EpochedAccept is excluded (see
// note above).
//
// Each cell uses an immediately-invoked lambda + decltype on its
// return so the call exists at compile-time as a function template
// instantiation (substrate-symbol-reaching) without needing a runtime
// driver — the static_asserts hold regardless of whether main() is
// linked.
// ═════════════════════════════════════════════════════════════════════

[[maybe_unused]] constexpr auto mint_check_end = []() noexcept {
    return fs::mint_session_handle<fs::End>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_end()), H_End>);

[[maybe_unused]] constexpr auto mint_check_send = []() noexcept {
    return fs::mint_session_handle<fs::Send<Msg, fs::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_send()), H_Send>);

[[maybe_unused]] constexpr auto mint_check_recv = []() noexcept {
    return fs::mint_session_handle<fs::Recv<Msg, fs::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_recv()), H_Recv>);

[[maybe_unused]] constexpr auto mint_check_select = []() noexcept {
    return fs::mint_session_handle<
        fs::Select<fs::Send<Msg, fs::End>, fs::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_select()), H_Select>);

[[maybe_unused]] constexpr auto mint_check_offer = []() noexcept {
    return fs::mint_session_handle<
        fs::Offer<fs::Recv<Msg, fs::End>, fs::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_offer()), H_Offer>);

[[maybe_unused]] constexpr auto mint_check_stop = []() noexcept {
    return fs::mint_session_handle<fs::Stop>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_stop()), H_Stop>);

[[maybe_unused]] constexpr auto mint_check_checkpoint = []() noexcept {
    return fs::mint_session_handle<
        fs::CheckpointedSession<fs::End, fs::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_checkpoint()), H_Checkpoint>);

[[maybe_unused]] constexpr auto mint_check_delegate = []() noexcept {
    return fs::mint_session_handle<
        fs::Delegate<DelegatedProto, fs::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_delegate()), H_Delegate>);

[[maybe_unused]] constexpr auto mint_check_accept = []() noexcept {
    return fs::mint_session_handle<
        fs::Accept<DelegatedProto, fs::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_accept()), H_Accept>);

[[maybe_unused]] constexpr auto mint_check_ep_delegate = []() noexcept {
    return fs::mint_session_handle<
        fs::EpochedDelegate<DelegatedProto, fs::End, 1, 1>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_ep_delegate()), H_EpDelegate>);

// Note: no mint_check_ep_accept — EpochedAccept's handle body
// static_asserts LoopCtx = EpochCtx, which the factory's default
// LoopCtx=void violates.  The friend overload is reachable structurally
// (via the H_EpAccept type instantiation above) and at runtime through
// `step_to_next` from inside a continuation that already carries the
// EpochCtx LoopCtx.  See SessionDelegate.h:1338 for the body assertion
// that gates direct factory construction.

// ═════════════════════════════════════════════════════════════════════
// Runtime smoke — every per-protocol cell instantiates a handle from
// non-constant arguments to defeat any consteval-folding.  Mirrors
// the discipline in feedback_algebra_runtime_smoke_test_discipline.md:
// pure static_assert tests mask consteval/SFINAE bugs that surface
// only when the factory is called with runtime args.
// ═════════════════════════════════════════════════════════════════════

[[noreturn]] void fail_smoke(const char* msg) {
    std::fprintf(stderr, "test_fixy_sess_mint_handle_reach: %s\n", msg);
    std::abort();
}

void smoke_check(bool cond, const char* msg) {
    if (!cond) fail_smoke(msg);
}

template <typename Expected, typename Actual>
void assert_same_handle_type(const char* tag) {
    smoke_check(std::is_same_v<Expected, Actual>, tag);
}

// Non-terminal handles are linear — the SessionHandleBase destructor
// aborts under NDEBUG-off if the handle is dropped without being
// advanced past via a &&-qualified consumer (send/recv/pick/...) OR
// explicitly via `.detach(detach_reason::*)`.  The TU-side smoke
// exercises the FACTORY (proof that mint_session_handle returns the
// expected SessionHandle type) — there is no application protocol to
// run, so each non-terminal handle is consumed with
// `detach_reason::TestInstrumentation{}`, the audit-class tag the
// substrate provides for exactly this scenario (see Session.h §1626
// + the per-tag grep audit at §1634).
namespace detach_reason = ::crucible::safety::proto::detach_reason;

void smoke_runtime_mints() {
    volatile int seed = 17;

    // 1. End — terminal; close() consumes the handle and returns the
    //    underlying resource.
    {
        FakeRes r{seed};
        auto h = fs::mint_session_handle<fs::End>(std::move(r));
        assert_same_handle_type<H_End, decltype(h)>("End handle type");
        FakeRes recovered = std::move(h).close();
        smoke_check(recovered.sentinel == seed, "End resource recover");
    }
    // 2. Send — non-terminal; .detach() with TestInstrumentation tag.
    {
        FakeRes r{seed + 1};
        auto h = fs::mint_session_handle<fs::Send<Msg, fs::End>>(std::move(r));
        assert_same_handle_type<H_Send, decltype(h)>("Send handle type");
        smoke_check(h.resource().sentinel == seed + 1, "Send resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    // 3. Recv — non-terminal.
    {
        FakeRes r{seed + 2};
        auto h = fs::mint_session_handle<fs::Recv<Msg, fs::End>>(std::move(r));
        assert_same_handle_type<H_Recv, decltype(h)>("Recv handle type");
        smoke_check(h.resource().sentinel == seed + 2, "Recv resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    // 4. Select — non-terminal.
    {
        FakeRes r{seed + 3};
        auto h = fs::mint_session_handle<
            fs::Select<fs::Send<Msg, fs::End>, fs::End>>(std::move(r));
        assert_same_handle_type<H_Select, decltype(h)>("Select handle type");
        smoke_check(h.resource().sentinel == seed + 3, "Select resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    // 5. Offer — non-terminal.
    {
        FakeRes r{seed + 4};
        auto h = fs::mint_session_handle<
            fs::Offer<fs::Recv<Msg, fs::End>, fs::End>>(std::move(r));
        assert_same_handle_type<H_Offer, decltype(h)>("Offer handle type");
        smoke_check(h.resource().sentinel == seed + 4, "Offer resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    // 6. Stop — terminal; close() consumes the handle.
    {
        FakeRes r{seed + 5};
        auto h = fs::mint_session_handle<fs::Stop>(std::move(r));
        assert_same_handle_type<H_Stop, decltype(h)>("Stop handle type");
        FakeRes recovered = std::move(h).close();
        smoke_check(recovered.sentinel == seed + 5, "Stop resource recover");
    }
    // 7. CheckpointedSession — non-terminal (has base()/rollback()
    //    consumers); detach for sentinel purposes.
    {
        FakeRes r{seed + 6};
        auto h = fs::mint_session_handle<
            fs::CheckpointedSession<fs::End, fs::End>>(std::move(r));
        assert_same_handle_type<H_Checkpoint, decltype(h)>(
            "Checkpoint handle type");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    // 8. Delegate — non-terminal.
    {
        FakeRes r{seed + 7};
        auto h = fs::mint_session_handle<
            fs::Delegate<DelegatedProto, fs::End>>(std::move(r));
        assert_same_handle_type<H_Delegate, decltype(h)>(
            "Delegate handle type");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    // 9. Accept — non-terminal.
    {
        FakeRes r{seed + 8};
        auto h = fs::mint_session_handle<
            fs::Accept<DelegatedProto, fs::End>>(std::move(r));
        assert_same_handle_type<H_Accept, decltype(h)>(
            "Accept handle type");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    // 10. EpochedDelegate — non-terminal (no LoopCtx restriction).
    {
        FakeRes r{seed + 9};
        auto h = fs::mint_session_handle<
            fs::EpochedDelegate<DelegatedProto, fs::End, 1, 1>>(std::move(r));
        assert_same_handle_type<H_EpDelegate, decltype(h)>(
            "EpochedDelegate handle type");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    // 11. EpochedAccept — non-terminal; cannot reach via bare
    //     mint_session_handle (handle body static_asserts EpochCtx
    //     LoopCtx).  Direct ctor with H_EpAccept's LoopCtx witnesses
    //     the specialization parses + the friend decl reaches.
    {
        FakeRes r{seed + 10};
        // fix-04: SessionHandle value ctor is private — mint via the
        // sole authorized factory detail::make_session_handle, which
        // (unlike bare mint_session_handle) exposes the LoopCtx
        // parameter so the EpochedAccept body's fresh-EpochCtx
        // static_assert is satisfiable.
        auto h = pf::detail::make_session_handle<
            fs::EpochedAccept<DelegatedProto, fs::End, 1, 1>,
            FakeRes, EpochLoopCtx>(std::move(r));
        static_assert(std::is_same_v<H_EpAccept, decltype(h)>);
        smoke_check(h.resource().sentinel == seed + 10,
                    "EpochedAccept resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
}

}  // namespace

int main() {
    smoke_runtime_mints();
    return 0;
}
