// Attacks on ownership transfer that use the discipline correctly.
//
// Every attack here is legal code through the public surface: no
// reinterpret_cast, no const_cast, no undefined behavior, no namespace
// reopened and no friend door.  test_cheat_probe.cpp covers those.  Each
// attack tries to end with a permission lost or held twice, a read proof
// that outlives its source, or a token on the wire that no set records.
//
// An attack that the discipline refuses is a static assertion here, or a
// negative fixture when the refusal is a compile error.  An attack that
// compiles and goes wrong is a finding.  It is fixed, or it is pinned on
// the ledger at the foot, which names the attack and the condition of the
// literature that it breaks.  The ledger can only shrink: each entry
// asserts that its attack still compiles, so a repair fails the entry.
//
// The run-time attacks run under a watchdog that aborts with a
// diagnostic, so a lost wakeup fails the test and does not hang it.

#include <fixy/Secret.h>
#include <fixy/session/Classified.h>
#include <fixy/session/Payload.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <any>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace ownership_attacks {

struct X {
    using permission_row = ::foundation::effects::Row<>;
};
struct Y {
    using permission_row = ::foundation::effects::Row<>;
};
using XAlias = X;

using TX = sess::Transferable<int, X>;
using TY = sess::Transferable<int, Y>;
using BX = sess::Borrowed<int, X>;
using RX = sess::Released<int, X>;

using Refusal = sess::detail::PayloadRefusal;

template <class P>
inline constexpr Refusal refusal_of = sess::detail::payload_verdict(^^P).refusal;

template <class P, class... Tags>
inline constexpr bool sender_requires_exactly =
    fp::perm_set_equal_v<typename sess::payload_perm_delta<P>::sender_requires, fp::PermSet<Tags...>>;

template <class P, class... Tags>
inline constexpr bool receiver_gains_exactly =
    fp::perm_set_equal_v<typename sess::payload_perm_delta<P>::receiver_gains, fp::PermSet<Tags...>>;

// ── Laundering a token through a container or a wrapper ─────────────
//
// Each shape below tries to move a token with no set change.  The walk
// reads every component, so each one is either counted, which is not a
// laundering, or refused with the reason that names the shape.

template <class U>
struct Wrap {
    U inner;
};
struct HoldsPointer {
    TX* token;
};
struct HoldsReference {
    TX& token;
};
struct HoldsArray {
    TX tokens[2];
};
struct HoldsPrivate {
    explicit HoldsPrivate(TX t) : token{std::move(t)} {}

private:
    TX token;
};
struct HoldsCompactToken {
    [[no_unique_address]] fp::Permission<X> token;
    int value = 0;
};
struct Derived : TX {
    using TX::TX;
};
struct HoldsView {
    fp::ReadView<X> view;
};
struct HoldsStatic {
    static TX token;
    int value = 0;
};
union RawUnion {
    fp::Permission<X> token;
    int value;
    ~RawUnion() {}
};
struct Declared;

// Counted, not laundered: the walk reaches the token owned.
static_assert(sender_requires_exactly<std::tuple<TX, int>, X>);
static_assert(sender_requires_exactly<std::pair<int, TX>, X>);
static_assert(sender_requires_exactly<Wrap<Wrap<Wrap<TX>>>, X>);
static_assert(sender_requires_exactly<Derived, X>, "a base is owned, so inheritance moves the token");
static_assert(sender_requires_exactly<HoldsPrivate, X>, "a private member is read, so access hides nothing");
static_assert(sender_requires_exactly<HoldsCompactToken, X>);
static_assert(sender_requires_exactly<fp::Permission<X>, X>, "a bare token moves as a Transferable does");
static_assert(sender_requires_exactly<std::tuple<TX, TY>, X, Y>);
static_assert(sender_requires_exactly<sess::Transferable<TY, X>, X, Y>, "a token inside a marker is counted too");
static_assert(sender_requires_exactly<sess::Borrowed<TY, X>, X, Y>);
static_assert(receiver_gains_exactly<sess::Borrowed<TY, X>, sess::BorrowedIn<X>, Y>);

