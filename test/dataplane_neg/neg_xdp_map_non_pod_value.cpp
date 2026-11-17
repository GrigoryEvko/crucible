// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A kernel map copies the bytes of each value, so a map value is trivially
// copyable.  A value with a copy constructor of its own is not, and the
// mint of the map spec refuses it.

#include <crucible/cntp/dataplane/Xdp.h>

struct Key {
    std::uint32_t value = 0;
    [[nodiscard]] friend constexpr bool operator==(Key const&, Key const&) noexcept = default;
};

// The copy constructor counts the copies, so a byte copy is not a copy.
struct CountedValue {
    std::uint32_t copies = 0;

    CountedValue() noexcept = default;
    CountedValue(CountedValue const& other) noexcept : copies{other.copies + 1} {}
};

int main() {
    auto const entries = crucible::cntp::dataplane::admit_bpf_map_entries(4).expect("4 entries are positive");
    auto const spec = crucible::cntp::dataplane::mint_bpf_map_spec<Key, CountedValue>(
        crucible::cntp::dataplane::BpfMapKind::Hash, entries);
    return spec.value().max_entries.value() == 4u ? 0 : 1;
}
