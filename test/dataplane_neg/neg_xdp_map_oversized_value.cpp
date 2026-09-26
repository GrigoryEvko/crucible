#include <crucible/cntp/dataplane/Xdp.h>

#include <array>
#include <cstddef>

struct Key {
    std::uint32_t value = 0;
    [[nodiscard]] friend constexpr bool operator==(Key, Key) noexcept = default;
};

// Trivially copyable and standard layout, so it passes BpfScalar, but a
// kernel map stores its width in a 16-bit field, and 65536 does not fit.
struct WideValue {
    std::array<std::byte, 65536> bytes{};
};

// These two hold, so the one conjunct of the gate that can refuse the call
// below is the width.
static_assert(crucible::cntp::dataplane::BpfScalar<WideValue>);
static_assert(!crucible::cntp::dataplane::BpfMapElementWidth<WideValue>);

int main() {
    auto entries = crucible::cntp::dataplane::admit_bpf_map_entries(4).value();
    auto spec = crucible::cntp::dataplane::mint_bpf_map_spec<Key, WideValue>(
        crucible::cntp::dataplane::BpfMapKind::Hash, entries);
    return static_cast<int>(spec.value().max_entries.value());
}
