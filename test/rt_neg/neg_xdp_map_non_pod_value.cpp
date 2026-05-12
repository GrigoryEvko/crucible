#include <crucible/rt/Xdp.h>

#include <string>

struct Key {
    std::uint32_t value = 0;
    [[nodiscard]] friend constexpr bool operator==(Key, Key) noexcept = default;
};

int main() {
    auto entries = crucible::rt::admit_bpf_map_entries(4).value();
    auto spec = crucible::rt::mint_bpf_map_spec<Key, std::string>(
        crucible::rt::BpfMapKind::Hash, entries);
    return spec.has_value() ? 0 : 1;
}