// Refused behind a pointer or a reference: a second name for a token
// that stays with the sender.
static_assert(refusal_of<TX*> == Refusal::TokenBehindPointer);
static_assert(refusal_of<TX&> == Refusal::TokenBehindPointer, "a payload typed as a reference moves nothing");
static_assert(refusal_of<const TX&> == Refusal::TokenBehindPointer);
static_assert(refusal_of<TX&&> == Refusal::TokenBehindPointer);
static_assert(refusal_of<HoldsPointer> == Refusal::TokenBehindPointer);
static_assert(refusal_of<HoldsReference> == Refusal::TokenBehindPointer);
static_assert(refusal_of<std::unique_ptr<TX>> == Refusal::TokenBehindPointer);
static_assert(refusal_of<std::shared_ptr<TX>> == Refusal::TokenBehindPointer);
static_assert(refusal_of<std::weak_ptr<TX>> == Refusal::TokenBehindPointer);
static_assert(refusal_of<std::vector<TX>> == Refusal::TokenBehindPointer);
static_assert(refusal_of<std::span<TX>> == Refusal::TokenBehindPointer);
static_assert(refusal_of<std::reference_wrapper<TX>> == Refusal::TokenBehindPointer);
static_assert(refusal_of<std::atomic<TX*>> == Refusal::TokenBehindPointer);
static_assert(refusal_of<std::pair<std::unique_ptr<TX>, int>> == Refusal::TokenBehindPointer);

// Refused in a union: the token can be absent at run time.
static_assert(refusal_of<std::optional<TX>> == Refusal::TokenInUnion);
static_assert(refusal_of<std::variant<TX, int>> == Refusal::TokenInUnion);
static_assert(refusal_of<std::expected<TX, int>> == Refusal::TokenInUnion);
static_assert(refusal_of<std::expected<int, TX>> == Refusal::TokenInUnion);
static_assert(refusal_of<RawUnion> == Refusal::TokenInUnion);
static_assert(refusal_of<std::tuple<std::optional<TX>>> == Refusal::TokenInUnion);

// Refused in an array: several tokens of one tag.
static_assert(refusal_of<std::array<TX, 2>> == Refusal::TokenInArray);
static_assert(refusal_of<std::array<TX, 1>> == Refusal::TokenInArray);
static_assert(refusal_of<HoldsArray> == Refusal::TokenInArray);
static_assert(refusal_of<TX[1]> == Refusal::TokenInArray);

// Refused behind type erasure: the static type does not say what it
// holds.
static_assert(refusal_of<std::function<void()>> == Refusal::TypeErasure);
static_assert(refusal_of<std::move_only_function<void()>> == Refusal::TypeErasure);
static_assert(refusal_of<std::copyable_function<void()>> == Refusal::TypeErasure);
static_assert(refusal_of<std::function_ref<void()>> == Refusal::TypeErasure);
static_assert(refusal_of<std::any> == Refusal::TypeErasure);
static_assert(refusal_of<std::tuple<int, std::function<void()>>> == Refusal::TypeErasure);

// Refused inside a lambda with captures: the captures cannot be read.
// The refusal does not depend on what the lambda captures, so a lambda
// that captures only an int is refused as well.  That is conservative.
inline auto capture_by_move(TX token) {
    return [held = std::move(token)] { return held.value; };
}
inline auto capture_by_reference(TX& token) {
    return [&token] { return token.value; };
}
inline auto capture_plain(int value) {
    return [value] { return value; };
}
inline auto capture_nothing() {
    return [] { return 3; };
}
static_assert(refusal_of<decltype(capture_by_move(std::declval<TX>()))> == Refusal::UnreadableState);
static_assert(refusal_of<decltype(capture_by_reference(std::declval<TX&>()))> == Refusal::UnreadableState);
static_assert(refusal_of<decltype(capture_plain(0))> == Refusal::UnreadableState);
static_assert(refusal_of<decltype(capture_nothing())> == Refusal::None, "a lambda with no state carries nothing");

// Refused as only declared: nothing says what it holds.
static_assert(refusal_of<Declared*> == Refusal::IncompleteType);

// Refused as one tag twice.
static_assert(refusal_of<std::pair<TX, TX>> == Refusal::DuplicateTag);
static_assert(refusal_of<std::pair<TX, BX>> == Refusal::DuplicateTag, "a move and a loan of one region");
static_assert(refusal_of<std::tuple<TX, sess::Returned<int, X>>> == Refusal::DuplicateTag);
static_assert(refusal_of<std::pair<TX, sess::Transferable<int, XAlias>>> == Refusal::DuplicateTag,
              "an alias of a tag is the same tag");
