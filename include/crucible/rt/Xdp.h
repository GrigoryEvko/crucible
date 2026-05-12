#pragma once

// GAPS-130 substrate slice. Runtime-owned XDP/BPF typed plans.
//
// This header does not attach programs to a NIC, allocate kernel maps, or load
// verifier bytecode. It pins the userspace contract first: source-tagged XDP
// program specs, source-tagged BPF map specs, NIC capability admission, Init-row
// minting, and a fixed-size in-process map image for deterministic tests and
// future loader wiring.

#include <crucible/cntp/Pacing.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::rt {

enum class XdpAction : std::uint8_t {
    Aborted  = 0,
    Drop     = 1,
    Pass     = 2,
    Tx       = 3,
    Redirect = 4,
};

enum class XdpMode : std::uint8_t {
    Generic = 0,
    Native  = 1,
    Offload = 2,
};

enum class XdpProgramKind : std::uint8_t {
    FlowFilter      = 0,
    AfXdpRedirect   = 1,
    GossipMulticast = 2,
    TcamAcl         = 3,
};

enum class BpfMapKind : std::uint8_t {
    Hash        = 0,
    LruHash     = 1,
    Array       = 2,
    PerCpuArray = 3,
    DevMap      = 4,
    XskMap      = 5,
};

enum class BpfMapUpdate : std::uint8_t {
    Any     = 0,
    NoExist = 1,
    Exist   = 2,
};

enum class XdpError : std::uint8_t {
    InvalidIfIndex,
    InvalidMapEntries,
    InvalidMapElementSize,
    WrongCogKind,
    MissingNativeXdp,
    MissingOffloadXdp,
    MapFull,
    KeyNotFound,
    KeyAlreadyExists,
};

[[nodiscard]] std::string_view xdp_action_name(XdpAction action) noexcept;
[[nodiscard]] std::string_view xdp_mode_name(XdpMode mode) noexcept;
[[nodiscard]] std::string_view xdp_program_kind_name(XdpProgramKind kind) noexcept;
[[nodiscard]] std::string_view bpf_map_kind_name(BpfMapKind kind) noexcept;
[[nodiscard]] std::string_view xdp_error_name(XdpError error) noexcept;

using XdpIfIndex = safety::Positive<std::uint32_t>;
using PositiveMapEntries = safety::Positive<std::uint32_t>;
using PositiveMapElementBytes = safety::Positive<std::uint16_t>;

struct XdpProgramSpec {
    cntp::NicInterfaceName interface{};
    XdpIfIndex ifindex{std::uint32_t{1}};
    XdpMode mode = XdpMode::Native;
    XdpProgramKind kind = XdpProgramKind::FlowFilter;
    safety::Bits<cog::NicFeature> required_features{};
};

struct BpfMapSpec {
    BpfMapKind kind = BpfMapKind::Hash;
    PositiveMapElementBytes key_bytes{std::uint16_t{1}};
    PositiveMapElementBytes value_bytes{std::uint16_t{1}};
    PositiveMapEntries max_entries{std::uint32_t{1}};
};

using DeclaredXdpProgram =
    safety::Tagged<XdpProgramSpec, safety::source::Xdp>;
using DeclaredBpfMap =
    safety::Tagged<BpfMapSpec, safety::source::BpfMap>;

template <class Ctx>
concept CtxFitsXdpMint =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

template <class T>
concept BpfScalar =
       std::is_trivially_copyable_v<T>
    && std::is_standard_layout_v<T>;

template <class K>
concept BpfKey =
       BpfScalar<K>
    && std::has_unique_object_representations_v<K>
    && requires(K a, K b) {
           { a == b } -> std::convertible_to<bool>;
       };

[[nodiscard]] constexpr std::expected<XdpIfIndex, XdpError>
admit_xdp_ifindex(std::uint32_t ifindex) noexcept {
    if (ifindex == 0) {
        return std::unexpected(XdpError::InvalidIfIndex);
    }
    return XdpIfIndex{ifindex, typename XdpIfIndex::Trusted{}};
}

