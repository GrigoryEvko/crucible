// What fixy/session/Checkpoint.h claims, checked.
//
// The compile-time half checks the compliance verdict against the
// Video-on-Demand example of Mezzina, Tiezzi and Yoshida (LMCS 2025,
// Sec. 2 and Example 4.4): the runs of Fig. 1(b) and Fig. 1(d) are not
// roll-safe, and the run of Fig. 1(c) is.  Each primitive becomes a
// label here, so every commit, roll and abort of the paper is a Select
// on one side and an Offer on the other.  It then checks each way to
// write a choice that one side makes alone, and each is refused.
//
// The runtime half runs Fig. 1(c) on both endpoints.  The user picks HD
// and rolls back, picks SD and aborts, and then picks SD again and
// watches the video.  After the roll and after the abort, the handle
// types are the types at the checkpoint and at the start, which is how
// the test shows that both sides reverted to the same pair.

#include <fixy/Core.h>
#include <fixy/session/Checkpoint.h>
#include <fixy/session/Watch.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <cstdio>
#include <deque>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace s = fixy::session;
namespace eff = ::foundation::effects;

// The verdict is a function that is not a template, and the boolean verdict
// is a concept, so no specialization changes either answer.  The fixture
// neg_sess_checkpoint_verdict_of_specialized tries the function.
static_assert(
    std::meta::is_concept(^^s::checkpoint_compliant_v),
    "checkpoint_compliant_v must stay a concept, so that no specialization admits a pair that is not compliant");

namespace {

// The context of each session in this file.  No payload of the example
// carries an effect row, so the background context admits it.
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
using BgIoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO>>;
[[nodiscard]] BgCtx bg_ctx() noexcept { return BgCtx{eff::testing::bg()}; }

// The payload row walk of fixy/concurrent/PayloadRow.h reads no
// std::string_view, so the text travels as a pointer to its characters.
struct Text {
    const char* value = "";
    [[nodiscard]] std::string_view view() const noexcept { return value; }
};
struct Region {};

// ── The Video-on-Demand runs ────────────────────────────────────────
//
// U is the user and S the service.  U sends a request and receives a
// price.  Then U receives metadata, picks HD or SD, receives a test
// video, and either receives the video or undoes the choice: a roll
// after HD, an abort after SD.

// Fig. 1(b): U commits after the price, S commits after the quality
// choice.  S's commit imposes a checkpoint on U, so U's roll fails.
using UserB = s::Send<
    Text,
    s::Recv<int,
            s::Select<s::Commit<s::Recv<
                Text, s::Select<s::Offer<s::Commit<s::Recv<Text, s::Select<s::Recv<Text, s::End>, s::Roll>>>>,
                                s::Offer<s::Commit<s::Recv<Text, s::Select<s::Recv<Text, s::End>, s::Abort>>>>>>>>>>;
using ServiceB = s::Recv<
    Text,
    s::Send<int,
            s::Offer<s::Commit<s::Send<
                Text, s::Offer<s::Select<s::Commit<s::Send<Text, s::Offer<s::Send<Text, s::End>, s::Roll>>>>,
                               s::Select<s::Commit<s::Send<Text, s::Offer<s::Send<Text, s::End>, s::Abort>>>>>>>>>>;

static_assert(s::checkpoint_verdict_of(^^UserB, ^^ServiceB) == s::CheckpointVerdict::RollToImposedCheckpoint);

// Fig. 1(c): S commits after it sends the price, and then U commits.  U
// set the last checkpoint, so U's roll finds a checkpoint it owns.
using UserC =
    s::Send<Text, s::Recv<int, s::Offer<s::Commit<s::Select<s::Commit<s::Recv<
                                   Text, s::Select<s::Recv<Text, s::Select<s::Recv<Text, s::End>, s::Roll>>,
                                                   s::Recv<Text, s::Select<s::Recv<Text, s::End>, s::Abort>>>>>>>>>>;
using ServiceC =
    s::Recv<Text, s::Send<int, s::Select<s::Commit<s::Offer<s::Commit<s::Send<
                                   Text, s::Offer<s::Send<Text, s::Offer<s::Send<Text, s::End>, s::Roll>>,
                                                  s::Send<Text, s::Offer<s::Send<Text, s::End>, s::Abort>>>>>>>>>>;

static_assert(s::checkpoint_verdict_of(^^UserC, ^^ServiceC) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^ServiceC, ^^UserC) == s::CheckpointVerdict::Compliant);
static_assert(s::CheckpointSessionAdmissible<UserC, ServiceC>);

