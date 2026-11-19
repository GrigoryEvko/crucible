// Microbench of the doors of fixy/os/Socket.h on one thread.
//
// Each row is one call of read_tcp_congestion on one idle TCP socket of the
// socket door: one getsockopt of TCP_CONGESTION.  The first row calls the door
// through the borrow of the Handle, a SocketRef.  The second row calls the
// same door through the shared ref of the same Handle, a ForeignSocketRef,
// which a table of cntp stores.  The ratio of the two medians gives the cost
// of the door of a foreign ref over the door of a borrow: the check of the
// generation of its number (THE GENERATION OF A DESCRIPTOR of fixy/os/Fd.h).
// The third row is that check alone.
//
// Each line gives the core of its row, the percentiles of one call in
// nanoseconds, and the coefficient of variation in thousandths.  A ratio line
// gives the ratio of two medians in thousandths.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

namespace eff = ::foundation::effects;

// A background context that opens a socket and makes its calls.  A socket
// call reads the live state of the kernel, so the row holds Nondet.
using SocketCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO, eff::Effect::Block, eff::Effect::Nondet>>;

using TcpSocket = ::fixy::net::SocketHandle<::fixy::net::socket_kind::TcpStream>;

// The wall time of one row, in milliseconds.
constexpr std::size_t kRowWallMs = 2000;

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// Writes one line: the name of the row, the core of the row, and the
// percentiles of one call.
void print_row(fixy::TextView name, bench::Report const& measured) noexcept {
    fixy::report(fixy::Sink::Out, "{}  core=cpu{}  p50={} ns  p99={} ns  max={} ns  cv={} permille\n", name,
                 measured.pinned_cpu.raw(), nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99),
                 nanoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

// Writes the ratio of the median of a row to the median of its reference.
void print_ratio(fixy::TextView name, bench::Report const& measured, bench::Report const& reference) noexcept {
    double const base = reference.pct.p50 > 0.0 ? reference.pct.p50 : 1.0;
    fixy::report(fixy::Sink::Out, "{}  ratio={} permille\n", name,
                 static_cast<std::uint64_t>(measured.pct.p50 * 1000.0 / base + 0.5));
}

// Measures one row with the pin of CRUCIBLE_BENCH_CORE and the wall cap of
// kRowWallMs.
template <typename Body>
[[nodiscard]] bench::Report measure_row(bench::Run&& run, Body&& body) {
    if (int const core = bench::env_core(); core >= 0) static_cast<void>(run.core(core));
    static_cast<void>(run.max_wall_ms(kRowWallMs));
    return run.measure(static_cast<Body&&>(body));
}

}  // namespace

int main() {
    bench::elevate_priority();
    SocketCtx const ctx{eff::testing::bg()};
    TcpSocket const socket = ::fixy::net::mint_socket<::fixy::net::socket_kind::TcpStream>(ctx).match(
        [](::fixy::Linear<TcpSocket> opened) noexcept { return std::move(opened).consume(); },
        [](::foundation::os::Errno) noexcept { return TcpSocket{}; });
    if (!socket.is_open()) {
        fixy::report(fixy::Sink::Err, "bench_socket_doors: the socket door gave no TCP socket\n");
        return 1;
    }
    auto const borrow = socket.ref();
    auto const shared = ::fixy::net::share_socket(borrow);

    bench::Report const through_borrow = measure_row(bench::Run{"read_tcp_congestion, SocketRef"}, [&] {
        bench::do_not_optimize(::fixy::net::read_tcp_congestion(ctx, borrow).is_ok());
    });
    print_row("read_tcp_congestion through a SocketRef", through_borrow);
    bench::Report const through_shared = measure_row(bench::Run{"read_tcp_congestion, ForeignSocketRef"}, [&] {
        bench::do_not_optimize(::fixy::net::read_tcp_congestion(ctx, shared).is_ok());
    });
    print_row("read_tcp_congestion through a ForeignSocketRef", through_shared);
    print_ratio("ForeignSocketRef over SocketRef", through_shared, through_borrow);
    bench::Report const current = measure_row(bench::Run{"is_current of a ForeignSocketRef"},
                                              [&] { bench::do_not_optimize(shared.is_current()); });
    print_row("is_current of a ForeignSocketRef", current);
    return 0;
}
