#pragma once

// The XDP programs and the BPF maps of the dataplane, as plans that the
// admission functions check.  Nothing here attaches a program to a NIC,
// makes a kernel map or loads verifier bytecode.  BpfMapImage is an array of
// this process that stands in for a kernel map.  The programs are the stubs
// of continuous monitoring under include/crucible/*/bpf/.

#include <crucible/cntp/Pacing.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::cntp::dataplane {

// The return codes of an XDP program, with the values of enum xdp_action of
// the kernel.
enum class XdpAction : std::uint8_t {
    Aborted = 0,
    Drop = 1,
    Pass = 2,
    Tx = 3,
    Redirect = 4,
};

enum class XdpMode : std::uint8_t {
    Generic = 0,
    Native = 1,
    Offload = 2,
};

enum class XdpProgramKind : std::uint8_t {
    FlowFilter = 0,
    GossipMulticast = 1,
    TcamAcl = 2,
};

enum class BpfMapKind : std::uint8_t {
    Hash = 0,
    LruHash = 1,
    Array = 2,
    PerCpuArray = 3,
    DevMap = 4,
};

// The flags of an update of a BPF map, with the values of BPF_ANY,
// BPF_NOEXIST and BPF_EXIST.
enum class BpfMapUpdate : std::uint8_t {
    Any = 0,
    NoExist = 1,
    Exist = 2,
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

// The log spelling of each enum comes from reflection.  A value that names
// no enumerator reads as "<unknown E>".
[[nodiscard]] constexpr ::fixy::TextView xdp_action_name(XdpAction action) noexcept {
    return ::foundation::core::enum_name_text(action);
}
[[nodiscard]] constexpr ::fixy::TextView xdp_mode_name(XdpMode mode) noexcept {
    return ::foundation::core::enum_name_text(mode);
}
[[nodiscard]] constexpr ::fixy::TextView xdp_program_kind_name(XdpProgramKind kind) noexcept {
    return ::foundation::core::enum_name_text(kind);
}
[[nodiscard]] constexpr ::fixy::TextView bpf_map_kind_name(BpfMapKind kind) noexcept {
    return ::foundation::core::enum_name_text(kind);
}
[[nodiscard]] constexpr ::fixy::TextView xdp_error_name(XdpError error) noexcept {
    return ::foundation::core::enum_name_text(error);
}

using XdpIfIndex = ::fixy::Positive<std::uint32_t>;
using PositiveMapEntries = ::fixy::Positive<std::uint32_t>;
using PositiveMapElementBytes = ::fixy::Positive<std::uint16_t>;

struct XdpProgramSpec {
    cntp::NicInterfaceName interface{};
    XdpIfIndex ifindex = ::fixy::mint_refined_constant<::fixy::positive, std::uint32_t{1}>();
    XdpMode mode = XdpMode::Native;
    XdpProgramKind kind = XdpProgramKind::FlowFilter;
    ::fixy::Bits<cog::NicFeature> required_features{};
};

struct BpfMapSpec {
    BpfMapKind kind = BpfMapKind::Hash;
    PositiveMapElementBytes key_bytes = ::fixy::mint_refined_constant<::fixy::positive, std::uint16_t{1}>();
    PositiveMapElementBytes value_bytes = ::fixy::mint_refined_constant<::fixy::positive, std::uint16_t{1}>();
    PositiveMapEntries max_entries = ::fixy::mint_refined_constant<::fixy::positive, std::uint32_t{1}>();
};

using DeclaredXdpProgram = ::fixy::Tagged<XdpProgramSpec, ::fixy::tags::source::Xdp>;
using DeclaredBpfMap = ::fixy::Tagged<BpfMapSpec, ::fixy::tags::source::BpfMap>;

// An attach plan is start-up work, so its mint takes a context that owns
// the initialization capability.
template <class Ctx>
concept CtxFitsXdpMint = ::foundation::effects::IsExecCtx<Ctx>
                      && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

// A kernel map copies the bytes of each key and each value.
template <class T>
concept BpfScalar = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

// A map image finds a key by the hash of its bytes, so two equal keys have
// the same bytes: no padding byte that the compiler adds, and an equality
// that reads each byte.
template <class K>
concept BpfKey = BpfScalar<K> && std::has_unique_object_representations_v<K> && requires(K const& a, K const& b) {
    { a == b } -> std::convertible_to<bool>;
};

// A kernel map stores the width of each key and each value in a 16-bit
// field, so a wider type cannot be a map element.  The refusal is at
// compile time.
template <class T>
concept BpfMapElementWidth = sizeof(T) <= ::fixy::max_of<std::uint16_t>;

template <class T>
concept BpfMapElement = BpfScalar<T> && BpfMapElementWidth<T>;

[[nodiscard]] constexpr ::fixy::Result<XdpIfIndex, XdpError> admit_xdp_ifindex(std::uint32_t ifindex) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(ifindex, XdpError::InvalidIfIndex);
}

