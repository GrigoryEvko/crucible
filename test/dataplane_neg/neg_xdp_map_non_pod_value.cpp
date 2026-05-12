#include <crucible/cntp/dataplane/Xdp.h>

#include <string>

struct Key {
    std::uint32_t value = 0;
    [[nodiscard]] friend constexpr bool operator==(Key, Key) noexcept = default;
};

int main() {
    auto entries = crucible::cntp::dataplane::admit_bpf_map_entries(4).value();
    auto spec = crucible::cntp::dataplane::mint_bpf_map_spec<Key, std::string>(
        crucible::cntp::dataplane::BpfMapKind::Hash, entries);
    return spec.has_value() ? 0 : 1;
}
