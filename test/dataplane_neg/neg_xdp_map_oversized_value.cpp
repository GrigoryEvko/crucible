// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A kernel map stores the width of each value in a 16-bit field.  A value
// of 65536 bytes is trivially copyable and has the standard layout, so it
// passes BpfScalar, but its width does not fit, and the mint refuses it.

#include <crucible/cntp/dataplane/Xdp.h>

struct Key {
    std::uint32_t value = 0;
    [[nodiscard]] friend constexpr bool operator==(Key const&, Key const&) noexcept = default;
};

struct WideValue {
    ::fixy::FixedArray<std::byte, 65536> bytes{};
};

// These hold, so the one conjunct of the gate that can refuse the call below
// is the width of the value.
static_assert(crucible::cntp::dataplane::BpfMapElement<Key>);
static_assert(crucible::cntp::dataplane::BpfScalar<WideValue>);
static_assert(!crucible::cntp::dataplane::BpfMapElementWidth<WideValue>);

int main() {
    auto const entries = crucible::cntp::dataplane::admit_bpf_map_entries(4).expect("4 entries are positive");
    auto const spec = crucible::cntp::dataplane::mint_bpf_map_spec<Key, WideValue>(
        crucible::cntp::dataplane::BpfMapKind::Hash, entries);
    return spec.value().max_entries.value() == 4u ? 0 : 1;
}