[[nodiscard]] constexpr ::fixy::Result<PositiveMapEntries, XdpError>
admit_bpf_map_entries(std::uint32_t entries) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(entries, XdpError::InvalidMapEntries);
}

[[nodiscard]] constexpr ::fixy::Result<PositiveMapElementBytes, XdpError>
admit_bpf_map_element_bytes(std::uint16_t bytes) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bytes, XdpError::InvalidMapElementSize);
}

// The NIC features that an XDP program of the mode needs.  The generic mode
// runs in the kernel stack and needs none.
[[nodiscard]] constexpr ::fixy::Bits<cog::NicFeature> xdp_required_features(XdpMode mode) noexcept {
    switch (mode) {
        case XdpMode::Native:
            return ::fixy::Bits<cog::NicFeature>{cog::NicFeature::XdpNative};
        case XdpMode::Offload:
            return ::fixy::Bits<cog::NicFeature>{cog::NicFeature::XdpOffload};
        case XdpMode::Generic:
        default:
            return ::fixy::Bits<cog::NicFeature>{};
    }
}

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsXdpMint<Ctx>
[[nodiscard]] constexpr DeclaredXdpProgram mint_xdp_program(Ctx const&, cntp::NicInterfaceName iface,
                                                            XdpIfIndex ifindex, XdpProgramKind kind,
                                                            XdpMode mode = XdpMode::Native) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::Xdp>(XdpProgramSpec{
        .interface = iface,
        .ifindex = ifindex,
        .mode = mode,
        .kind = kind,
        .required_features = xdp_required_features(mode),
    });
}

// Each element has at least one byte, and BpfMapElement bounds its width by
// the 16-bit field, so the two widths are positive and fit by construction.
template <BpfMapElement Key, BpfMapElement Value>
[[nodiscard]] constexpr DeclaredBpfMap mint_bpf_map_spec(BpfMapKind kind, PositiveMapEntries entries) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::BpfMap>(BpfMapSpec{
        .kind = kind,
        .key_bytes = ::fixy::mint_refined<::fixy::positive>(static_cast<std::uint16_t>(sizeof(Key))),
        .value_bytes = ::fixy::mint_refined<::fixy::positive>(static_cast<std::uint16_t>(sizeof(Value))),
        .max_entries = entries,
    });
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, XdpError>
xdp_admit_nic(cog::CogIdentity const& identity, cog::NicPortTargetCaps const& caps,
              DeclaredXdpProgram const& program) noexcept {
    if (identity.kind != cog::CogKind::NicPort) {
        return ::fixy::err(XdpError::WrongCogKind);
    }
    XdpProgramSpec const& spec = program.value();
    if (spec.required_features.test(cog::NicFeature::XdpNative) && !caps.features.test(cog::NicFeature::XdpNative)) {
        return ::fixy::err(XdpError::MissingNativeXdp);
    }
    if (spec.required_features.test(cog::NicFeature::XdpOffload) && !caps.features.test(cog::NicFeature::XdpOffload)) {
        return ::fixy::err(XdpError::MissingOffloadXdp);
    }
    return ::fixy::Unit{};
}

