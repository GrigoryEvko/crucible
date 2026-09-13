#pragma once

#include <crucible/Platform.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::fixy::spawn::join {

enum class JoinMechanism : std::uint8_t {
    AutoJoin = 0,
    ManualJoin = 1,
    Detached = 2,
    Cloned = 3,
    Forked = 4,
    PosixSpawn = 5,
};

inline constexpr std::size_t join_mechanism_count = std::meta::enumerators_of(^^JoinMechanism).size();

[[nodiscard]] consteval std::string_view name_of(JoinMechanism m) noexcept {
    switch (m) {
        case JoinMechanism::AutoJoin:
            return "AutoJoin";
        case JoinMechanism::ManualJoin:
            return "ManualJoin";
        case JoinMechanism::Detached:
            return "Detached";
        case JoinMechanism::Cloned:
            return "Cloned";
        case JoinMechanism::Forked:
            return "Forked";
        case JoinMechanism::PosixSpawn:
            return "PosixSpawn";
        default:
            return std::string_view{"<unknown JoinMechanism>"};
    }
}

// Each tag is final so no imposter can inherit from one and reach the
// identity allowlist below through a derived type.
struct AutoJoin final {
    static constexpr JoinMechanism mechanism = JoinMechanism::AutoJoin;
};
struct ManualJoin final {
    static constexpr JoinMechanism mechanism = JoinMechanism::ManualJoin;
};
struct Detached final {
    static constexpr JoinMechanism mechanism = JoinMechanism::Detached;
};
struct Cloned final {
    static constexpr JoinMechanism mechanism = JoinMechanism::Cloned;
};
struct Forked final {
    static constexpr JoinMechanism mechanism = JoinMechanism::Forked;
};
struct PosixSpawn final {
    static constexpr JoinMechanism mechanism = JoinMechanism::PosixSpawn;
};

// The gate is an identity allowlist rather than a structural check for a
// mechanism member. A structural check would admit any struct that declares
// the field, letting a caller substitute its own type for a declared tag.
template <typename T>
concept IsJoinMechanismTag = std::is_same_v<T, AutoJoin> || std::is_same_v<T, ManualJoin> || std::is_same_v<T, Detached>
                          || std::is_same_v<T, Cloned> || std::is_same_v<T, Forked> || std::is_same_v<T, PosixSpawn>;

template <typename T>
    requires IsJoinMechanismTag<T>
inline constexpr JoinMechanism mechanism_of_v = T::mechanism;

using Default = AutoJoin;

namespace detail::join_policy_self_test {

static_assert(join_mechanism_count == 6, "fixy::spawn::join::JoinMechanism universe drifted from six "
                                         "{AutoJoin, ManualJoin, Detached, Cloned, Forked, PosixSpawn}. "
                                         "Adding a mechanism is append-only at the next free ordinal, "
                                         "because a stored cache slot keys on the ordinal. Update the "
                                         "cardinality sentinel, the tag struct and the name_of switch together.");

[[nodiscard]] consteval bool every_mechanism_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^JoinMechanism));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (name_of([:en:]) == std::string_view{"<unknown JoinMechanism>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_mechanism_has_name(), "fixy::spawn::join::name_of switch is missing an arm for at "
                                          "least one JoinMechanism enumerator. Add the arm, or the new "
                                          "mechanism leaks the '<unknown JoinMechanism>' sentinel into "
                                          "debug output.");

static_assert(AutoJoin::mechanism == JoinMechanism::AutoJoin);
static_assert(ManualJoin::mechanism == JoinMechanism::ManualJoin);
static_assert(Detached::mechanism == JoinMechanism::Detached);
static_assert(Cloned::mechanism == JoinMechanism::Cloned);
static_assert(Forked::mechanism == JoinMechanism::Forked);
static_assert(PosixSpawn::mechanism == JoinMechanism::PosixSpawn);

static_assert(mechanism_of_v<AutoJoin> == JoinMechanism::AutoJoin);
static_assert(mechanism_of_v<ManualJoin> == JoinMechanism::ManualJoin);
static_assert(mechanism_of_v<Detached> == JoinMechanism::Detached);
static_assert(mechanism_of_v<Cloned> == JoinMechanism::Cloned);
static_assert(mechanism_of_v<Forked> == JoinMechanism::Forked);
static_assert(mechanism_of_v<PosixSpawn> == JoinMechanism::PosixSpawn);

static_assert(IsJoinMechanismTag<AutoJoin>);
static_assert(IsJoinMechanismTag<ManualJoin>);
static_assert(IsJoinMechanismTag<Detached>);
static_assert(IsJoinMechanismTag<Cloned>);
static_assert(IsJoinMechanismTag<Forked>);
static_assert(IsJoinMechanismTag<PosixSpawn>);

static_assert(!IsJoinMechanismTag<int>);
static_assert(!IsJoinMechanismTag<void>);
static_assert(!IsJoinMechanismTag<JoinMechanism>);

static_assert(std::is_empty_v<AutoJoin>);
static_assert(std::is_empty_v<ManualJoin>);
static_assert(std::is_empty_v<Detached>);
static_assert(std::is_empty_v<Cloned>);
static_assert(std::is_empty_v<Forked>);
static_assert(std::is_empty_v<PosixSpawn>);
static_assert(sizeof(AutoJoin) == 1);
static_assert(sizeof(ManualJoin) == 1);
static_assert(sizeof(Detached) == 1);
static_assert(sizeof(Cloned) == 1);
static_assert(sizeof(Forked) == 1);
static_assert(sizeof(PosixSpawn) == 1);

static_assert(!std::is_same_v<AutoJoin, ManualJoin>);
static_assert(!std::is_same_v<AutoJoin, Detached>);
static_assert(!std::is_same_v<ManualJoin, Detached>);
static_assert(!std::is_same_v<Cloned, Forked>);
static_assert(!std::is_same_v<Forked, PosixSpawn>);
static_assert(!std::is_same_v<Detached, PosixSpawn>);

static_assert(std::is_final_v<AutoJoin>);
static_assert(std::is_final_v<ManualJoin>);
static_assert(std::is_final_v<Detached>);
static_assert(std::is_final_v<Cloned>);
static_assert(std::is_final_v<Forked>);
static_assert(std::is_final_v<PosixSpawn>);

static_assert(std::is_same_v<Default, AutoJoin>);

static_assert(name_of(JoinMechanism::AutoJoin) == "AutoJoin");
static_assert(name_of(JoinMechanism::ManualJoin) == "ManualJoin");
static_assert(name_of(JoinMechanism::Detached) == "Detached");
static_assert(name_of(JoinMechanism::Cloned) == "Cloned");
static_assert(name_of(JoinMechanism::Forked) == "Forked");
static_assert(name_of(JoinMechanism::PosixSpawn) == "PosixSpawn");

}  // namespace detail::join_policy_self_test

}  // namespace crucible::fixy::spawn::join