[[nodiscard]] constexpr std::expected<PositiveMapEntries, XdpError>
admit_bpf_map_entries(std::uint32_t entries) noexcept {
    if (entries == 0) {
        return std::unexpected(XdpError::InvalidMapEntries);
    }
    return PositiveMapEntries{
        entries, typename PositiveMapEntries::Trusted{}};
}

[[nodiscard]] constexpr std::expected<PositiveMapElementBytes, XdpError>
admit_bpf_map_element_bytes(std::uint16_t bytes) noexcept {
    if (bytes == 0) {
        return std::unexpected(XdpError::InvalidMapElementSize);
    }
    return PositiveMapElementBytes{
        bytes, typename PositiveMapElementBytes::Trusted{}};
}

[[nodiscard]] constexpr safety::Bits<cog::NicFeature>
xdp_required_features(XdpMode mode) noexcept {
    switch (mode) {
        case XdpMode::Native:
            return safety::Bits<cog::NicFeature>{cog::NicFeature::XdpNative};
        case XdpMode::Offload:
            return safety::Bits<cog::NicFeature>{cog::NicFeature::XdpOffload};
        case XdpMode::Generic:
        default:
            return {};
    }
}

template <class Ctx>
    requires CtxFitsXdpMint<Ctx>
[[nodiscard]] constexpr DeclaredXdpProgram
mint_xdp_program(Ctx const&,
                 cntp::NicInterfaceName iface,
                 XdpIfIndex ifindex,
                 XdpProgramKind kind,
                 XdpMode mode = XdpMode::Native) noexcept {
    return DeclaredXdpProgram{XdpProgramSpec{
        .interface = iface,
        .ifindex = ifindex,
        .mode = mode,
        .kind = kind,
        .required_features = xdp_required_features(mode),
    }};
}

template <BpfScalar Key, BpfScalar Value>
[[nodiscard]] constexpr std::expected<DeclaredBpfMap, XdpError>
mint_bpf_map_spec(BpfMapKind kind, PositiveMapEntries entries) noexcept {
    if constexpr (sizeof(Key) > std::numeric_limits<std::uint16_t>::max()
                  || sizeof(Value) > std::numeric_limits<std::uint16_t>::max()) {
        return std::unexpected(XdpError::InvalidMapElementSize);
    }
    constexpr auto key_bytes = static_cast<std::uint16_t>(sizeof(Key));
    constexpr auto value_bytes = static_cast<std::uint16_t>(sizeof(Value));
    auto admitted_key = admit_bpf_map_element_bytes(key_bytes);
    auto admitted_value = admit_bpf_map_element_bytes(value_bytes);
    if (!admitted_key.has_value() || !admitted_value.has_value()) {
        return std::unexpected(XdpError::InvalidMapElementSize);
    }
    return DeclaredBpfMap{BpfMapSpec{
        .kind = kind,
        .key_bytes = *admitted_key,
        .value_bytes = *admitted_value,
        .max_entries = entries,
    }};
}

[[nodiscard]] constexpr std::expected<void, XdpError>
xdp_admit_nic(cog::CogIdentity const& identity,
              cog::NicPortTargetCaps const& caps,
              DeclaredXdpProgram program) noexcept {
    if (identity.kind != cog::CogKind::NicPort) {
        return std::unexpected(XdpError::WrongCogKind);
    }

    XdpProgramSpec const& spec = program.value();
    if (spec.required_features.test(cog::NicFeature::XdpNative)
        && !caps.features.test(cog::NicFeature::XdpNative)) {
        return std::unexpected(XdpError::MissingNativeXdp);
    }
    if (spec.required_features.test(cog::NicFeature::XdpOffload)
        && !caps.features.test(cog::NicFeature::XdpOffload)) {
        return std::unexpected(XdpError::MissingOffloadXdp);
    }
    return {};
}

namespace detail {

template <BpfScalar Key>
[[nodiscard]] constexpr std::uint32_t key_hash(Key const& key) noexcept {
    auto bytes = std::as_bytes(std::span{&key, std::size_t{1}});
    std::uint32_t h = 2'166'136'261u;
    for (std::byte b : bytes) {
        h ^= static_cast<std::uint8_t>(b);
        h *= 16'777'619u;
    }
    return h;
}

}  // namespace detail

