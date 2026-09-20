// The witnessed deref: a borrow that names what has to be presented
// before it can be read.
//
// fixy/Borrowed.h ties a borrow to an owner tag and a brand, and leaves
// WHEN it may be read to lexical scope.  That is the whole of what Rust
// gives.  The cells below stand on the two obligations this tree can
// state instead: a protocol position, and an effect row.
//
// Each cell is a pair.  The read that is admitted is shown to reach the
// bytes, and the neighbouring read that is refused is shown to be
// refused, because a gate that admitted everything would pass the first
// half of every cell.

#include <fixy/Witnessed.h>

#include <fixy/Borrowed.h>
#include <fixy/OwnedRegion.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;
namespace s = ::fixy::session;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

namespace {

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

// ── The two obligations, and what discharges each ────────────────────

struct Cache {
    using permission_row = eff::Row<>;
};
struct Spilled {
    using permission_row = eff::Row<eff::Effect::IO>;
};

using FgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;
using IoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;

struct Ping {
    int value = 0;
};
struct Wire {
    int last_sent = 0;
};

using Sending = s::Send<Ping, s::End>;

// A named brand for the type-level cells.  A borrow left at the old
// arity is DefaultBrand, which scripts/check-brand-drain.sh counts and
// does not let a new file add; every claim below reads the same at
// either brand.  The runtime cells mint their borrows, so they carry a
// fresh brand and never spell this one.
struct probe_brand {};

// The question every cell asks of the gate, written once.
template <typename Wit, typename Presented>
concept CanDeref = requires(Wit const& witnessed, Presented const& presented) { deref(witnessed, presented); };

// A requires-expression in a non-template context is checked where it
// is written rather than answered, so the accessor question lives in a
// concept too.
template <typename Wit>
concept HasBareAccessor = requires(Wit const& w) { w.size(); } || requires(Wit const& w) { w.data(); }
                       || requires(Wit const& w) { w[std::size_t{0}]; };

// ── EFFECT ───────────────────────────────────────────────────────────

void test_a_row_witnessed_borrow_reads_under_a_context_that_admits_it() {
    static std::uint64_t storage[4] = {2, 4, 6, 8};
    FgCtx ctx{};

    auto region = ::fixy::mint_owned_region(storage, std::size_t{4}, perm::mint_permission_root<Cache>());
    auto borrow = ::fixy::mint_borrowed(region);
    auto witnessed = ::fixy::mint_witnessed_under(borrow);

    // The obligation is the tag's, read off the borrow rather than
    // spelled by the caller, so there is nothing here to spell wrongly.
    static_assert(std::is_same_v<decltype(witnessed)::witness_type, ::fixy::witness::UnderRow<Cache>>);

    auto read = deref(witnessed, ctx);
    CRUCIBLE_TEST_REQUIRE(read.size() == 4);
    CRUCIBLE_TEST_REQUIRE(read[0] == 2);
    CRUCIBLE_TEST_REQUIRE(read[3] == 8);

    std::uint64_t sum = 0;
    for (std::uint64_t value : read) sum += value;
    CRUCIBLE_TEST_REQUIRE(sum == 20);
}

void test_a_context_that_refuses_the_row_refuses_the_read() {
    using CacheBorrow = ::fixy::Borrowed<std::uint64_t, Cache, probe_brand>;
    using SpilledBorrow = ::fixy::Borrowed<std::uint64_t, Spilled, probe_brand>;
    using CacheWitnessed = ::fixy::Witnessed<CacheBorrow, ::fixy::witness::UnderRow<Cache>>;
    using SpilledWitnessed = ::fixy::Witnessed<SpilledBorrow, ::fixy::witness::UnderRow<Spilled>>;

    // A region whose tag costs nothing is read from anywhere.  One whose
    // tag says IO is read only where IO is permitted, and the refusal is
    // the same Subrow test a lend from the permission pool already runs.
    static_assert(CanDeref<CacheWitnessed, FgCtx>);
    static_assert(!CanDeref<SpilledWitnessed, FgCtx>, "a foreground context does not admit a spilled region");
    static_assert(CanDeref<SpilledWitnessed, IoCtx>, "and a context that carries IO does");

    CRUCIBLE_TEST_REQUIRE(true);
}

// ── TEMPORAL ─────────────────────────────────────────────────────────

void test_a_protocol_witnessed_borrow_reads_while_the_session_is_there() {
    static std::uint64_t storage[3] = {5, 6, 7};

    auto handle = s::mint_session_handle<Sending, Wire>(Wire{});
    auto borrow = ::fixy::mint_borrowed<Cache>(storage);
    auto witnessed = ::fixy::mint_witnessed_at(handle, borrow);

    static_assert(std::is_same_v<decltype(witnessed)::witness_type, ::fixy::witness::AtProtocol<Sending>>);

    auto read = deref(witnessed, handle);
    CRUCIBLE_TEST_REQUIRE(read.size() == 3);
    CRUCIBLE_TEST_REQUIRE(read[1] == 6);

    // Advancing the session consumes the handle and hands back one at
    // the next position.  The borrow's witness names the old position,
    // so the new handle does not discharge it.
    auto at_end = std::move(handle).send(Ping{11}, [](Wire& wire, Ping&& ping) noexcept { wire.last_sent = ping.value; });
    static_assert(!CanDeref<decltype(witnessed), decltype(at_end)>,
                  "a handle one step on is not a handle at the position the borrow was minted at");

    Wire closed = std::move(at_end).close();
    CRUCIBLE_TEST_REQUIRE(closed.last_sent == 11);
}

void test_the_position_is_the_witness_and_not_the_spelling() {
    using AtSend = s::SessionHandle<Sending, Wire>;
    using AtEnd = s::SessionHandle<s::End, Wire>;
    using SendBorrow = ::fixy::Borrowed<std::uint64_t, Cache, probe_brand>;
    using Witnessed = ::fixy::Witnessed<SendBorrow, ::fixy::witness::AtProtocol<Sending>>;

    static_assert(CanDeref<Witnessed, AtSend>);
    static_assert(!CanDeref<Witnessed, AtEnd>, "the same handle spelling at another position is another witness");

    // The handle being linear is what makes that refusal worth
    // something: a copy would leave a witness behind at every step the
    // session took.
    static_assert(!std::is_copy_constructible_v<AtSend>);
    static_assert(!std::is_copy_assignable_v<AtSend>);

    CRUCIBLE_TEST_REQUIRE(true);
}

// ── One gate, two kinds ──────────────────────────────────────────────

void test_a_witness_of_one_kind_is_not_the_other() {
    using CacheBorrow = ::fixy::Borrowed<std::uint64_t, Cache, probe_brand>;
    using RowWitnessed = ::fixy::Witnessed<CacheBorrow, ::fixy::witness::UnderRow<Cache>>;
    using PositionWitnessed = ::fixy::Witnessed<CacheBorrow, ::fixy::witness::AtProtocol<Sending>>;
    using AtSend = s::SessionHandle<Sending, Wire>;

    static_assert(CanDeref<RowWitnessed, FgCtx>);
    static_assert(!CanDeref<RowWitnessed, AtSend>, "a handle does not stand in for a row");
    static_assert(CanDeref<PositionWitnessed, AtSend>);
    static_assert(!CanDeref<PositionWitnessed, FgCtx>, "a context does not stand in for a protocol position");

    // Neither carrier has an accessor of its own, so a read that skipped
    // the gate has nothing to call.
    static_assert(!HasBareAccessor<RowWitnessed>);
    static_assert(!HasBareAccessor<PositionWitnessed>);

    CRUCIBLE_TEST_REQUIRE(true);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_witnessed:\n");
    run_test("test_a_row_witnessed_borrow_reads_under_a_context_that_admits_it",
             test_a_row_witnessed_borrow_reads_under_a_context_that_admits_it);
    run_test("test_a_context_that_refuses_the_row_refuses_the_read", test_a_context_that_refuses_the_row_refuses_the_read);
    run_test("test_a_protocol_witnessed_borrow_reads_while_the_session_is_there",
             test_a_protocol_witnessed_borrow_reads_while_the_session_is_there);
    run_test("test_the_position_is_the_witness_and_not_the_spelling",
             test_the_position_is_the_witness_and_not_the_spelling);
    run_test("test_a_witness_of_one_kind_is_not_the_other", test_a_witness_of_one_kind_is_not_the_other);
    std::fprintf(stderr, "test_witnessed: %d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