// Fig. 1(d): both commit after the metadata, U first.  S's commit comes
// last and imposes a checkpoint on U.
using UserD =
    s::Send<Text, s::Recv<int, s::Recv<Text, s::Select<s::Commit<s::Offer<s::Commit<s::Select<
                                                 s::Recv<Text, s::Select<s::Recv<Text, s::End>, s::Roll>>,
                                                 s::Recv<Text, s::Select<s::Recv<Text, s::End>, s::Abort>>>>>>>>>>;
using ServiceD =
    s::Recv<Text, s::Send<int, s::Send<Text, s::Offer<s::Commit<s::Select<s::Commit<s::Offer<
                                                 s::Send<Text, s::Offer<s::Send<Text, s::End>, s::Roll>>,
                                                 s::Send<Text, s::Offer<s::Send<Text, s::End>, s::Abort>>>>>>>>>>;

static_assert(s::checkpoint_verdict_of(^^UserD, ^^ServiceD) == s::CheckpointVerdict::RollToImposedCheckpoint);

// ── One side choosing alone ─────────────────────────────────────────

using Decide = s::Select<s::Commit<s::Send<int, s::End>>, s::Roll>;
using Follow = s::Offer<s::Commit<s::Recv<int, s::End>>, s::Roll>;
static_assert(s::checkpoint_verdict_of(^^Decide, ^^Follow) == s::CheckpointVerdict::Compliant);
static_assert(std::is_same_v<s::dual_of_t<Decide>, s::Offer<s::Commit<s::Recv<int, s::End>>, s::Roll>>);

// The two sides disagree about which label is the rollback.
static_assert(s::checkpoint_verdict_of(^^Decide, ^^s::Offer<s::Roll, s::Commit<s::Recv<int, s::End>>>)
              == s::CheckpointVerdict::LabelsDisagree);
// Both sides decide, so no label is ever received.
static_assert(s::checkpoint_verdict_of(^^Decide, ^^Decide) == s::CheckpointVerdict::Stuck);
// A primitive outside a choice is a decision that one side takes alone.
static_assert(s::checkpoint_verdict_of(^^s::Send<int, s::Roll>, ^^s::Recv<int, s::Roll>)
              == s::CheckpointVerdict::NotCheckpointShaped);
static_assert(s::checkpoint_verdict_of(^^s::Commit<s::End>, ^^s::Commit<s::End>)
              == s::CheckpointVerdict::NotCheckpointShaped);
// A label the receiver cannot take.
static_assert(s::checkpoint_verdict_of(^^s::Select<s::End, s::Abort>, ^^s::Offer<s::End>)
              == s::CheckpointVerdict::LabelOutOfRange);
// The plain factory refuses a checkpoint protocol on one endpoint.
static_assert(!s::is_well_formed_v<Decide>);

// A permission cannot cross a checkpoint session, and neither can a
// crash record.
static_assert(s::checkpoint_verdict_of(^^s::Select<s::Commit<s::Send<s::Transferable<int, Region>, s::End>>, s::Roll>,
                                       ^^s::Offer<s::Commit<s::Recv<s::Transferable<int, Region>, s::End>>, s::Roll>)
              == s::CheckpointVerdict::NotCheckpointShaped);
struct Peer {};
static_assert(s::checkpoint_verdict_of(^^s::Select<s::Commit<s::Recv<s::Crash<Peer>, s::End>>, s::Roll>,
                                       ^^s::Offer<s::Commit<s::Send<s::Crash<Peer>, s::End>>, s::Roll>)
              == s::CheckpointVerdict::NotCheckpointShaped);

// The passive party of a commit that later rolls is the error of
// Fig. 11, rule TS-Rll2.
static_assert(s::checkpoint_verdict_of(^^s::Offer<s::Commit<s::Select<s::End, s::Roll>>>,
                                       ^^s::Select<s::Commit<s::Offer<s::End, s::Roll>>>)
              == s::CheckpointVerdict::RollToImposedCheckpoint);

// A Sender note on a Select names the endpoint that picks.  The erasure
// keeps the note and erases the branches only, as it does for an Offer.
static_assert(
    std::is_same_v<s::checkpoint_erase_t<s::Select<s::Sender<Peer>, s::Commit<s::Send<int, s::End>>, s::Roll>>,
                   s::Select<s::Sender<Peer>, s::Send<int, s::End>, s::End>>);
