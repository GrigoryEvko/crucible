#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/SwmrSession.h>

#include "bench_harness.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

namespace concur = ::crucible::concurrent;
namespace safety = ::crucible::safety;
namespace proto = ::crucible::safety::proto;
namespace ses = ::crucible::safety::proto::swmr_session;

struct RawTag {};
struct WriterTag {};
struct ReaderTag {};

struct Payload {
    std::uint64_t seq = 0;
    std::uint64_t checksum = ~std::uint64_t{0};
};

using RawSnapshot = concur::PermissionedSnapshot<Payload, RawTag>;
using Swmr = ses::SwmrSession<Payload, WriterTag, ReaderTag>;

[[nodiscard]] constexpr Payload payload_at(std::uint64_t seq) noexcept {
    return Payload{.seq = seq, .checksum = ~seq};
}

template <typename Body>
[[nodiscard]] bench::Report measure(char const* name, Body&& body) {
    return bench::Run{name}
        .samples(50'000)
        .warmup(5'000)
        .max_wall_ms(3'000)
        .measure(std::forward<Body>(body));
}

[[nodiscard]] bench::Report raw_publish() {
    RawSnapshot snap{payload_at(0)};
    auto perm = safety::mint_permission_root<RawSnapshot::writer_tag>();
    auto writer = snap.writer(std::move(perm));
    std::uint64_t seq = 0;
    return measure("raw PermissionedSnapshot.publish", [&] {
        writer.publish(payload_at(++seq));
        bench::do_not_optimize(seq);
    });
}

[[nodiscard]] bench::Report swmr_publish() {
    Swmr swmr{payload_at(0)};
    auto perm = safety::mint_permission_root<Swmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, std::move(perm));
    std::uint64_t seq = 0;
    return measure("SwmrSession.WriterHandle.publish", [&] {
        writer.publish(payload_at(++seq));
        bench::do_not_optimize(seq);
    });
}

[[nodiscard]] bench::Report swmr_session_send() {
    using proto::detach_reason::TestInstrumentation;

    Swmr swmr{payload_at(0)};
    auto perm = safety::mint_permission_root<Swmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, std::move(perm));
    auto psh = ses::mint_writer_session<Swmr>(writer);
    std::uint64_t seq = 0;

    auto report = measure("SwmrSession PSH.send(ContentAddressed<T>)", [&] {
        auto next = std::move(psh).send(payload_at(++seq), ses::publish_value);
        psh = std::move(next);
        bench::do_not_optimize(seq);
    });

    std::move(psh).detach(TestInstrumentation{});
    return report;
}

[[nodiscard]] bench::Report raw_load() {
    RawSnapshot snap{payload_at(123)};
    auto reader = snap.reader();
    if (!reader) std::abort();
    return measure("raw PermissionedSnapshot.ReaderHandle.load", [&] {
        const Payload observed = reader->load();
        bench::do_not_optimize(observed);
    });
}

[[nodiscard]] bench::Report swmr_load() {
    Swmr swmr{payload_at(123)};
    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    if (!reader) std::abort();
    return measure("SwmrSession.ReaderHandle.load", [&] {
        const Payload observed = reader->load();
        bench::do_not_optimize(observed);
    });
}

[[nodiscard]] bench::Report swmr_session_recv() {
    using proto::detach_reason::TestInstrumentation;

    Swmr swmr{payload_at(123)};
    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    if (!reader) std::abort();
    auto psh = ses::mint_reader_session<Swmr>(*reader);

    auto report = measure("SwmrSession PSH.recv(Borrowed<T>)", [&] {
        auto [borrowed, next] = std::move(psh).recv(ses::load_borrowed_value);
        psh = std::move(next);
        bench::do_not_optimize(borrowed.value);
    });

    std::move(psh).detach(TestInstrumentation{});
    return report;
}

}  // namespace

int main() {
    std::array reports{
        raw_publish(),
        swmr_publish(),
        swmr_session_send(),
        raw_load(),
        swmr_load(),
        swmr_session_recv(),
    };

    bench::emit_reports_text(reports);

    std::puts("\n=== comparisons ===");
    bench::compare(reports[0], reports[1]).print_text();
    bench::compare(reports[0], reports[2]).print_text();
    bench::compare(reports[3], reports[4]).print_text();
    bench::compare(reports[3], reports[5]).print_text();
    bench::emit_reports_json(reports, bench::env_json());
    return EXIT_SUCCESS;
}
