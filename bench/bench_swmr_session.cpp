// The SWMR channel against its seqlock.
//
// fixy/channels/SwmrSession.h wraps fixy/concurrent/AtomicSnapshot.h.  A
// writer handle holds the linear writer permission and publishes, a reader
// handle holds one share of the reader pool and loads, and each side can
// run a session over its own handle.  Three layers on each side: the bare
// snapshot, the handle, and the session.
//
// The reader session reads through a polling read over try_load, so its
// baseline is the bare try_load, and the handle's load is compared with the
// bare load.  A handle cannot be reassigned, so each session arm keeps its
// session in a Seat of foundation/core/Seat.h, which the harness holds for
// the row (measure_with).  Each step runs on the session in the storage of
// the Seat and puts the next session there.
//
// Each handle lives in the body of a frame door (with_writer and with_reader
// of fixy/channels/SwmrSession.h).  The frame opens before the timed row, and
// the row runs in its body.  A report owns its samples, so it is no result of
// a frame: the body moves the report into a local of the row.

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>
#include <fixy/concurrent/AtomicSnapshot.h>

#include <foundation/Linear.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/core/Seat.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

#include <cstdint>
#include <utility>

namespace {

namespace cc = ::fixy::concurrent;
namespace perm = ::foundation::permissions;
namespace s = ::fixy::session;
namespace ses = ::fixy::channels::swmr_session;

struct Payload {
    std::uint64_t seq = 0;
    std::uint64_t checksum = ~std::uint64_t{0};
};

[[nodiscard]] constexpr Payload payload_at(std::uint64_t seq) noexcept { return Payload{.seq = seq, .checksum = ~seq}; }

struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};

// One call site mints every reader root, so every channel of the type
// carries the brand of that one site.
[[nodiscard]] auto reader_root() noexcept { return perm::mint_permission_root<ReaderTag>(); }
[[nodiscard]] auto writer_root() noexcept { return perm::mint_permission_root<WriterTag>(); }

using Swmr = ses::SwmrSession<Payload, WriterTag, ReaderTag, ::foundation::brand::brand_of_t<decltype(reader_root())>,
                              ::foundation::brand::brand_of_t<decltype(writer_root())>>;

// The one writer of the bare snapshot of the rows: the owner of the proof of
// its writer.  Each row publishes from one thread.
class RawWriter;
struct PublishRole final {};
using RawSnapshot = cc::AtomicSnapshot<Payload, ::foundation::SoleWriterProof<RawWriter, PublishRole>>;

class RawWriter final {
public:
    // Publishes value into snapshot as its one writer.
    static void publish(RawSnapshot& snapshot, Payload const& value) noexcept {
        snapshot.publish(::foundation::SoleWriterProof<RawWriter, PublishRole>{}, value);
    }
};

constexpr auto kForeground = ::foundation::effects::testing::foreground();

// The context of each reader: the lend of a reader share and each read take
// Nondet, and the foreground context holds no atom.
constexpr ::fixy::TestRunnerCtx kReader{::foundation::effects::testing::test()};

// The Run of each row: its counts of samples and of warmup calls, and its cap.
[[nodiscard]] bench::Run run_of(::fixy::TextView name) {
    bench::Run run{name};
    static_cast<void>(run.samples(50000).warmup(5000).max_wall_ms(3000));
    return run;
}

template <typename Body>
[[nodiscard]] bench::Report measure(::fixy::TextView name, Body&& body) {
    return run_of(name).measure(static_cast<Body&&>(body));
}

[[nodiscard]] bench::Report raw_publish() {
    RawSnapshot snapshot{payload_at(0)};
    std::uint64_t seq = 0;
    return measure("raw AtomicSnapshot.publish", [&] {
        RawWriter::publish(snapshot, payload_at(++seq));
        bench::mark_changed(seq);
    });
}

[[nodiscard]] bench::Report handle_publish() {
    Swmr swmr{reader_root(), payload_at(0)};
    std::uint64_t seq = 0;
    bench::Report report{};
    // The row reaches the linear writer handle through a Ref, and the body
    // ends the writer token after the row.
    ses::with_writer(swmr, writer_root(), [&seq, &report](auto writer) noexcept {
        report = measure("SwmrSession.WriterHandle.publish", [&seq, publisher = ::fixy::to(writer)] {
            publisher->publish(payload_at(++seq));
            bench::mark_changed(seq);
        });
        ::foundation::linear::drop(std::move(writer).release());
    });
    return report;
}