static_assert(
    std::is_same_v<s::checkpoint_erase_t<s::Select<s::Sender<Peer>, s::End>>, s::Select<s::Sender<Peer>, s::End>>);

// The handle steps the erased protocol, and the erasure decides where the
// handle is terminal.  A branch that commits and then sends is not a
// terminal position.  A branch that commits and ends, rolls or aborts is.
static_assert(!s::is_terminal_state_v<s::checkpoint_erase_t<s::Commit<s::Send<int, s::End>>>>);
static_assert(s::is_terminal_state_v<s::checkpoint_erase_t<s::Commit<s::End>>>);
static_assert(s::is_terminal_state_v<s::checkpoint_erase_t<s::Roll>>
              && s::is_terminal_state_v<s::checkpoint_erase_t<s::Abort>>);

// A loop that rolls back to a checkpoint inside it.
using LoopDecide = s::Loop<s::Select<s::Commit<s::Send<int, s::Continue>>, s::Roll, s::End>>;
using LoopFollow = s::Loop<s::Offer<s::Commit<s::Recv<int, s::Continue>>, s::Roll, s::End>>;
static_assert(s::checkpoint_verdict_of(^^LoopDecide, ^^LoopFollow) == s::CheckpointVerdict::Compliant);

// ── A primitive under a vendor pin ──────────────────────────────────
//
// The check reads each position under its vendor pins.  So a pinned Roll
// rolls back, a pinned Abort starts again, a pinned Commit saves its
// continuation, and a pinned End closes, on the side that selects and on the
// side that offers.  The runtime half below runs each pair.  No pair carries
// a payload, so each step of a pair is a label.
template <typename P>
using Pinned = s::VendorPinned<s::VendorBackend::NV, P>;

using CommitDecide = s::Select<s::Commit<s::End>, s::Roll>;
using CommitFollow = s::Offer<s::Commit<s::End>, s::Roll>;
using PinnedRollDecide = s::Select<s::Commit<s::End>, Pinned<s::Roll>>;
using PinnedRollFollow = s::Offer<s::Commit<s::End>, Pinned<s::Roll>>;
using TwoPinsDecide = s::Select<s::Commit<s::End>, s::VendorPinned<s::VendorBackend::CPU, Pinned<s::Roll>>>;
using PinnedStartDecide = Pinned<CommitDecide>;
using PinnedCommitDecide = s::Select<Pinned<s::Commit<s::Select<s::End, s::Roll>>>, s::Roll>;
using PinnedCommitFollow = s::Offer<s::Commit<s::Offer<s::End, s::Roll>>, s::Roll>;
using PinnedAbortDecide = s::Select<s::Commit<s::Select<s::End, Pinned<s::Abort>>>, s::End>;
using PinnedAbortFollow = s::Offer<s::Commit<s::Offer<s::End, s::Abort>>, s::End>;
using PinnedEndDecide = s::Select<s::Commit<s::End>, Pinned<s::End>>;
using PinnedEndFollow = s::Offer<s::Commit<s::End>, s::End>;
using PinnedTailDecide = s::Select<s::Commit<Pinned<s::End>>, s::Roll>;

static_assert(s::checkpoint_verdict_of(^^PinnedRollDecide, ^^CommitFollow) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^PinnedRollFollow, ^^CommitDecide) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^TwoPinsDecide, ^^CommitFollow) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^PinnedStartDecide, ^^CommitFollow) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^PinnedCommitDecide, ^^PinnedCommitFollow) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^PinnedAbortDecide, ^^PinnedAbortFollow) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^PinnedEndDecide, ^^PinnedEndFollow) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^PinnedTailDecide, ^^CommitFollow) == s::CheckpointVerdict::Compliant);

// A pin around a Loop: at the start of each side, and as a branch.  The handle
// removes the pin first and then enters the Loop.
using PinnedLoopDecide = Pinned<LoopDecide>;
using PinnedLoopFollow = Pinned<LoopFollow>;
using PinnedLoopBranchDecide = s::Select<s::Commit<s::Send<int, s::End>>, PinnedLoopDecide>;
using PinnedLoopBranchFollow = s::Offer<s::Commit<s::Recv<int, s::End>>, LoopFollow>;

static_assert(s::checkpoint_verdict_of(^^PinnedLoopDecide, ^^LoopFollow) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^PinnedLoopFollow, ^^LoopDecide) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^PinnedLoopBranchDecide, ^^PinnedLoopBranchFollow)
              == s::CheckpointVerdict::Compliant);

