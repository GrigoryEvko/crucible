// The Vigil mode cell of fixy/session/VigilModeCell.h, with its one writer
// and a reader on a second thread, for many rounds.
//
// In each round the writer writes a plain value and then moves the cell to
// COMPILED.  The reader waits for COMPILED, reads the plain value, and tells
// the writer.  The writer then moves the cell back to RECORDING, and the
// reader tells the writer that it saw RECORDING before the next round
// starts.  So each plain access of one thread happens before each plain
// access of the other thread, through the release store of the cell and the
// acquire load of the reader, or through the release store and the acquire
// load of a reply.  With a relaxed store in the cell, the read of the plain
// value is a data race, and the tsan preset reports it.
//
// The writer moves the cell in three phases: with the publishers of the
// cell, with the edges of a session that holds the lent writer, and with
// the publishers again after the session gives the writer back.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionFork.h>
#include <fixy/session/VigilMode.h>

#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace mode_race_tags {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Writer {
    using permission_row = ::foundation::effects::Row<>;
};
struct Reader {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace mode_race_tags

template <>
struct foundation::permissions::can_split_into_pack<mode_race_tags::Whole, mode_race_tags::Writer,
                                                    mode_race_tags::Reader> : std::true_type {};
template <>
struct foundation::permissions::has_split_pack_authoring_witness<mode_race_tags::Whole, mode_race_tags::Writer,
                                                                 mode_race_tags::Reader> : std::true_type {};

namespace {

namespace s = ::fixy::session;
namespace vm = ::fixy::session::vigil_mode;
namespace eff = ::foundation::effects;

using BgCtx = ::fixy::BgDrainCtx;

// The rounds of each phase of the writer.  Each session takes the edges of
// kRoundsInASession rounds, so the recursion of take_edges below is that
// deep.
inline constexpr std::uint64_t kDirectRounds = 600;
inline constexpr std::uint64_t kSessions = 6;
inline constexpr std::uint64_t kRoundsInASession = 100;
inline constexpr std::uint64_t kRounds = (2 * kDirectRounds) + (kSessions * kRoundsInASession);

// The state that the two threads share.  The plain value is the subject of
// the test, and each reply is an atomic of the base.  The mode cell orders
// each access of the plain value: the writer stores it before the release
// store of a mode, and the reader reads it after the acquire load of that
// mode.  The plain value is private, so only these two doors reach it.  So
// two threads use one Shared at the same time with no race, and the class
// states synchronized (foundation/Transfer.h).
class[[= ::foundation::transfer::synchronized{}]] Shared {
public:
    // The key of the writer of the plain value.  The writer thread makes it,
    // and the key stays on that thread (THE PROOF OF A DOOR of
    // foundation/Transfer.h).
    struct[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] WriterKey final {};

    // The writer calls it before the release store of a mode.
    void write_round(WriterKey& /*key*/, std::uint64_t round) noexcept { written_ = round; }
    // The reader calls it after the acquire load of that mode.
    [[nodiscard]] std::uint64_t read_round() const noexcept { return written_; }

