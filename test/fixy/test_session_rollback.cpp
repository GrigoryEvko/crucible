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

#include <fixy/session/Checkpoint.h>
#include <fixy/session/Watch.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <cstdio>
#include <deque>
#include <meta>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace s = fixy::session;
namespace eff = ::foundation::effects;

// checkpoint_verdict_v is a value, and a program can specialize it.  The
// gate reads the walk and not this spelling, so the forged verdict below
// changes only what this file reads through it.
namespace forged_verdict {
using Left = s::Select<s::Commit<s::Send<long, s::End>>, s::Roll>;
using Right = s::Offer<s::Roll, s::Commit<s::Recv<long, s::End>>>;
}  // namespace forged_verdict

namespace fixy::session {
template <>
inline constexpr CheckpointVerdict checkpoint_verdict_v<forged_verdict::Left, forged_verdict::Right> =
    CheckpointVerdict::Compliant;
}  // namespace fixy::session

static_assert(s::checkpoint_verdict_v<forged_verdict::Left, forged_verdict::Right> == s::CheckpointVerdict::Compliant,
              "the specialization answers for its author");
static_assert(!s::CheckpointSessionAdmissible<forged_verdict::Left, forged_verdict::Right>,
              "the gate reads the walk, and the walk finds that the labels disagree");

// The boolean verdict is a concept, and a concept has no specialization.
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

static_assert(s::checkpoint_verdict_v<UserB, ServiceB> == s::CheckpointVerdict::RollToImposedCheckpoint);

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

static_assert(s::checkpoint_verdict_v<UserC, ServiceC> == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_v<ServiceC, UserC> == s::CheckpointVerdict::Compliant);
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

static_assert(s::checkpoint_verdict_v<UserD, ServiceD> == s::CheckpointVerdict::RollToImposedCheckpoint);

// ── One side choosing alone ─────────────────────────────────────────

using Decide = s::Select<s::Commit<s::Send<int, s::End>>, s::Roll>;
using Follow = s::Offer<s::Commit<s::Recv<int, s::End>>, s::Roll>;
static_assert(s::checkpoint_verdict_v<Decide, Follow> == s::CheckpointVerdict::Compliant);
static_assert(std::is_same_v<s::dual_of_t<Decide>, s::Offer<s::Commit<s::Recv<int, s::End>>, s::Roll>>);

// The two sides disagree about which label is the rollback.
static_assert(s::checkpoint_verdict_v<Decide, s::Offer<s::Roll, s::Commit<s::Recv<int, s::End>>>>
              == s::CheckpointVerdict::LabelsDisagree);
// Both sides decide, so no label is ever received.
static_assert(s::checkpoint_verdict_v<Decide, Decide> == s::CheckpointVerdict::Stuck);
// A primitive outside a choice is a decision that one side takes alone.
static_assert(s::checkpoint_verdict_v<s::Send<int, s::Roll>, s::Recv<int, s::Roll>>
              == s::CheckpointVerdict::NotCheckpointShaped);
static_assert(s::checkpoint_verdict_v<s::Commit<s::End>, s::Commit<s::End>>
              == s::CheckpointVerdict::NotCheckpointShaped);
// A label the receiver cannot take.
static_assert(s::checkpoint_verdict_v<s::Select<s::End, s::Abort>, s::Offer<s::End>>
              == s::CheckpointVerdict::LabelOutOfRange);
// The plain factory refuses a checkpoint protocol on one endpoint.
static_assert(!s::is_well_formed_v<Decide>);

// A permission cannot cross a checkpoint session, and neither can a
// crash record.
static_assert(s::checkpoint_verdict_v<s::Select<s::Commit<s::Send<s::Transferable<int, Region>, s::End>>, s::Roll>,
                                      s::Offer<s::Commit<s::Recv<s::Transferable<int, Region>, s::End>>, s::Roll>>
              == s::CheckpointVerdict::NotCheckpointShaped);
struct Peer {};
static_assert(s::checkpoint_verdict_v<s::Select<s::Commit<s::Recv<s::Crash<Peer>, s::End>>, s::Roll>,
                                      s::Offer<s::Commit<s::Send<s::Crash<Peer>, s::End>>, s::Roll>>
              == s::CheckpointVerdict::NotCheckpointShaped);

// The passive party of a commit that later rolls is the error of
// Fig. 11, rule TS-Rll2.
static_assert(s::checkpoint_verdict_v<s::Offer<s::Commit<s::Select<s::End, s::Roll>>>,
                                      s::Select<s::Commit<s::Offer<s::End, s::Roll>>>>
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
static_assert(s::checkpoint_verdict_v<LoopDecide, LoopFollow> == s::CheckpointVerdict::Compliant);

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
constexpr auto pop_slot = [](Port& port) noexcept -> std::optional<std::uint64_t> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return slot;
};
constexpr auto pop_label = [](Port& port) noexcept -> std::optional<std::size_t> { return pop_slot(port); };
constexpr auto pop_int = [](Port& port) noexcept {
    return pop_slot(port).transform([](std::uint64_t slot) noexcept { return static_cast<int>(slot); });
};
constexpr auto pop_text = [](Port& port) noexcept {
    return pop_slot(port).transform([](std::uint64_t first) noexcept {
        if (first == 'm') return Text{"meta"};
        if (first == 'c') return Text{"clip"};
        return Text{"film"};
    });
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
// send, send again, end.
int run_loop_rollback() {
    Mailbox to_left;
    Mailbox to_right;
    const std::uint32_t baseline = s::watch::live_count();
    auto left = s::mint_checkpoint_session<LoopDecide, LoopFollow>(bg_ctx(), Port{&to_left, &to_right});
    auto right = s::mint_checkpoint_session<LoopFollow, LoopDecide>(bg_ctx(), Port{&to_right, &to_left});

    auto left_saved = std::move(left).select<0>(push_label);
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

}  // namespace

int main() {
    if (const int rc = run_video_on_demand(); rc != 0) return rc;
    if (const int rc = run_loop_rollback(); rc != 0) return rc;
    return 0;
}
