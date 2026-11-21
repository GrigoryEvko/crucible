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

#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>
#include <fixy/concurrent/AtomicSnapshot.h>

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

constexpr auto kForeground = ::foundation::effects::testing::foreground();

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

// A reader of the channel, which has a free share by construction.
[[nodiscard]] Swmr::ReaderHandle take_reader(Swmr& swmr) noexcept {
    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_BENCH_CHECK(reader.is_some());
    return std::move(reader).expect("the pool lends a share");
}

[[nodiscard]] bench::Report raw_publish() {
    cc::AtomicSnapshot<Payload> snapshot{payload_at(0)};
    std::uint64_t seq = 0;
    return measure("raw AtomicSnapshot.publish", [&] {
        snapshot.publish(payload_at(++seq));
        bench::mark_changed(seq);
    });
}

[[nodiscard]] bench::Report handle_publish() {
    Swmr swmr{reader_root(), payload_at(0)};
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, writer_root());
    std::uint64_t seq = 0;
    return measure("SwmrSession.WriterHandle.publish", [&] {
        writer.publish(payload_at(++seq));
        bench::mark_changed(seq);
    });
}

[[nodiscard]] bench::Report session_send() {
    Swmr swmr{reader_root(), payload_at(0)};
    std::uint64_t seq = 0;
    // The step takes a generic lambda, so the brand of the session stays in the
    // template parameter of the lambda.
    auto [report, session] = bench::measure_with(
        run_of("SwmrSession session send"),
        ::foundation::core::mint_seat(
            ses::mint_writer_runtime_session<Swmr>(kForeground, ses::mint_swmr_writer<Swmr>(swmr, writer_root()))),
        [&seq](auto& seat) noexcept {
            Payload const next = payload_at(++seq);
            seat.step([&next](auto&& held) noexcept { return std::move(held).send(kForeground, next); });
            bench::mark_changed(seq);
        });
    std::move(session).take().detach(s::detach_reason::InfiniteLoopProtocol{});
    return std::move(report);
}

[[nodiscard]] bench::Report raw_load() {
    const cc::AtomicSnapshot<Payload> snapshot{payload_at(123)};
    const ::fixy::TestRunnerCtx reader{::foundation::effects::testing::test()};
    return measure("raw AtomicSnapshot.load", [&] {
        const Payload observed = snapshot.load(reader);
        bench::do_not_optimize(observed);
    });
}

[[nodiscard]] bench::Report handle_load() {
    Swmr swmr{reader_root(), payload_at(123)};
    const auto reader = take_reader(swmr);
    const ::fixy::TestRunnerCtx spin{::foundation::effects::testing::test()};
    return measure("SwmrSession.ReaderHandle.load", [&] {
        const Payload observed = reader.load(spin);
        bench::do_not_optimize(observed);
    });
}

[[nodiscard]] bench::Report raw_try_load() {
    const cc::AtomicSnapshot<Payload> snapshot{payload_at(123)};
    return measure("raw AtomicSnapshot.try_load", [&] {
        const ::fixy::Option<Payload> observed = snapshot.try_load();
        bench::do_not_optimize(observed);
    });
}

[[nodiscard]] bench::Report session_recv() {
    Swmr swmr{reader_root(), payload_at(123)};
    auto [report, session] = bench::measure_with(
        run_of("SwmrSession session recv"),
        ::foundation::core::mint_seat(ses::mint_reader_runtime_session<Swmr>(kForeground, take_reader(swmr))),
        [](auto& seat) noexcept {
            seat.step([](auto&& held) noexcept {
                auto [observed, next] = std::move(held).recv(kForeground);
                bench::do_not_optimize(observed);
                return std::move(next);
            });
        });
    std::move(session).take().detach(s::detach_reason::InfiniteLoopProtocol{});
    return std::move(report);
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