// ── The context gate ────────────────────────────────────────────────
//
// The context of a checkpoint session admits the effect row of each
// payload, as the context of mint_session does.
using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using DecideIo = s::Select<s::Commit<s::Send<IoPayload, s::End>>, s::Roll>;
using FollowIo = s::Offer<s::Commit<s::Recv<IoPayload, s::End>>, s::Roll>;
struct IoWire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
static_assert(s::CheckpointSessionAdmissible<DecideIo, FollowIo>);
static_assert(!s::CtxFitsCheckpointSession<BgCtx, DecideIo, FollowIo, IoWire>, "the background context holds no IO");
static_assert(s::CtxFitsCheckpointSession<BgIoCtx, DecideIo, FollowIo, IoWire>);
static_assert(!s::CtxFitsCheckpointSession<int, Decide, Follow, IoWire>, "an int is not an execution context");

// ── The cost of a verdict ───────────────────────────────────────────
//
// The gate asks the verdict of one pair in each of its clauses.  Here one
// side loops over a Select of eight branches, and each branch sends forty
// words of its own type.  The pair has 321 configurations.  One walk of it
// costs about 13 % of the operation limit of a constant evaluation, so
// sixteen walks pass the limit two times over.  So one evaluation that asks
// the verdict sixteen times compiles only when each ask after the first reads
// the cache.

inline constexpr int kWordsInABranch = 40;

template <int Index>
struct BudgetWord {
    int value = 0;
};

template <int Branch, int Step>
struct budget_sends {
    using type = s::Send<BudgetWord<(Branch * kWordsInABranch) + Step>, typename budget_sends<Branch, Step + 1>::type>;
};
template <int Branch>
struct budget_sends<Branch, kWordsInABranch> {
    using type = s::Continue;
};

template <int Branch, int Step>
struct budget_receives {
    using type =
        s::Recv<BudgetWord<(Branch * kWordsInABranch) + Step>, typename budget_receives<Branch, Step + 1>::type>;
};
template <int Branch>
struct budget_receives<Branch, kWordsInABranch> {
    using type = s::Continue;
};

template <int... Branch>
struct budget_pair {
    using sender = s::Loop<s::Select<typename budget_sends<Branch, 0>::type...>>;
    using receiver = s::Loop<s::Offer<typename budget_receives<Branch, 0>::type...>>;
};
using BudgetPair = budget_pair<0, 1, 2, 3, 4, 5, 6, 7>;
using BudgetSender = BudgetPair::sender;
using BudgetReceiver = BudgetPair::receiver;

[[nodiscard]] consteval bool asks_the_verdict_sixteen_times() {
    for (int ask = 0; ask < 16; ++ask) {
        if (s::checkpoint_verdict_of(^^BudgetSender, ^^BudgetReceiver) != s::CheckpointVerdict::Compliant) {
            return false;
        }
    }
    return true;
}
static_assert(asks_the_verdict_sixteen_times(), "each ask after the first reads the cache of the verdict");

// ── The runtime ─────────────────────────────────────────────────────

struct Mailbox {
    std::deque<std::uint64_t> slots;
};

struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

// The two ends run on one thread, and each send comes before its receive.
// A write tries, and the queue has no bound, so each try takes the value.
// A read polls, so the handle never waits while the queue holds a slot.
constexpr auto push_label = [](Port& port, std::size_t label) noexcept {
    port.out->slots.push_back(label);
    return true;
};
// A Text travels as its first character, which is enough to tell the
// three texts of the example apart.
constexpr auto push_text = [](Port& port, Text& text) noexcept {
    port.out->slots.push_back(static_cast<std::uint64_t>(static_cast<unsigned char>(text.view().front())));
    return true;
};
constexpr auto push_int = [](Port& port, int& value) noexcept {
    port.out->slots.push_back(static_cast<std::uint64_t>(value));
    return true;
};
constexpr auto pop_slot = [](Port& port) noexcept -> ::fixy::Option<std::uint64_t> {
    if (port.in->slots.empty()) return ::fixy::none;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return ::fixy::some(slot);
};
constexpr auto pop_label = [](Port& port) noexcept -> ::fixy::Option<std::size_t> {
    return pop_slot(port).match(
        [](std::uint64_t slot) noexcept { return ::fixy::some(static_cast<std::size_t>(slot)); },
        []() noexcept { return ::fixy::Option<std::size_t>{}; });
};
constexpr auto pop_int = [](Port& port) noexcept -> ::fixy::Option<int> {
    return pop_slot(port).match([](std::uint64_t slot) noexcept { return ::fixy::some(static_cast<int>(slot)); },
                                []() noexcept { return ::fixy::Option<int>{}; });
};
// A Text holds a pointer, so the rvalue match does not give it.  The read
// takes the slot out with expect after the test.
constexpr auto pop_text = [](Port& port) noexcept -> ::fixy::Option<Text> {
    ::fixy::Option<std::uint64_t> slot = pop_slot(port);
    if (slot.is_none()) return ::fixy::none;
    const std::uint64_t first = std::move(slot).expect("the test found a slot");
    if (first == 'm') return ::fixy::some(Text{"meta"});
    if (first == 'c') return ::fixy::some(Text{"clip"});
    return ::fixy::some(Text{"film"});
};

