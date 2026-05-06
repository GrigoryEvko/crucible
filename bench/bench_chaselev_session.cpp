// ChaseLevDequeSession.h zero-cost validation bench (GAPS-060).
//
// Compares raw PermissionedChaseLevDeque owner/thief handle calls with
// the typed-session PSH facade over the same handles.  The timed bodies
// are self-bounded round trips: each sample pushes one item and removes
// one item, so bench::Run auto-batching cannot fill or drain the deque.
//
// Tier A is structural and load-bearing: the static_asserts in this TU
// and in ChaseLevDequeSession.h prove the pointer-resource PSH has the
// same size as the bare SessionHandle for the same protocol head.  Tier
// B is timed measurement for regression visibility.

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/ChaseLevDequeSession.h>

#include "bench_harness.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

namespace {

namespace concur = ::crucible::concurrent;
namespace safety = ::crucible::safety;
namespace proto = ::crucible::safety::proto;
namespace ses = ::crucible::safety::proto::chaselev_session;

using Item = std::uint64_t;
constexpr std::size_t kCap = 1U << 20;

struct BenchTag {};
using Deque = concur::PermissionedChaseLevDeque<Item, kCap, BenchTag>;

template <typename Body>
[[nodiscard]] bench::Report measure(char const* name, Body&& body) {
    return bench::Run{name}
        .samples(50'000)
        .warmup(5'000)
        .max_wall_ms(3'000)
        .measure(std::forward<Body>(body));
}

[[nodiscard]] Deque::OwnerHandle mint_owner(Deque& deque) noexcept {
    auto perm = safety::mint_permission_root<Deque::owner_tag>();
    return ses::mint_chaselev_owner<Deque>(deque, std::move(perm));
}

[[nodiscard]] bench::Report owner_raw_push_raw_pop() {
    auto deque = std::make_unique<Deque>();
    auto owner = mint_owner(*deque);
    auto* ownerp = &owner;
    Item seq = 0;

    return measure("owner round-trip: raw push + raw pop", [&] {
        (void)ownerp->try_push(++seq);
        const Item value = ownerp->try_pop().value_or(Item{0});
        bench::do_not_optimize(value);
    });
}

[[nodiscard]] bench::Report owner_typed_send_raw_pop() {
    using proto::detach_reason::TestInstrumentation;

    auto deque = std::make_unique<Deque>();
    auto owner = mint_owner(*deque);
    auto* ownerp = &owner;
    auto psh = ses::mint_owner_session<Deque>(owner);
    Item seq = 0;

    auto report = measure("owner round-trip: typed hot push + raw pop", [&] {
        (void)ses::owner_session_try_push<Deque>(psh, ++seq);
        const Item value = ownerp->try_pop().value_or(Item{0});
        bench::do_not_optimize(value);
    });

    std::move(psh).detach(TestInstrumentation{});
    return report;
}

[[nodiscard]] bench::Report owner_raw_push_typed_recv() {
    using proto::detach_reason::TestInstrumentation;

    auto deque = std::make_unique<Deque>();
    auto owner = mint_owner(*deque);
    auto* ownerp = &owner;
    auto psh = ses::mint_owner_session<Deque>(owner);
    Item seq = 0;

    auto report = measure("owner round-trip: raw push + typed hot pop", [&] {
        (void)ownerp->try_push(++seq);
        const Item value =
            ses::owner_session_try_pop<Deque>(psh).value_or(Item{0});
        bench::do_not_optimize(value);
    });

    std::move(psh).detach(TestInstrumentation{});
    return report;
}

[[nodiscard]] bench::Report owner_typed_send_typed_recv() {
    using proto::detach_reason::TestInstrumentation;

    auto deque = std::make_unique<Deque>();
    auto owner = mint_owner(*deque);
    auto psh = ses::mint_owner_session<Deque>(owner);
    Item seq = 0;

    auto report = measure("owner round-trip: typed hot push + typed hot pop",
        [&] {
        (void)ses::owner_session_try_push<Deque>(psh, ++seq);
        const Item value =
            ses::owner_session_try_pop<Deque>(psh).value_or(Item{0});
        bench::do_not_optimize(value);
    });

    std::move(psh).detach(TestInstrumentation{});
    return report;
}

[[nodiscard]] bench::Report steal_raw_push_raw_steal() {
    auto deque = std::make_unique<Deque>();
    auto owner = mint_owner(*deque);
    auto thief = ses::mint_chaselev_thief<Deque>(*deque);
    if (!thief) std::abort();
    auto* ownerp = &owner;
    auto* thiefp = &*thief;
    Item seq = 0;

    return measure("steal round-trip: raw push + raw steal", [&] {
        (void)ownerp->try_push(++seq);
        const Item value = thiefp->try_steal().value_or(Item{0});
        bench::do_not_optimize(value);
    });
}

[[nodiscard]] bench::Report steal_typed_send_raw_steal() {
    using proto::detach_reason::TestInstrumentation;

    auto deque = std::make_unique<Deque>();
    auto owner = mint_owner(*deque);
    auto thief = ses::mint_chaselev_thief<Deque>(*deque);
    if (!thief) std::abort();
    auto* thiefp = &*thief;
    auto owner_psh = ses::mint_owner_session<Deque>(owner);
    Item seq = 0;

    auto report = measure("steal round-trip: typed hot push + raw steal", [&] {
        (void)ses::owner_session_try_push<Deque>(owner_psh, ++seq);
        const Item value = thiefp->try_steal().value_or(Item{0});
        bench::do_not_optimize(value);
    });

    std::move(owner_psh).detach(TestInstrumentation{});
    return report;
}

[[nodiscard]] bench::Report steal_raw_push_typed_recv() {
    using proto::detach_reason::TestInstrumentation;

    auto deque = std::make_unique<Deque>();
    auto owner = mint_owner(*deque);
    auto* ownerp = &owner;
    auto thief = ses::mint_chaselev_thief<Deque>(*deque);
    if (!thief) std::abort();
    auto thief_psh = ses::mint_thief_session<Deque>(*thief);
    Item seq = 0;

    auto report = measure("steal round-trip: raw push + typed hot recv", [&] {
        (void)ownerp->try_push(++seq);
        const auto borrowed = ses::thief_session_steal_borrowed<Deque>(
            thief_psh);
        bench::do_not_optimize(borrowed.value);
    });

    std::move(thief_psh).detach(TestInstrumentation{});
    return report;
}

[[nodiscard]] bench::Report steal_typed_send_typed_recv() {
    using proto::detach_reason::TestInstrumentation;

    auto deque = std::make_unique<Deque>();
    auto owner = mint_owner(*deque);
    auto thief = ses::mint_chaselev_thief<Deque>(*deque);
    if (!thief) std::abort();
    auto owner_psh = ses::mint_owner_session<Deque>(owner);
    auto thief_psh = ses::mint_thief_session<Deque>(*thief);
    Item seq = 0;

    auto report = measure("steal round-trip: typed hot push + typed hot recv",
        [&] {
        (void)ses::owner_session_try_push<Deque>(owner_psh, ++seq);
        const auto borrowed = ses::thief_session_steal_borrowed<Deque>(
            thief_psh);
        bench::do_not_optimize(borrowed.value);
    });

    std::move(owner_psh).detach(TestInstrumentation{});
    std::move(thief_psh).detach(TestInstrumentation{});
    return report;
}

}  // namespace

int main() {
    static_assert(sizeof(proto::PermissionedSessionHandle<
                      proto::End, proto::EmptyPermSet, Deque::OwnerHandle*>)
                  == sizeof(proto::SessionHandle<proto::End,
                                                 Deque::OwnerHandle*>));
    static_assert(sizeof(proto::PermissionedSessionHandle<
                      proto::End, proto::EmptyPermSet, Deque::ThiefHandle*>)
                  == sizeof(proto::SessionHandle<proto::End,
                                                 Deque::ThiefHandle*>));

    std::array reports{
        owner_raw_push_raw_pop(),
        owner_typed_send_raw_pop(),
        owner_raw_push_typed_recv(),
        owner_typed_send_typed_recv(),
        steal_raw_push_raw_steal(),
        steal_typed_send_raw_steal(),
        steal_raw_push_typed_recv(),
        steal_typed_send_typed_recv(),
    };

    bench::emit_reports_text(reports);

    std::puts("\n=== owner local round-trip deltas ===");
    bench::compare(reports[0], reports[1]).print_text();
    bench::compare(reports[0], reports[2]).print_text();
    bench::compare(reports[0], reports[3]).print_text();

    std::puts("\n=== cross-role steal round-trip deltas ===");
    bench::compare(reports[4], reports[5]).print_text();
    bench::compare(reports[4], reports[6]).print_text();
    bench::compare(reports[4], reports[7]).print_text();

    std::puts("\n=== verdict ===");
    std::puts("  Tier A: compile-time sizeof equality asserted for owner and "
              "thief pointer-resource PSH wrappers.");
    std::puts("  Tier B: timed round trips are steady-state and informational; "
              "any sub-cycle delta should be checked against CRUCIBLE_DUMP_ASM.");

    bench::emit_reports_json(reports, bench::env_json());
    return EXIT_SUCCESS;
}