namespace detail {

// The 32-bit FNV-1a hash of the bytes of a key.  Complexity: one pass over
// the sizeof(Key) bytes.
template <BpfScalar Key>
[[nodiscard]] constexpr std::uint32_t key_hash(Key const& key) noexcept {
    auto const bytes = ::fixy::bit_cast<::fixy::FixedArray<std::byte, sizeof(Key)>>(key);
    std::uint32_t hash = 2166136261u;
    for (std::byte const octet : bytes) {
        hash ^= static_cast<std::uint8_t>(octet);
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace detail

// An open-address hash table of at most MaxEntries keys, with linear probes.
// A lookup and an update take at most MaxEntries probes.  An erase moves each
// later key of the run of probes back, so no slot holds a tombstone.  The
// image does not evict a key, also for the kind LruHash: an update of a full
// image fails with MapFull.
template <BpfKey Key, BpfScalar Value, std::uint32_t MaxEntries, BpfMapKind Kind = BpfMapKind::Hash>
    requires(MaxEntries > 0)
class BpfMapImage : public ::foundation::Pinned<BpfMapImage<Key, Value, MaxEntries, Kind>> {
    using SlotIndex = ::fixy::Index<MaxEntries>;
    using UpdateResult = ::fixy::Result<::fixy::Unit, XdpError>;

    struct Slot {
        Key key{};
        Value value{};
        bool is_occupied = false;
    };

    ::fixy::FixedArray<Slot, MaxEntries> slots_{};
    std::uint32_t size_ = 0;

    // The slot where the probes of key start.
    [[nodiscard]] static constexpr SlotIndex home_of(Key const& key) noexcept {
        return SlotIndex::check(detail::key_hash(key) % MaxEntries).expect("a hash modulo the count is below it");
    }

    // The slot after index, and the first slot after the last one.
    [[nodiscard]] static constexpr SlotIndex next_of(SlotIndex index) noexcept {
        std::size_t const next = index.raw() + 1u;
        return SlotIndex::check(next == MaxEntries ? 0u : next).expect("the next slot of a wrap is below the count");
    }

    // The slot that holds key, or no value.  The probes stop at the first
    // empty slot.
    [[nodiscard]] constexpr ::fixy::Option<SlotIndex> find_slot(Key const& key) const noexcept {
        SlotIndex index = home_of(key);
        for (std::uint32_t probe = 0; probe < MaxEntries; ++probe) {
            Slot const& slot = slots_.at(index);
            if (!slot.is_occupied) {
                return ::fixy::none;
            }
            if (slot.key == key) {
                return ::fixy::some(index);
            }
            index = next_of(index);
        }
        return ::fixy::none;
    }

    // The first empty slot of the probes of key, or no value for a full image.
    [[nodiscard]] constexpr ::fixy::Option<SlotIndex> find_free_slot(Key const& key) const noexcept {
        SlotIndex index = home_of(key);
        for (std::uint32_t probe = 0; probe < MaxEntries; ++probe) {
            if (!slots_.at(index).is_occupied) {
                return ::fixy::some(index);
            }
            index = next_of(index);
        }
        return ::fixy::none;
    }

    [[nodiscard]] constexpr UpdateResult insert(Key const& key, Value const& value) noexcept {
        return find_free_slot(key).match(
            [this, &key, &value](SlotIndex index) noexcept -> UpdateResult {
                Slot const filled{.key = key, .value = value, .is_occupied = true};
                slots_.with_element(index, [&filled](Slot& held) noexcept { held = filled; });
                ++size_;
                return ::fixy::Unit{};
            },
            []() noexcept -> UpdateResult { return ::fixy::err(XdpError::MapFull); });
    }

public:
    using key_type = Key;
    using value_type = Value;
    static constexpr std::uint32_t max_entries = MaxEntries;
    static constexpr BpfMapKind kind = Kind;

    constexpr BpfMapImage() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return size_; }

    [[nodiscard]] constexpr bool is_full() const noexcept { return size_ == MaxEntries; }

    // The flags of mode give the same verdicts as the flags of a kernel map:
    // NoExist refuses a key that the image holds, and Exist refuses a key
    // that it does not hold.
    [[nodiscard]] constexpr UpdateResult update(Key const& key, Value const& value,
                                                BpfMapUpdate mode = BpfMapUpdate::Any) noexcept {
        return find_slot(key).match(
            [this, &value, mode](SlotIndex index) noexcept -> UpdateResult {
                if (mode == BpfMapUpdate::NoExist) {
                    return ::fixy::err(XdpError::KeyAlreadyExists);
                }
                slots_.with_element(index, [&value](Slot& held) noexcept { held.value = value; });
                return ::fixy::Unit{};
            },
            [this, &key, &value, mode]() noexcept -> UpdateResult {
                if (mode == BpfMapUpdate::Exist) {
                    return ::fixy::err(XdpError::KeyNotFound);
                }
                return insert(key, value);
            });
    }

    [[nodiscard]] constexpr ::fixy::Option<Value> lookup(Key const& key) const noexcept {
        return find_slot(key).match(
            [this](SlotIndex index) noexcept { return ::fixy::Option<Value>::some(slots_.at(index).value); },
            []() noexcept { return ::fixy::Option<Value>{::fixy::none}; });
    }

    // The erase empties the slot of key.  Then each later key of the run,
    // up to the next empty slot, moves into the empty slot when that slot
    // lies on its own probes, and its old slot becomes the empty slot.
    // So each key stays where a lookup finds it.  Complexity: the length of
    // the run, at most MaxEntries probes.
    [[nodiscard]] constexpr UpdateResult erase(Key const& key) noexcept {
        CRUCIBLE_TRY(hole, find_slot(key).ok_or(XdpError::KeyNotFound));
        SlotIndex current = next_of(hole);
        for (std::uint32_t probe = 1; probe < MaxEntries; ++probe) {
            Slot const& slot = slots_.at(current);
            if (!slot.is_occupied) {
                break;
            }
            std::uint64_t const home = home_of(slot.key).raw();
            std::uint64_t const home_to_hole = (std::uint64_t{hole.raw()} + MaxEntries - home) % MaxEntries;
            std::uint64_t const home_to_current = (std::uint64_t{current.raw()} + MaxEntries - home) % MaxEntries;
            if (home_to_hole < home_to_current) {
                slots_.with_element(hole, [moved = slot](Slot& held) noexcept { held = moved; });
                hole = current;
            }
            current = next_of(current);
        }
        slots_.with_element(hole, [](Slot& held) noexcept { held = Slot{}; });
        --size_;
        return ::fixy::Unit{};
    }
};

}  // namespace crucible::cntp::dataplane