template <BpfKey Key,
          BpfScalar Value,
          std::uint32_t MaxEntries,
          BpfMapKind Kind = BpfMapKind::Hash>
    requires (MaxEntries > 0)
class BpfMapImage
    : public safety::Pinned<BpfMapImage<Key, Value, MaxEntries, Kind>> {
    struct Slot {
        Key key{};
        Value value{};
        bool occupied = false;
    };

    std::array<Slot, MaxEntries> slots_{};
    std::uint32_t size_ = 0;

    [[nodiscard]] constexpr std::uint32_t first_slot(Key const& key) const noexcept {
        if constexpr (std::has_single_bit(MaxEntries)) {
            return detail::key_hash(key) & (MaxEntries - 1u);
        } else {
            return detail::key_hash(key) % MaxEntries;
        }
    }

    [[nodiscard]] constexpr std::optional<std::uint32_t>
    find_slot(Key const& key) const noexcept {
        const std::uint32_t start = first_slot(key);
        for (std::uint32_t probe = 0; probe < MaxEntries; ++probe) {
            const std::uint32_t idx = (start + probe) % MaxEntries;
            if (!slots_[idx].occupied) {
                return std::nullopt;
            }
            if (slots_[idx].key == key) {
                return idx;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<std::uint32_t>
    first_free_slot(Key const& key) const noexcept {
        const std::uint32_t start = first_slot(key);
        for (std::uint32_t probe = 0; probe < MaxEntries; ++probe) {
            const std::uint32_t idx = (start + probe) % MaxEntries;
            if (!slots_[idx].occupied) {
                return idx;
            }
        }
        return std::nullopt;
    }

public:
    using key_type = Key;
    using value_type = Value;
    static constexpr std::uint32_t max_entries = MaxEntries;
    static constexpr BpfMapKind kind = Kind;

    constexpr BpfMapImage() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] constexpr bool full() const noexcept {
        return size_ == MaxEntries;
    }

    [[nodiscard]] constexpr std::expected<void, XdpError>
    update(Key key, Value value, BpfMapUpdate mode = BpfMapUpdate::Any) noexcept {
        auto existing = find_slot(key);
        if (existing.has_value()) {
            if (mode == BpfMapUpdate::NoExist) {
                return std::unexpected(XdpError::KeyAlreadyExists);
            }
            slots_[*existing].value = value;
            return {};
        }
        if (mode == BpfMapUpdate::Exist) {
            return std::unexpected(XdpError::KeyNotFound);
        }
        auto free = first_free_slot(key);
        if (!free.has_value()) {
            return std::unexpected(XdpError::MapFull);
        }
        slots_[*free] = Slot{.key = key, .value = value, .occupied = true};
        ++size_;
        return {};
    }

    [[nodiscard]] constexpr std::optional<Value>
    lookup(Key const& key) const noexcept {
        auto idx = find_slot(key);
        if (!idx.has_value()) {
            return std::nullopt;
        }
        return slots_[*idx].value;
    }

    [[nodiscard]] constexpr std::expected<void, XdpError>
    erase(Key const& key) noexcept {
        auto idx = find_slot(key);
        if (!idx.has_value()) {
            return std::unexpected(XdpError::KeyNotFound);
        }
        slots_[*idx].occupied = false;
        --size_;

        std::array<Slot, MaxEntries> old = slots_;
        slots_ = {};
        std::uint32_t old_size = size_;
        size_ = 0;
        for (Slot const& slot : old) {
            if (slot.occupied) {
                (void)update(slot.key, slot.value, BpfMapUpdate::Any);
            }
        }
        return size_ == old_size ? std::expected<void, XdpError>{}
                                 : std::unexpected(XdpError::MapFull);
    }
};

static_assert(static_cast<std::uint8_t>(XdpAction::Pass) == 2);
static_assert(sizeof(XdpIfIndex) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveMapEntries) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveMapElementBytes) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredXdpProgram) == sizeof(XdpProgramSpec));
static_assert(sizeof(DeclaredBpfMap) == sizeof(BpfMapSpec));
static_assert(std::is_trivially_copyable_v<XdpProgramSpec>);
static_assert(std::is_trivially_copyable_v<BpfMapSpec>);

}  // namespace crucible::rt
