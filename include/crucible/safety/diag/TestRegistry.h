#pragma once

// Nothing checks ids for collision. Two entries that collide share one
// record. Consumers rank witnesses by tier and never by id equality, so a
// collision costs provenance detail and nothing else.

#include <cstdint>
#include <string_view>

namespace crucible::safety::diag {

// Active   — passes on the current run, so the evidence holds.
// Stale    — passes, but the last run is old enough to discount.
// Expired  — the source changed and the test did not run again.
// Revoked  — flagged by hand as untrustworthy, such as a flaky test.
//            Revocation is independent of whether the test passes.

enum class WitnessStatus : std::uint8_t {
    Active = 0,
    Stale = 1,
    Expired = 2,
    Revoked = 3,
};

[[nodiscard]] constexpr std::string_view witness_status_name(WitnessStatus s) noexcept {
    switch (s) {
        case WitnessStatus::Active:
            return "Active";
        case WitnessStatus::Stale:
            return "Stale";
        case WitnessStatus::Expired:
            return "Expired";
        case WitnessStatus::Revoked:
            return "Revoked";
        default:
            return std::string_view{"<unknown WitnessStatus>"};
    }
}

// An unregistered id is Active by default. A witness that pins no
// particular test must stay valid. Only a registered id can be revoked or
// go stale.

template <auto Id>
struct TestEntry final {
    static constexpr auto id_v = Id;
    static constexpr std::string_view name{"<unregistered>"};
    static constexpr std::string_view path{""};
    static constexpr WitnessStatus status = WitnessStatus::Active;
    static constexpr std::uint64_t last_run_epoch = 0;
};

template <auto Id>
inline constexpr bool is_active_test_v = TestEntry<Id>::status == WitnessStatus::Active;

inline constexpr std::uint64_t UnnamedTestId = 0;

namespace id {

inline constexpr std::uint64_t fixy_custom_optimizer = 0xC051'0F71'C012'E901ULL;
inline constexpr std::uint64_t fixy_forge_phase = 0xF067'E0CA'F034'AAE5ULL;
inline constexpr std::uint64_t fixy_mimic_backend_hook = 0xA17C'01EE'CBA0'C8F1ULL;
inline constexpr std::uint64_t fixy_cipher_writer = 0xC1AE'5FC0'1D5E'AB78ULL;

inline constexpr std::uint64_t fixy_revoked_demo = 0xBAD0'BAD0'BAD0'BAD0ULL;

}  // namespace id

template <>
struct TestEntry<id::fixy_custom_optimizer> final {
    static constexpr auto id_v = id::fixy_custom_optimizer;
    static constexpr std::string_view name = "test_fixy_custom_optimizer";
    static constexpr std::string_view path = "examples/fixy/example_fixy_custom_optimizer.cpp";
    static constexpr WitnessStatus status = WitnessStatus::Active;
    static constexpr std::uint64_t last_run_epoch = 1;
};

template <>
struct TestEntry<id::fixy_forge_phase> final {
    static constexpr auto id_v = id::fixy_forge_phase;
    static constexpr std::string_view name = "test_fixy_forge_phase";
    static constexpr std::string_view path = "examples/fixy/example_fixy_forge_phase.cpp";
    static constexpr WitnessStatus status = WitnessStatus::Active;
    static constexpr std::uint64_t last_run_epoch = 1;
};

template <>
struct TestEntry<id::fixy_mimic_backend_hook> final {
    static constexpr auto id_v = id::fixy_mimic_backend_hook;
    static constexpr std::string_view name = "test_fixy_mimic_backend_hook";
    static constexpr std::string_view path = "examples/fixy/example_fixy_mimic_backend_hook.cpp";
    static constexpr WitnessStatus status = WitnessStatus::Active;
    static constexpr std::uint64_t last_run_epoch = 1;
};

template <>
struct TestEntry<id::fixy_cipher_writer> final {
    static constexpr auto id_v = id::fixy_cipher_writer;
    static constexpr std::string_view name = "test_fixy_cipher_writer";
    static constexpr std::string_view path = "examples/fixy/example_fixy_cipher_writer.cpp";
    static constexpr WitnessStatus status = WitnessStatus::Active;
    static constexpr std::uint64_t last_run_epoch = 1;
};

template <>
struct TestEntry<id::fixy_revoked_demo> final {
    static constexpr auto id_v = id::fixy_revoked_demo;
    static constexpr std::string_view name = "fixy_revoked_demo (synthetic)";
    static constexpr std::string_view path = "<synthetic — registry demo>";
    static constexpr WitnessStatus status = WitnessStatus::Revoked;
    static constexpr std::uint64_t last_run_epoch = 0;
};

namespace test_registry_self_test {

static_assert(TestEntry<UnnamedTestId>::status == WitnessStatus::Active);
static_assert(is_active_test_v<UnnamedTestId>);

static_assert(is_active_test_v<id::fixy_custom_optimizer>);
static_assert(is_active_test_v<id::fixy_forge_phase>);
static_assert(is_active_test_v<id::fixy_mimic_backend_hook>);
static_assert(is_active_test_v<id::fixy_cipher_writer>);

static_assert(TestEntry<id::fixy_revoked_demo>::status == WitnessStatus::Revoked);
static_assert(!is_active_test_v<id::fixy_revoked_demo>);

static_assert(witness_status_name(WitnessStatus::Active) == "Active");
static_assert(witness_status_name(WitnessStatus::Revoked) == "Revoked");

}  // namespace test_registry_self_test

}  // namespace crucible::safety::diag