// A trace of what happened, in order.
struct Trace {
    int rolls = 0;
    int aborts = 0;
    int videos = 0;
    int commits = 0;
};

[[noreturn]] void unreachable_branch(const char* where) {
    std::fprintf(stderr, "test_session_rollback: took an unexpected branch at %s\n", where);
    std::abort();
}

template <typename UserHandle, typename ServiceHandle>
int run_session(UserHandle user, ServiceHandle service, int round, Trace& trace);

// From the metadata, which is where U's checkpoint is.
template <typename UserHandle, typename ServiceHandle>
int run_from_metadata(UserHandle user, ServiceHandle service, int round, Trace& trace) {
    auto service_meta = std::move(service).send(Text{"meta"}, push_text);
    auto [meta, user_meta] = std::move(user).recv(pop_text);
    if (meta.view() != "meta") return 1;

    if (round == 0) {
        // HD, then roll.
        auto user_hd = std::move(user_meta).template select<0>(push_label);
        return std::move(service_meta).branch(pop_label, [&](auto service_choice) -> int {
            using Head = typename decltype(service_choice)::protocol;
            if constexpr (std::is_same_v<Head, s::Send<Text, s::Offer<s::Send<Text, s::End>, s::Roll>>>) {
                auto service_test = std::move(service_choice).send(Text{"clip"}, push_text);
                auto [clip, user_test] = std::move(user_hd).recv(pop_text);
                (void)clip;
                auto user_rolled = std::move(user_test).template select<1>(push_label);
                static_assert(std::is_same_v<decltype(user_rolled), UserHandle>,
                              "a roll returns the user to the handle it had at its checkpoint");
                return std::move(service_test).branch(pop_label, [&](auto service_after) -> int {
                    if constexpr (std::is_same_v<decltype(service_after), ServiceHandle>) {
                        ++trace.rolls;
                        return run_from_metadata(std::move(user_rolled), std::move(service_after), 1, trace);
                    } else {
                        unreachable_branch("the service after the test clip in round 0");
                    }
                });
            } else {
                unreachable_branch("the service quality choice in round 0");
            }
        });
    }

    // SD.  In round 1 abort, in round 2 watch.
    auto user_sd = std::move(user_meta).template select<1>(push_label);
    return std::move(service_meta).branch(pop_label, [&](auto service_choice) -> int {
        using Head = typename decltype(service_choice)::protocol;
        if constexpr (std::is_same_v<Head, s::Send<Text, s::Offer<s::Send<Text, s::End>, s::Abort>>>) {
            auto service_test = std::move(service_choice).send(Text{"clip"}, push_text);
            auto [clip, user_test] = std::move(user_sd).recv(pop_text);
            (void)clip;
            if (round == 1) {
                auto user_restarted = std::move(user_test).template select<1>(push_label);
                return std::move(service_test).branch(pop_label, [&](auto service_after) -> int {
                    using After = typename decltype(service_after)::protocol;
                    if constexpr (std::is_same_v<After, ServiceC>) {
                        ++trace.aborts;
                        return run_session(std::move(user_restarted), std::move(service_after), 2, trace);
                    } else {
                        unreachable_branch("the service after the SD clip in round 1");
                    }
                });
            }
            auto user_watch = std::move(user_test).template select<0>(push_label);
            return std::move(service_test).branch(pop_label, [&](auto service_after) -> int {
                using After = typename decltype(service_after)::protocol;
                if constexpr (std::is_same_v<After, s::Send<Text, s::End>>) {
                    auto service_end = std::move(service_after).send(Text{"clip"}, push_text);
                    auto [video, user_end] = std::move(user_watch).recv(pop_text);
                    (void)video;
                    ++trace.videos;
                    (void)std::move(service_end).close();
                    (void)std::move(user_end).close();
                    return 0;
                } else {
                    unreachable_branch("the service after the SD clip in round 2");
                }
            });
        } else {
            unreachable_branch("the service quality choice in an SD round");
        }
    });
}