static_assert(refusal_of<std::pair<fp::Permission<X>, TX>> == Refusal::DuplicateTag);
static_assert(refusal_of<std::pair<TX, sess::DelegatedSession<int, fp::PermSet<X>>>> == Refusal::DuplicateTag,
              "a delegated endpoint carries its tags");

// Refused as a read proof or a share outside its marker.
static_assert(refusal_of<fp::ReadView<X>> == Refusal::BareBorrowOrShare);
static_assert(refusal_of<HoldsView> == Refusal::BareBorrowOrShare);
static_assert(refusal_of<std::pair<int, fp::ReadView<X>>> == Refusal::BareBorrowOrShare);
static_assert(refusal_of<fp::SharedPermission<X, ::foundation::brand::DefaultBrand>> == Refusal::BareBorrowOrShare);
static_assert(refusal_of<fp::SharedPermissionGuard<X, ::foundation::brand::DefaultBrand>>
              == Refusal::BareBorrowOrShare);

// No false refusal: plain values travel.
static_assert(sess::is_plain_payload_v<int>);
static_assert(sess::is_plain_payload_v<std::optional<int>>);
static_assert(sess::is_plain_payload_v<std::variant<int, double>>);
static_assert(sess::is_plain_payload_v<std::string>);
static_assert(sess::is_plain_payload_v<std::vector<int>>);
static_assert(sess::is_plain_payload_v<std::array<int, 4>>);
static_assert(sess::is_plain_payload_v<HoldsStatic>, "a static member does not travel with the value");
static_assert(sess::is_plain_payload_v<int*>);

// ── Classified values on the channel ────────────────────────────────
//
// A classified value that travels bare leaves classification with no
// named policy.  A constant-time value that travels bare offers == and
// element access, which can branch on the content.

struct [[=sess::constant_time_value{}]] AuthTag {
    std::byte bytes[16]{};
};
struct [[=sess::constant_time_value{}]] SignedAuth {
    AuthTag tag;
    std::byte nonce[8]{};
};
struct NotMarked {
    std::byte bytes[16]{};
};
using Wire = ::fixy::tags::secret_policy::WireSerialize;

static_assert(sess::ConstantTimeValue<AuthTag> && sess::ConstantTimeValue<SignedAuth>);
static_assert(!sess::ConstantTimeValue<NotMarked> && !sess::ConstantTimeValue<int>);
static_assert(!sess::ConstantTimeValue<sess::CTPayload<AuthTag>>, "the carrier is not itself marked");

static_assert(refusal_of<::fixy::Secret<int>> == Refusal::ClassifiedBare);
static_assert(refusal_of<std::pair<int, ::fixy::Secret<int>>> == Refusal::ClassifiedBare);
static_assert(refusal_of<std::optional<::fixy::Secret<int>>> == Refusal::ClassifiedBare);
static_assert(refusal_of<Wrap<Wrap<::fixy::Secret<int>>>> == Refusal::ClassifiedBare);
static_assert(refusal_of<sess::DeclassifyOnSend<int, Wire>> == Refusal::None);
static_assert(refusal_of<sess::DeclassifyOnSend<TX*, Wire>> == Refusal::TokenBehindPointer,
              "the carried value is walked for tokens");
static_assert(refusal_of<AuthTag> == Refusal::ConstantTimeBare);
static_assert(refusal_of<std::pair<int, AuthTag>> == Refusal::ConstantTimeBare);
static_assert(refusal_of<std::array<AuthTag, 2>> == Refusal::ConstantTimeBare);
static_assert(refusal_of<Wrap<AuthTag>> == Refusal::ConstantTimeBare);
static_assert(refusal_of<sess::CTPayload<AuthTag>> == Refusal::None);
static_assert(refusal_of<sess::CTPayload<SignedAuth>> == Refusal::None, "the parts of a carried value are carried");
static_assert(refusal_of<NotMarked> == Refusal::None);

