// V203: a replay-deterministic payload x a non-deterministic timestamp.
//
// The payload's DetSafe band at Pure claims the bytes are the same on
// every replay.  A timestamp counter read differs per run by
// construction, so a binding that reads one cannot keep that claim, and
// replay cannot be bit-exact.
//
// This is the first rule to consume the payload read that the previous
// commit added: rules_of<Payload, Atoms...> reads the band from the fn's
// first template parameter.  The pack is not hot and carries no other
// premise, so the fixture floors on V203 alone; V201 needs the hot tier
// and stands down.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>,
                                ::fixy::atom::hw::non_deterministic_tsc>
        refused{};
    return 0;
}