    ::foundation::core::Atomic<std::uint64_t> read_compiled{0};
    ::foundation::core::Atomic<std::uint64_t> read_recording{0};
    ::foundation::core::Atomic<std::uint64_t> wrong_reads{0};

private:
    std::uint64_t written_ = 0;
};
static_assert(::foundation::transfer::Synchronized<Shared>);
// Each write of a cell is atomic, and the identity of its writer is const.
static_assert(::foundation::transfer::Synchronized<vm::ModeCell>);

void wait_until(::foundation::core::Atomic<std::uint64_t> const& reply, std::uint64_t value) noexcept {
    while (reply.load_acquire() != value)
        CRUCIBLE_SPIN_PAUSE;
}

// The transport of each Send step of the mode session.
constexpr auto apply = []<typename Transition>(vm::ModeLease& lease, Transition& transition) noexcept {
    return lease.publish_from_session(transition);
};

// One round of the publishers of the cell.  The round number counts from
// one, so the value zero of a reply names no round.
void publish_one_round(vm::ModeCell& cell, vm::ModeWriter const& writer, Shared& shared, std::uint64_t round) noexcept {
    Shared::WriterKey key{};
    shared.write_round(key, round);
    cell.publish_compiled(writer);
    wait_until(shared.read_compiled, round);
    cell.publish_recording_after_divergence(writer);
    wait_until(shared.read_recording, round);
}

// The edges of the rounds from `round` up to `last`, on a session at its
// choice.  It gives back the lease that the session closes.
template <typename AtChoice>
[[nodiscard]] vm::ModeLease take_edges(AtChoice at_choice, Shared& shared, std::uint64_t round,
                                       std::uint64_t last) noexcept {
    if (round > last) return std::move(at_choice).template select<2>(s::no_label).close();
    Shared::WriterKey key{};
    shared.write_round(key, round);
    auto compiled = std::move(at_choice).template select<0>(s::no_label).send(vm::ModeRecordingToCompiled{}, apply);
    wait_until(shared.read_compiled, round);
    auto recording = std::move(compiled).template select<1>(s::no_label).send(vm::ModeCompiledToRecording{}, apply);
    wait_until(shared.read_recording, round);
    return take_edges(std::move(recording), shared, round + 1, last);
}

// Lends the writer to kSessions sessions, one after the other, and gives it
// back.  The recursion is kSessions deep.
[[nodiscard]] vm::ModeWriter lend_to_sessions(BgCtx const& ctx, vm::ModeCell& cell, vm::ModeWriter writer,
                                              Shared& shared, std::uint64_t session) noexcept {
    if (session == kSessions) return writer;
    const std::uint64_t first = kDirectRounds + (session * kRoundsInASession) + 1;
    vm::ModeLease lease = take_edges(vm::mint_vigil_mode_bridge(ctx, cell, std::move(writer)), shared, first,
                                     first + kRoundsInASession - 1);
    return lend_to_sessions(ctx, cell, std::move(lease).return_writer(), shared, session + 1);
}

void write_each_round(BgCtx const& ctx, vm::ModeCell& cell, vm::ModeWriter writer, Shared& shared) noexcept {
    for (std::uint64_t round = 1; round <= kDirectRounds; ++round)
        publish_one_round(cell, writer, shared, round);
    const vm::ModeWriter returned = lend_to_sessions(ctx, cell, std::move(writer), shared, 0);
    const std::uint64_t after_sessions = kDirectRounds + (kSessions * kRoundsInASession);
    for (std::uint64_t round = after_sessions + 1; round <= kRounds; ++round)
        publish_one_round(cell, returned, shared, round);
}

// Waits for the mode of one round, and counts each read of DIVERGED, which
// no writer of the test stores.
void wait_for_mode(vm::ModeCell const& cell, vm::Mode mode, Shared& shared) noexcept {
    for (;;) {
        const vm::Mode seen = cell.load();
        if (seen == mode) return;
        if (seen == vm::Mode::DIVERGED) static_cast<void>(shared.wrong_reads.fetch_add_acq_rel(1));
        CRUCIBLE_SPIN_PAUSE;
    }
}

void read_each_round(vm::ModeCell const& cell, Shared& shared) noexcept {
    for (std::uint64_t round = 1; round <= kRounds; ++round) {
        wait_for_mode(cell, vm::Mode::COMPILED, shared);
        if (shared.read_round() != round) static_cast<void>(shared.wrong_reads.fetch_add_acq_rel(1));
        shared.read_compiled.store_release(round);
        wait_for_mode(cell, vm::Mode::RECORDING, shared);
        shared.read_recording.store_release(round);
    }
}

[[nodiscard]] int a_reader_follows_the_one_writer() {
    vm::ModeWriter writer = vm::mint_mode_writer();
    vm::ModeCell cell{writer};
    Shared shared{};
    auto whole = ::foundation::permissions::mint_permission_root<mode_race_tags::Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    using ::foundation::permissions::WriteView;
    auto rebuilt = ::fixy::concurrent::mint_permission_fork<mode_race_tags::Writer, mode_race_tags::Reader>(
        BgCtx{eff::testing::bg()}, std::move(whole),
        [&cell, writer = std::move(writer), &shared](WriteView<mode_race_tags::Writer, WholeBrand> const&,
                                                     BgCtx const& ctx) mutable noexcept {
            write_each_round(ctx, cell, std::move(writer), shared);
        },
        [&cell, &shared](WriteView<mode_race_tags::Reader, WholeBrand> const&, BgCtx const&) noexcept {
            read_each_round(cell, shared);
        });
    ::foundation::linear::drop(std::move(rebuilt));
    if (shared.wrong_reads.load_acquire() != 0 || shared.read_recording.load_acquire() != kRounds) {
        ::fixy::report(::fixy::Sink::Err, "the reader saw {} wrong reads in {} of {} rounds\n",
                       shared.wrong_reads.load_acquire(), shared.read_recording.load_acquire(), kRounds);
        return 1;
    }
    if (s::atomic_machine_state(cell) != vm::Mode::RECORDING) {
        ::fixy::report(::fixy::Sink::Err, "the cell did not end in RECORDING\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() { return a_reader_follows_the_one_writer(); }