template <typename UserHandle, typename ServiceHandle>
int run_session(UserHandle user, ServiceHandle service, int round, Trace& trace) {
    auto user_asked = std::move(user).send(Text{"film"}, push_text);
    auto [request, service_asked] = std::move(service).recv(pop_text);
    (void)request;
    auto service_priced = std::move(service_asked).send(7, push_int);
    auto [price, user_priced] = std::move(user_asked).recv(pop_int);
    if (price != 7) return 1;

    // S commits, then U commits.  Each handler has one branch, so it can
    // return the handle.
    auto service_committed = std::move(service_priced).template select<0>(push_label);
    auto user_saw_commit = std::move(user_priced).branch(pop_label, [](auto next) { return next; });
    auto user_committed = std::move(user_saw_commit).template select<0>(push_label);
    auto service_saw_commit = std::move(service_committed).branch(pop_label, [](auto next) { return next; });
    trace.commits += 2;
    return run_from_metadata(std::move(user_committed), std::move(service_saw_commit), round, trace);
}

int run_video_on_demand() {
    Mailbox to_user;
    Mailbox to_service;
    auto user = s::mint_checkpoint_session<UserC, ServiceC>(bg_ctx(), Port{&to_user, &to_service});
    auto service = s::mint_checkpoint_session<ServiceC, UserC>(bg_ctx(), Port{&to_service, &to_user});
    static_assert(std::is_same_v<decltype(user)::protocol, UserC>);

    Trace trace;
    if (const int rc = run_session(std::move(user), std::move(service), 0, trace); rc != 0) return rc;
    if (trace.rolls != 1 || trace.aborts != 1 || trace.videos != 1 || trace.commits != 4) {
        std::fprintf(stderr, "test_session_rollback: rolls=%d aborts=%d videos=%d commits=%d\n", trace.rolls,
                     trace.aborts, trace.videos, trace.commits);
        return 1;
    }
    if (!to_user.slots.empty() || !to_service.slots.empty()) {
        std::fprintf(stderr, "test_session_rollback: a message was left on the wire\n");
        return 1;
    }
    return 0;
}

// A loop whose checkpoint is inside it: commit, send, roll back to the
// send, send again, end.  A vendor pin around the loop of one side changes
// none of the steps.
template <typename DecideProto, typename FollowProto>
int run_loop_rollback() {
    Mailbox to_left;
    Mailbox to_right;
    const std::uint32_t baseline = s::watch::live_count();
    auto left = s::mint_checkpoint_session<DecideProto, FollowProto>(bg_ctx(), Port{&to_left, &to_right});
    auto right = s::mint_checkpoint_session<FollowProto, DecideProto>(bg_ctx(), Port{&to_right, &to_left});

    auto left_saved = std::move(left).template select<0>(push_label);
    int rc = 1;
    std::move(right).branch(pop_label, [&](auto right_saved) {
        if constexpr (std::is_same_v<typename decltype(right_saved)::protocol, s::Recv<int, s::Continue>>) {
            auto left_sent = std::move(left_saved).send(1, push_int);
            auto [first, right_got] = std::move(right_saved).recv(pop_int);
            auto left_back = std::move(left_sent).template select<1>(push_label);
            static_assert(std::is_same_v<decltype(left_back), decltype(left_saved)>,
                          "a roll inside a loop returns to the handle at the checkpoint");
            std::move(right_got).branch(pop_label, [&](auto right_next) {
                if constexpr (std::is_same_v<decltype(right_next), decltype(right_saved)>) {
                    // Each side built its handle again at the checkpoint.  A
                    // handle that the roll built holds a record in the watch,
                    // as a minted handle does.
                    const std::uint32_t records = s::watch::live_count() - baseline;
                    if (records != 2) {
                        std::fprintf(stderr,
                                     "test_session_rollback: after the roll the watch holds %u records, and each "
                                     "of the two rebuilt handles must hold one\n",
                                     records);
                    }
                    auto left_again = std::move(left_back).send(2, push_int);
                    auto [second, right_again] = std::move(right_next).recv(pop_int);
                    auto left_done = std::move(left_again).template select<2>(push_label);
                    std::move(right_again).branch(pop_label, [&](auto right_end) {
                        if constexpr (std::is_same_v<typename decltype(right_end)::protocol, s::End>) {
                            (void)std::move(right_end).close();
                            (void)std::move(left_done).close();
                            rc = (first == 1 && second == 2 && records == 2) ? 0 : 1;
                        } else {
                            unreachable_branch("the last label of the loop");
                        }
                    });
                } else {
                    unreachable_branch("the label after the roll");
                }
            });
        } else {
            unreachable_branch("the first label of the loop");
        }
    });
    return rc;
}

