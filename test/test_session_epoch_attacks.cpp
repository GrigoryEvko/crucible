// Attacks on the session epoch source.
//
// A wrapper claims an epoch and a generation, and every mint asks the one
// live source whether it still holds them.  The claim is only as good as
// the count the source keeps.  Each attack below is legal C++ that tries
// to make the source hold a coordinate that its advances did not reach.
// An attack that the source refuses is checked here.  An attack that
// still succeeds is an entry of the ledger at the foot of this file, and
// the ledger only shrinks.

#include <crucible/sessions/_SessionMint.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>

namespace session_epoch_attacks {

namespace proto = ::crucible::safety::proto;

// Two threads advance one source at once.  Each advance reads the count
// and stores its successor, so two advances that overlap must still add
// two.  A lost advance leaves a claim live that a later advance should
// have made stale.
[[nodiscard]] inline bool concurrent_advances_all_count() {
    constexpr std::uint64_t kPerThread = 200000;
    proto::SessionEpochSource source{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
    std::atomic<bool> go{false};
    const auto advancer = [&source, &go] {
        while (!go.load(std::memory_order_acquire)) {
        }
        for (std::uint64_t i = 0; i < kPerThread; ++i) source.advance_epoch();
    };
    std::thread first{advancer};
    std::thread second{advancer};
    go.store(true, std::memory_order_release);
    first.join();
    second.join();
    return source.holds(2 * kPerThread, 0);
}

struct KnownLimit {
    std::string_view attack;
    std::string_view reason;
};

inline constexpr KnownLimit kLedger[] = {
    {"a source rebuilt at the address of a dead source answers for the wrappers of the dead one",
     "a wrapper keeps the address of its source, and a new source at that address holds the same coordinate after "
     "the same advances; only a per-source identity in every wrapper would refuse it, which costs a word in each"},
};
static_assert(std::size(kLedger) <= 1, "the ledger only shrinks");

}  // namespace session_epoch_attacks

int main() {
    using namespace session_epoch_attacks;
    if (!concurrent_advances_all_count()) {
        std::fprintf(stderr, "FAIL: two concurrent advancers lost an advance\n");
        return EXIT_FAILURE;
    }
    std::printf("test_session_epoch_attacks: concurrent advances all count, %zu ledger entry\n", std::size(kLedger));
    return EXIT_SUCCESS;
}