template <class T>
concept ComparesByValue = requires(const T& lhs, const T& rhs) { lhs == rhs; };
template <class T, class Policy>
concept DeclassifiesUnder = requires { typename sess::DeclassifyOnSend<T, Policy>; };
template <class T>
concept CarriedAsCT = requires { typename sess::CTPayload<T>; };
struct UnadmittedPolicy : ::fixy::tags::secret_policy::secret_policy_base {};

static_assert(!ComparesByValue<sess::CTPayload<AuthTag>>, "== on a constant-time carrier is deleted");
static_assert(!DeclassifiesUnder<int, UnadmittedPolicy>, "a policy with no admitted edge is refused");
static_assert(DeclassifiesUnder<int, Wire>);
static_assert(!CarriedAsCT<NotMarked>, "only a marked type travels as CTPayload");
static_assert(!std::is_copy_constructible_v<sess::CTPayload<AuthTag>>);
static_assert(!std::is_copy_constructible_v<sess::DeclassifyOnSend<int, Wire>>);

// ── Moving, lending and releasing against the set ───────────────────

template <class... Tags>
using PS = fp::PermSet<Tags...>;
using sess::BorrowedIn;
using sess::LentOut;

// A token the sender does not hold.
static_assert(!sess::SendablePayload<TX, PS<>>);
// A token the sender lent out: the borrowed prefix holds the suffix.
static_assert(!sess::SendablePayload<TX, PS<LentOut<X>>>);
// A second loan of a region that is lent.
static_assert(!sess::SendablePayload<BX, PS<LentOut<X>>>);
// A release by an endpoint that holds no loan, and by the owner.
static_assert(!sess::SendablePayload<RX, PS<>>);
static_assert(!sess::SendablePayload<RX, PS<X>>);
static_assert(!sess::SendablePayload<RX, PS<LentOut<X>>>);
// A borrower that forwards the loan: it holds BorrowedIn<X>, not X.
static_assert(!sess::SendablePayload<BX, PS<BorrowedIn<X>>>);
static_assert(!sess::SendablePayload<TX, PS<BorrowedIn<X>>>);
// A release that nobody lent.
static_assert(!sess::ReceivablePayload<RX, PS<>>);
static_assert(!sess::ReceivablePayload<RX, PS<X>>);
// A token that the recipient already holds, lends or borrows.
static_assert(!sess::ReceivablePayload<TX, PS<X>>);
static_assert(!sess::ReceivablePayload<TX, PS<LentOut<X>>>);
static_assert(!sess::ReceivablePayload<TX, PS<BorrowedIn<X>>>);
// A loan of a region that the recipient holds, or borrows already.
static_assert(!sess::ReceivablePayload<BX, PS<X>>);
static_assert(!sess::ReceivablePayload<BX, PS<BorrowedIn<X>>>);
// Unrelated regions do not block each other.
static_assert(sess::SendablePayload<TX, PS<X, BorrowedIn<Y>>>);
static_assert(sess::ReceivablePayload<TX, PS<LentOut<Y>>>);

// The loan round trip.  The second release has nothing to release.
using LenderAfterLend = sess::perm_set_after_send_t<PS<X>, BX>;
using BorrowerAfterLoan = sess::perm_set_after_recv_t<PS<>, BX>;
using BorrowerAfterRelease = sess::perm_set_after_send_t<BorrowerAfterLoan, RX>;
using LenderAfterRelease = sess::perm_set_after_recv_t<LenderAfterLend, RX>;
static_assert(fp::perm_set_equal_v<LenderAfterLend, PS<LentOut<X>>>);
static_assert(fp::perm_set_equal_v<BorrowerAfterLoan, PS<BorrowedIn<X>>>);
static_assert(fp::perm_set_equal_v<BorrowerAfterRelease, PS<>>);
static_assert(fp::perm_set_equal_v<LenderAfterRelease, PS<X>>);
static_assert(!sess::SendablePayload<RX, BorrowerAfterRelease>, "a loan is released once");
static_assert(!sess::ReceivablePayload<RX, LenderAfterRelease>, "a loan is closed once");
static_assert(sess::perm_set_has_open_loan_v<LenderAfterLend>);
static_assert(sess::perm_set_has_open_loan_v<BorrowerAfterLoan>);
static_assert(!sess::perm_set_has_open_loan_v<LenderAfterRelease>);