[[nodiscard]] bench::Report session_send() {
    Swmr swmr{reader_root(), payload_at(0)};
    std::uint64_t seq = 0;
    // The step takes a generic lambda, so the brand of the session stays in the
    // template parameter of the lambda.  The session takes the writer handle of
    // the frame.
    bench::Report report{};
    ses::with_writer(swmr, writer_root(), [&seq, &report](auto writer) noexcept {
        auto [measured, session] = bench::measure_with(
            run_of("SwmrSession session send"),
            ::foundation::core::mint_seat(ses::mint_writer_runtime_session<Swmr>(kForeground, std::move(writer))),
            [&seq](auto& seat) noexcept {
                Payload const next = payload_at(++seq);
                seat.step([&next](auto&& held) noexcept { return std::move(held).send(kForeground, next); });
                bench::mark_changed(seq);
            });
        std::move(session).take().detach(s::detach_reason::InfiniteLoopProtocol{});
        report = std::move(measured);
    });
    return report;
}

[[nodiscard]] bench::Report raw_load() {
    const RawSnapshot snapshot{payload_at(123)};
    const ::fixy::TestRunnerCtx reader{::foundation::effects::testing::test()};
    return measure("raw AtomicSnapshot.load", [&] {
        const Payload observed = snapshot.load(reader);
        bench::do_not_optimize(observed);
    });
}

[[nodiscard]] bench::Report handle_load() {
    Swmr swmr{reader_root(), payload_at(123)};
    const ::fixy::TestRunnerCtx spin{::foundation::effects::testing::test()};
    bench::Report report{};
    // The channel has a free share by construction, so the reader door runs
    // its body.
    auto const read = ses::with_reader(kReader, swmr, [&spin, &report](auto reader) noexcept {
        report = measure("SwmrSession.ReaderHandle.load", [&] {
            const Payload observed = reader.load(spin);
            bench::do_not_optimize(observed);
        });
    });
    CRUCIBLE_BENCH_CHECK(read.is_some());
    return report;
}

[[nodiscard]] bench::Report raw_try_load() {
    const RawSnapshot snapshot{payload_at(123)};
    return measure("raw AtomicSnapshot.try_load", [&] {
        const ::fixy::Option<Payload> observed = snapshot.try_load();
        bench::do_not_optimize(observed);
    });
}

[[nodiscard]] bench::Report session_recv() {
    Swmr swmr{reader_root(), payload_at(123)};
    bench::Report report{};
    // The session takes the reader handle of the frame.  The channel has a
    // free share by construction, so the reader door runs its body.
    auto const read = ses::with_reader(kReader, swmr, [&report](auto reader) noexcept {
        auto [measured, session] = bench::measure_with(
            run_of("SwmrSession session recv"),
            ::foundation::core::mint_seat(ses::mint_reader_runtime_session<Swmr>(kReader, std::move(reader))),
            [](auto& seat) noexcept {
                seat.step([](auto&& held) noexcept {
                    auto [observed, next] = std::move(held).recv(kReader);
                    bench::do_not_optimize(observed);
                    return std::move(next);
                });
            });
        std::move(session).take().detach(s::detach_reason::InfiniteLoopProtocol{});
        report = std::move(measured);
    });
    CRUCIBLE_BENCH_CHECK(read.is_some());
    return report;
}

}  // namespace

int main() {
    ::fixy::FixedArray<bench::Report, 7> reports{
        raw_publish(), handle_publish(), session_send(), raw_load(), handle_load(), raw_try_load(), session_recv(),
    };
    bench::emit_reports_text(reports.view());

    bench::print("\n=== comparisons ===\n");
    ::fixy::FixedArray<bench::Compare, 4> const compares{
        bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>())),  // the writer handle
        bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<2>())),  // the writer session
        bench::compare(reports.at(::fixy::index<3>()), reports.at(::fixy::index<4>())),  // the reader handle
        bench::compare(reports.at(::fixy::index<5>()), reports.at(::fixy::index<6>())),  // the reader session
    };
    bench::emit_compares(compares.view());
    bench::emit_reports_json(reports.view(), bench::env_json());
    return 0;
}