// ── The pinned primitives at run time ───────────────────────────────

// No head of a checkpoint handle holds a vendor pin, because the check reads
// each position under its pins.
template <typename Handle>
inline constexpr bool is_unpinned_head = !s::is_vendor_pinned_v<typename Handle::protocol>;

// Label L reaches End on the two ends, and the two ends close.
template <std::size_t Label, typename Left, typename Right>
void finish_at_end(Left left, Right right) {
    auto left_end = std::move(left).template select<Label>(push_label);
    static_assert(is_unpinned_head<decltype(left_end)>, "no head of a checkpoint handle holds a vendor pin");
    bool is_closed = false;
    std::move(right).branch(pop_label, [&](auto right_end) {
        static_assert(is_unpinned_head<decltype(right_end)>, "no head of a checkpoint handle holds a vendor pin");
        if constexpr (std::is_same_v<typename decltype(right_end)::protocol, s::End>) {
            (void)std::move(right_end).close();
            is_closed = true;
        } else {
            ::fixy::fatal("test_session_rollback: the follower took a branch that is not End");
        }
    });
    (void)std::move(left_end).close();
    if (!is_closed) ::fixy::fatal("test_session_rollback: the follower did not close");
}

// The decider rolls back at the first label, and each end stands at its
// start again.  Then the pair commits at label 0 and ends.
template <typename DecideProto, typename FollowProto>
void run_roll_at_start() {
    Mailbox to_left;
    Mailbox to_right;
    auto left = s::mint_checkpoint_session<DecideProto, FollowProto>(bg_ctx(), Port{&to_left, &to_right});
    auto right = s::mint_checkpoint_session<FollowProto, DecideProto>(bg_ctx(), Port{&to_right, &to_left});
    static_assert(is_unpinned_head<decltype(left)>, "no head of a checkpoint handle holds a vendor pin");
    using LeftStart = decltype(left);
    using RightStart = decltype(right);
    auto left_back = std::move(left).template select<1>(push_label);
    static_assert(std::is_same_v<decltype(left_back), LeftStart>, "a pinned Roll returns the decider to its start");
    std::move(right).branch(pop_label, [&](auto right_back) {
        static_assert(is_unpinned_head<decltype(right_back)>, "no head of a checkpoint handle holds a vendor pin");
        if constexpr (std::is_same_v<decltype(right_back), RightStart>) {
            finish_at_end<0>(std::move(left_back), std::move(right_back));
        } else {
            ::fixy::fatal("test_session_rollback: the follower did not roll back to its start");
        }
    });
}

// The decider commits through a pinned Commit and then rolls back.  Each end
// stands at the handle that the Commit gave it.  Then the pair ends.
void run_roll_after_pinned_commit() {
    Mailbox to_left;
    Mailbox to_right;
    auto left = s::mint_checkpoint_session<PinnedCommitDecide, PinnedCommitFollow>(bg_ctx(), Port{&to_left, &to_right});
    auto right =
        s::mint_checkpoint_session<PinnedCommitFollow, PinnedCommitDecide>(bg_ctx(), Port{&to_right, &to_left});
    using RightStart = decltype(right);
    auto left_saved = std::move(left).select<0>(push_label);
    static_assert(is_unpinned_head<decltype(left_saved)>, "no head of a checkpoint handle holds a vendor pin");
    std::move(right).branch(pop_label, [&](auto right_saved) {
        if constexpr (!std::is_same_v<decltype(right_saved), RightStart>) {
            auto left_back = std::move(left_saved).template select<1>(push_label);
            static_assert(std::is_same_v<decltype(left_back), decltype(left_saved)>,
                          "a Roll after a pinned Commit returns the decider to the checkpoint of the Commit");
            std::move(right_saved).branch(pop_label, [&](auto right_back) {
                if constexpr (std::is_same_v<decltype(right_back), decltype(right_saved)>) {
                    finish_at_end<0>(std::move(left_back), std::move(right_back));
                } else {
                    ::fixy::fatal("test_session_rollback: the follower did not roll back to the checkpoint");
                }
            });
        } else {
            ::fixy::fatal("test_session_rollback: the follower did not commit");
        }
    });
}