// A move and a loan in one message.
using Mixed = std::pair<TX, sess::Borrowed<int, Y>>;
static_assert(fp::perm_set_equal_v<sess::perm_set_after_send_t<PS<X, Y>, Mixed>, PS<LentOut<Y>>>);
static_assert(fp::perm_set_equal_v<sess::perm_set_after_recv_t<PS<>, Mixed>, PS<X, BorrowedIn<Y>>>);

// ── Forging a token or a proof with no constructor ──────────────────

template <class T>
concept BuiltFromBytes = requires(std::array<std::byte, sizeof(T)> bytes) { std::bit_cast<T>(bytes); };

static_assert(!BuiltFromBytes<fp::Permission<X>>);
static_assert(!BuiltFromBytes<fp::perm_mint_key>);
static_assert(!BuiltFromBytes<fp::ReadView<X>>);
static_assert(!BuiltFromBytes<TX>, "a copy of a Transferable by bytes is a second token");
static_assert(!BuiltFromBytes<BX>, "a copy of a Borrowed by bytes is a second loan");
static_assert(!std::is_implicit_lifetime_v<fp::Permission<X>>);
static_assert(!std::is_implicit_lifetime_v<fp::perm_mint_key>);
static_assert(!std::is_implicit_lifetime_v<fp::ReadView<X>>);
static_assert(!std::is_implicit_lifetime_v<TX>);
static_assert(!std::is_implicit_lifetime_v<BX>);
// A marker cannot be built without its token or its proof.
static_assert(!std::is_constructible_v<TX, int>);
static_assert(!std::is_constructible_v<BX, int>);
static_assert(!std::is_constructible_v<BX, int, fp::ReadView<Y>>, "a proof of one region does not lend another");
static_assert(!std::is_default_constructible_v<fp::ReadView<X>>);
static_assert(!std::is_copy_constructible_v<TX> && !std::is_copy_constructible_v<BX>);

// ── Attacks on the hold ─────────────────────────────────────────────
//
// A PermHold keeps the physical tokens of a set.  Each transition is a
// member that the set gates, so an attack on the hold is a call that must
// not resolve.

template <class... Tags>
using Hold = sess::PermHold<PS<Tags...>>;

template <class H, class Tag>
concept CanTake = requires(H hold) { std::move(hold).template take<Tag>(); };
template <class H, class Tag>
concept CanPack = requires(H hold) { std::move(hold).template pack<Tag>(0); };
template <class H, class Tag>
concept CanLend = requires(H hold) { std::move(hold).template lend<Tag>(0); };
template <class H, class Message>
concept CanEndLoan = requires(H hold, Message message) { std::move(hold).end_loan(std::move(message)); };
template <class H, class Tag>
concept CanRelease = requires(H hold) { std::move(hold).template release<Tag>(0); };
template <class H, class Message>
concept CanUnpack = requires(H hold, Message message) { std::move(hold).unpack(std::move(message)); };
template <class H, class Message>
concept CanAcceptLoan = requires(H hold, Message message) { std::move(hold).accept_loan(std::move(message)); };
template <class H>
concept CanEnd = requires(H hold) { std::move(hold).into_permissions(); };
template <class... Args>
concept CanMintHold = requires(Args&&... args) { sess::mint_permission_hold(std::forward<Args>(args)...); };

