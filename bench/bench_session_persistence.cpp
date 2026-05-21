// fixy-A2-014: SessionPersistence.h no longer pulls Cipher.h.
#include <crucible/Cipher.h>
#include <crucible/bridges/SessionPersistence.h>

#include "bench_harness.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

// FIXY-V-031: Cipher::open() now takes Path<source::External>.
using CipherRoot = crucible::fixy::wrap::Path<
    crucible::fixy::tags::source::External>;

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;

namespace {

constexpr proto::RoleTagId kA{1};
constexpr proto::RoleTagId kB{2};

struct CounterResource {
    int last = 0;
};

using PersistProto = proto::Loop<
    proto::Select<
        proto::Send<int, proto::Continue>,
        proto::End>>;

static void send_value(CounterResource& r, int value) noexcept {
    r.last = value;
}

template <typename Handle>
[[nodiscard]] CounterResource drive_events(Handle handle, int sends) {
    for (int i = 0; i < sends; ++i) {
        auto send_handle = std::move(handle).template select_local<0>();
        handle = std::move(send_handle).send(i, send_value);
    }
    auto end_handle = std::move(handle).template select_local<1>();
    return std::move(end_handle).close();
}

[[nodiscard]] auto tmp_dir() {
    const auto path = std::filesystem::temp_directory_path() /
        ("crucible_bench_session_persistence_" + std::to_string(::getpid()));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const auto dir = tmp_dir();
    std::printf("=== session persistence ===\n  tmpdir: %s\n\n",
                dir.c_str());

    auto cipher = crucible::Cipher::open(CipherRoot{dir.string()});
    auto view = cipher.mint_open_view();
    eff::TestRunnerCtx ctx{};

    proto::SessionPersistencePolicy no_midrun_flush{
        .count_threshold = 0,
        .time_threshold = std::chrono::steady_clock::duration::zero(),
    };
    proto::SessionPersistencePolicy flush_every_1000{
        .count_threshold = 1000,
        .time_threshold = std::chrono::steady_clock::duration::zero(),
    };

    auto recorded_only = bench::run("RecordingSessionHandle 5000 events", [&] {
        proto::SessionEventLog log{proto::SessionTagId{77}};
        auto bare = proto::mint_session_handle<PersistProto>(CounterResource{});
        auto rec = proto::mint_recording_session(std::move(bare), log, kA, kB);
        auto resource = drive_events(std::move(rec), 2499);
        bench::do_not_optimize(resource.last);
        bench::do_not_optimize(log.size());
    });

    std::uint64_t session_counter = 1000;
    auto persisted_final_flush = bench::run(
        "PersistedSessionHandle 5000 events final flush", [&] {
            auto h = proto::mint_persisted_session<PersistProto>(
                ctx,
                cipher,
                view,
                CounterResource{},
                proto::SessionTagId{++session_counter},
                kA,
                kB,
                no_midrun_flush);
            auto resource = drive_events(std::move(h), 2499);
            bench::do_not_optimize(resource.last);
        });

    auto persisted_batched = bench::run(
        "PersistedSessionHandle 5000 events flush/1000", [&] {
            auto h = proto::mint_persisted_session<PersistProto>(
                ctx,
                cipher,
                view,
                CounterResource{},
                proto::SessionTagId{++session_counter},
                kA,
                kB,
                flush_every_1000);
            auto resource = drive_events(std::move(h), 2499);
            bench::do_not_optimize(resource.last);
        });

    std::vector<bench::Report> reports;
    reports.reserve(3);
    reports.emplace_back(std::move(recorded_only));
    reports.emplace_back(std::move(persisted_final_flush));
    reports.emplace_back(std::move(persisted_batched));

    bench::emit_reports_text(reports);
    bench::compare(reports[0], reports[1]).print_text();
    bench::compare(reports[1], reports[2]).print_text();

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return 0;
}