// The decider commits and then takes a pinned Abort.  Each end stands at its
// start again, not at the checkpoint of the Commit.  Then the pair ends.
void run_pinned_abort() {
    Mailbox to_left;
    Mailbox to_right;
    auto left = s::mint_checkpoint_session<PinnedAbortDecide, PinnedAbortFollow>(bg_ctx(), Port{&to_left, &to_right});
    auto right = s::mint_checkpoint_session<PinnedAbortFollow, PinnedAbortDecide>(bg_ctx(), Port{&to_right, &to_left});
    using LeftStart = decltype(left);
    using RightStart = decltype(right);
    auto left_saved = std::move(left).select<0>(push_label);
    std::move(right).branch(pop_label, [&](auto right_saved) {
        if constexpr (s::is_offer_v<typename decltype(right_saved)::protocol>) {
            auto left_back = std::move(left_saved).template select<1>(push_label);
            static_assert(std::is_same_v<decltype(left_back), LeftStart>,
                          "a pinned Abort returns the decider to its start");
            std::move(right_saved).branch(pop_label, [&](auto right_back) {
                if constexpr (std::is_same_v<decltype(right_back), RightStart>) {
                    finish_at_end<1>(std::move(left_back), std::move(right_back));
                } else {
                    ::fixy::fatal("test_session_rollback: the follower did not start again");
                }
            });
        } else {
            ::fixy::fatal("test_session_rollback: the follower did not commit");
        }
    });
}

// A pinned End is an End that closes: as a branch, and as the continuation of
// a Commit.
void run_pinned_end() {
    Mailbox to_left;
    Mailbox to_right;
    finish_at_end<1>(s::mint_checkpoint_session<PinnedEndDecide, PinnedEndFollow>(bg_ctx(), Port{&to_left, &to_right}),
                     s::mint_checkpoint_session<PinnedEndFollow, PinnedEndDecide>(bg_ctx(), Port{&to_right, &to_left}));
    finish_at_end<0>(s::mint_checkpoint_session<PinnedTailDecide, CommitFollow>(bg_ctx(), Port{&to_left, &to_right}),
                     s::mint_checkpoint_session<CommitFollow, PinnedTailDecide>(bg_ctx(), Port{&to_right, &to_left}));
}

// The decider takes the branch that is a pinned Loop, and each end stands at
// the head of the body of its loop.  Then the pair ends inside the loop.
void run_pinned_loop_branch() {
    Mailbox to_left;
    Mailbox to_right;
    auto left =
        s::mint_checkpoint_session<PinnedLoopBranchDecide, PinnedLoopBranchFollow>(bg_ctx(), Port{&to_left, &to_right});
    auto right =
        s::mint_checkpoint_session<PinnedLoopBranchFollow, PinnedLoopBranchDecide>(bg_ctx(), Port{&to_right, &to_left});
    auto left_loop = std::move(left).select<1>(push_label);
    static_assert(std::is_same_v<typename decltype(left_loop)::protocol,
                                 s::Select<s::Commit<s::Send<int, s::Continue>>, s::Roll, s::End>>,
                  "the pinned Loop branch enters the body of the loop");
    std::move(right).branch(pop_label, [&](auto right_loop) {
        if constexpr (s::is_offer_v<typename decltype(right_loop)::protocol>) {
            finish_at_end<2>(std::move(left_loop), std::move(right_loop));
        } else {
            ::fixy::fatal("test_session_rollback: the follower did not enter its loop");
        }
    });
}

}  // namespace

int main() {
    if (const int rc = run_video_on_demand(); rc != 0) return rc;
    if (const int rc = run_loop_rollback<LoopDecide, LoopFollow>(); rc != 0) return rc;
    if (const int rc = run_loop_rollback<PinnedLoopDecide, LoopFollow>(); rc != 0) return rc;
    if (const int rc = run_loop_rollback<LoopDecide, PinnedLoopFollow>(); rc != 0) return rc;
    run_roll_at_start<PinnedRollDecide, CommitFollow>();
    run_roll_at_start<CommitDecide, PinnedRollFollow>();
    run_roll_at_start<TwoPinsDecide, CommitFollow>();
    run_roll_at_start<PinnedStartDecide, CommitFollow>();
    run_roll_after_pinned_commit();
    run_pinned_abort();
    run_pinned_end();
    run_pinned_loop_branch();
    return 0;
}