// A token the hold does not have, and a token that is lent out.
static_assert(!CanTake<Hold<Y>, X>);
static_assert(!CanTake<Hold<LentOut<X>>, X>, "a lent token is parked, and take cannot reach it");
static_assert(!CanTake<Hold<LentOut<X>>, LentOut<X>>, "a loan state is not a token");
static_assert(!CanTake<Hold<BorrowedIn<X>>, X>);
// A move of a lent or borrowed region.
static_assert(!CanPack<Hold<LentOut<X>>, X>, "the borrowed prefix holds the move of the region");
static_assert(!CanPack<Hold<BorrowedIn<X>>, X>, "a borrower cannot give away what it borrows");
// A second loan, and a loan of a borrowed region.
static_assert(!CanLend<Hold<LentOut<X>>, X>);
static_assert(!CanLend<Hold<BorrowedIn<X>>, X>, "a borrower cannot lend what it borrows");
// A loan end that nobody lent, and a release that nobody borrowed.
static_assert(!CanEndLoan<Hold<X>, RX>);
static_assert(!CanEndLoan<Hold<>, RX>);
static_assert(!CanRelease<Hold<X>, X>);
static_assert(!CanRelease<Hold<LentOut<X>>, X>);
// A second release: the hold after the first one has no loan.
static_assert(!CanRelease<Hold<>, X>, "a loan is released once");
// A token of a region the recipient holds in some state.
static_assert(!CanUnpack<Hold<X>, TX>);
static_assert(!CanUnpack<Hold<LentOut<X>>, TX>);
static_assert(!CanUnpack<Hold<BorrowedIn<X>>, TX>);
static_assert(!CanAcceptLoan<Hold<X>, BX>);
static_assert(!CanAcceptLoan<Hold<BorrowedIn<X>>, BX>, "a region is borrowed once");
// The end of a hold with a loan open, on either side.
static_assert(!CanEnd<Hold<LentOut<X>>>);
static_assert(!CanEnd<Hold<BorrowedIn<X>>>);
static_assert(CanEnd<Hold<X, Y>>);
// A token passed by name, and one tag twice.
static_assert(!CanMintHold<fp::Permission<X>&>, "a token named after the hold took it is a second owner");
static_assert(!CanMintHold<const fp::Permission<X>&>);
static_assert(!CanMintHold<fp::Permission<X>, fp::Permission<X>>, "one tag twice");
static_assert(CanMintHold<fp::Permission<X>, fp::Permission<Y>>);
// A hold is not copied, not assigned, and not built from bytes.
static_assert(!std::is_copy_constructible_v<Hold<X>> && !std::is_move_assignable_v<Hold<X>>);
static_assert(!BuiltFromBytes<Hold<X>> && !std::is_implicit_lifetime_v<Hold<X>>);
static_assert(!std::is_copy_constructible_v<sess::SharedReader<X>>);

// ── The run-time attacks ────────────────────────────────────────────

namespace {

// Aborts the process when the guarded scope does not end in time.  A
// hang in an attack is then a failure with a name, not a stuck CI job.
class Watchdog {
public:
    Watchdog(const char* what, std::chrono::seconds limit)
        : what_{what},
          limit_{limit},
          thread_{[this](std::stop_token stop) {
              const auto deadline = std::chrono::steady_clock::now() + limit_;
              while (!stop.stop_requested()) {
                  if (std::chrono::steady_clock::now() > deadline) {
                      std::fprintf(stderr, "watchdog: %s did not finish in %lld s\n", what_,
                                   static_cast<long long>(limit_.count()));
                      std::abort();
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds{10});
              }
          }} {}

    Watchdog(const Watchdog&) = delete("one watchdog guards one scope");
    Watchdog& operator=(const Watchdog&) = delete("one watchdog guards one scope");

private:
    const char* what_;
    std::chrono::seconds limit_;
    std::jthread thread_;
};

int failures = 0;

void require(bool condition, const char* what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

// Readers hold shares while a writer tries to upgrade.  No upgrade can
// succeed while one share is out, so no read overlaps a write through a
// guard.
void readers_hold_past_an_upgrade_attempt() {
    const Watchdog watchdog{"readers_hold_past_an_upgrade_attempt", std::chrono::seconds{20}};
    fp::SharedPermissionPool pool{fp::mint_permission_root<X>()};
    constexpr int kReaders = 4;
    std::atomic<int> holding{0};
    std::atomic<bool> release{false};
    {
        std::vector<std::jthread> readers;
        for (int reader = 0; reader < kReaders; ++reader) {
            readers.emplace_back([&] {
                auto guard = pool.lend();
                require(guard.has_value(), "a reader gets a share while no writer holds the region");
                holding.fetch_add(1, std::memory_order_acq_rel);
                while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
            });
        }
        while (holding.load(std::memory_order_acquire) != kReaders) std::this_thread::yield();
        for (int attempt = 0; attempt < 1000; ++attempt) {
            require(!pool.try_upgrade().has_value(), "an upgrade succeeds while shares are out");
        }
        release.store(true, std::memory_order_release);
    }
    auto exclusive = pool.try_upgrade();
    require(exclusive.has_value(), "the upgrade succeeds once every share is back");
    require(!pool.lend().has_value(), "a reader gets a share while the writer holds the region");
    pool.deposit_exclusive(std::move(*exclusive));
}

// Readers take and drop shares in a loop while a writer upgrades and
// deposits in a loop.  Each side checks that it never sees the other in
// the region.
void readers_and_writer_race() {
    const Watchdog watchdog{"readers_and_writer_race", std::chrono::seconds{30}};
    fp::SharedPermissionPool pool{fp::mint_permission_root<X>()};
    std::atomic<int> readers_inside{0};
    std::atomic<bool> writer_inside{false};
    std::atomic<bool> stop{false};
    std::atomic<int> overlaps{0};
    constexpr int kWrites = 2000;
    {
        std::vector<std::jthread> readers;
        for (int reader = 0; reader < 3; ++reader) {
            readers.emplace_back([&] {
                while (!stop.load(std::memory_order_acquire)) {
                    auto guard = pool.lend();
                    if (!guard) continue;
                    readers_inside.fetch_add(1, std::memory_order_acq_rel);
                    if (writer_inside.load(std::memory_order_acquire)) overlaps.fetch_add(1);
                    readers_inside.fetch_sub(1, std::memory_order_acq_rel);
                }
            });
        }
        int writes = 0;
        while (writes < kWrites) {
            auto exclusive = pool.try_upgrade();
            if (!exclusive) {
                std::this_thread::yield();
                continue;
            }
            writer_inside.store(true, std::memory_order_release);
            if (readers_inside.load(std::memory_order_acquire) != 0) overlaps.fetch_add(1);
            writer_inside.store(false, std::memory_order_release);
            pool.deposit_exclusive(std::move(*exclusive));
            ++writes;
        }
        stop.store(true, std::memory_order_release);
    }
    require(overlaps.load() == 0, "a reader and the writer were in the region together");
}

}  // namespace

namespace {

// A one-slot channel between two threads, with a bounded wait.  The
// watchdog of the caller turns a lost message into an abort.
template <class T>
class Slot {
public:
    void put(T value) {
        value_.emplace(std::move(value));
        full_.store(true, std::memory_order_release);
    }
    [[nodiscard]] T take() {
        while (!full_.load(std::memory_order_acquire)) std::this_thread::yield();
        T value = std::move(*value_);
        value_.reset();
        full_.store(false, std::memory_order_release);
        return value;
    }

private:
    std::optional<T> value_;
    std::atomic<bool> full_{false};
};

// The region is written only through its token and read only through a
// proof, so the type of each access says who may make it.
int region_value = 0;
void write_region(fp::Permission<X>& token, int value) {
    (void)token;
    region_value = value;
}
int read_region(fp::ReadView<X> proof) {
    (void)proof;
    return region_value;
}

// A borrowed prefix on thread B while thread A runs the suffix.  A lends
// X to B and continues.  A's hold then has LentOut<X>, so no call of A
// reaches the token until the release comes back.  Under tsan, a write by
// A during the loan would race with the read by B.
void borrowed_prefix_on_another_thread() {
    const Watchdog watchdog{"borrowed_prefix_on_another_thread", std::chrono::seconds{20}};
    auto hold = sess::mint_permission_hold(fp::mint_permission_root<X>());
    auto token_first = std::move(hold).template take<X>();
    write_region(token_first.first, 10);
    auto lender = std::move(token_first.second).put(std::move(token_first.first));

    Slot<BX> to_borrower;
    Slot<RX> to_lender;
    auto [loan, lent] = std::move(lender).template lend<X>(1);
    static_assert(!CanTake<decltype(lent), X>, "the suffix of A cannot reach the lent token");
    std::jthread borrower_thread{[&] {
        auto borrower = sess::mint_permission_hold();
        auto [ticket, borrowing] = std::move(borrower).accept_loan(to_borrower.take());
        const int seen = read_region(borrowing.template view<X>());
        auto [release, done] = std::move(borrowing).template release<X>(seen + ticket);
        to_lender.put(std::move(release));
        auto nothing = std::move(done).into_permissions();
        (void)nothing;
    }};
    to_borrower.put(std::move(loan));
    auto [answer, closed] = std::move(lent).end_loan(to_lender.take());
    require(answer == 11, "the borrower read the value written before the loan");
    auto [token, empty] = std::move(closed).template take<X>();
    write_region(token, 20);
    require(read_region(fp::mint_read_view(token)) == 20, "the lender writes after the loan closes");
    auto ended = std::move(empty).into_permissions();
    (void)ended;
}

}  // namespace

// ── The ledger of attacks that compile and go wrong ─────────────────
//
// Each entry pins an attack that the discipline does not refuse.  The
// pin asserts that the attack still compiles, so a repair turns the pin
// red and the entry must go.

// A read proof outlives its source.  A ReadView is an empty value with
// no link to the Permission or the guard it came from, and it copies.  A
// copy taken from a Borrowed outlives the Released that ends the loan,
// and a copy taken under a share guard outlives the guard and the
// upgrade that follows it.  This breaks the rule of Saffrich, Spaderna,
// Thiemann and Vasconcelos, OOPSLA 2025, that a borrow ends when it is
// returned, and the CLASS rule, ESOP 2023, that no reader acts after the
// writer takes the region.  The set accounting of the session layer
// records the loan, but a copy of the proof is outside the set.
template <class Tag>
concept ReadProofOutlivesItsSource =
    requires(BX& loan) { fp::ReadView<Tag>{loan.view}; } && std::is_copy_constructible_v<fp::ReadView<Tag>>;

// The same token moves twice.  A Permission is empty, so a moved-from
// token is indistinguishable from a live one, and nothing diagnoses a
// second use after std::move.  Two markers then carry one region.  This
// breaks the exclusive points-to of Actris 2.0 (Hinrichsen, Bengtson,
// Krebbers, LMCS 2022): a resource owned twice.  Permission.h states
// this limit as the linearity decision of the tree.
template <class Tag>
concept TokenMovesTwice = requires(fp::Permission<Tag>& token) {
    sess::Transferable<int, Tag>{1, std::move(token)};
    sess::Transferable<int, Tag>{2, std::move(token)};
};

static_assert(ReadProofOutlivesItsSource<X>);
static_assert(TokenMovesTwice<X>);

inline constexpr std::size_t kLedgerSize = 2;
static_assert(kLedgerSize == 2, "the ledger only shrinks.  A new entry needs a review of why it cannot be refused.");

namespace {

// The first entry, run through a hold: a borrower copies its proof, and
// the copy survives the release and the end of the loan.
void pinned_read_proof_outlives_loan() {
    auto lender = sess::mint_permission_hold(fp::mint_permission_root<X>());
    auto [loan, lent] = std::move(lender).template lend<X>(0);
    auto [value, borrowing] = sess::mint_permission_hold().accept_loan(std::move(loan));
    const fp::ReadView<X> kept = borrowing.template view<X>();
    auto [release, done] = std::move(borrowing).template release<X>(value);
    auto [back, closed] = std::move(lent).end_loan(std::move(release));
    auto [token, empty] = std::move(closed).template take<X>();
    (void)back;
    require(read_region(kept) == region_value,
            "pinned: a read proof copied during a loan still reads after the lender took its token back");
    write_region(token, 0);
    auto rest = std::move(empty).into_permissions();
    auto nothing = std::move(done).into_permissions();
    (void)rest;
    (void)nothing;
}

// The first entry, run: a proof survives an upgrade.
void pinned_read_proof_outlives_upgrade() {
    fp::SharedPermissionPool pool{fp::mint_permission_root<X>()};
    std::optional<fp::ReadView<X>> kept;
    {
        auto guard = pool.lend();
        kept.emplace(fp::mint_read_view(*guard));
    }
    auto exclusive = pool.try_upgrade();
    require(exclusive.has_value() && kept.has_value(),
            "pinned: a read proof copied under a share outlives the upgrade that ends every share");
    pool.deposit_exclusive(std::move(*exclusive));
}

}  // namespace

}  // namespace ownership_attacks

int main() {
    using namespace ownership_attacks;
    readers_hold_past_an_upgrade_attempt();
    readers_and_writer_race();
    borrowed_prefix_on_another_thread();
    pinned_read_proof_outlives_upgrade();
    pinned_read_proof_outlives_loan();
    if (failures != 0) {
        std::fprintf(stderr, "test_session_ownership_attacks: %d failures\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_session_ownership_attacks: every refused attack refused, %zu pinned on the ledger\n",
                kLedgerSize);
    return EXIT_SUCCESS;
}
