// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags.

#include <crucible/fixy/spawn/JoinPolicy.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace join = ::crucible::fixy::spawn::join;

static_assert(join::IsJoinMechanismTag<join::AutoJoin>);
static_assert(join::IsJoinMechanismTag<join::ManualJoin>);
static_assert(join::IsJoinMechanismTag<join::Detached>);
static_assert(join::IsJoinMechanismTag<join::Cloned>);
static_assert(join::IsJoinMechanismTag<join::Forked>);
static_assert(join::IsJoinMechanismTag<join::PosixSpawn>);

static_assert(!join::IsJoinMechanismTag<int>);
static_assert(!join::IsJoinMechanismTag<unsigned char>);
static_assert(!join::IsJoinMechanismTag<void*>);
static_assert(!join::IsJoinMechanismTag<join::JoinMechanism>);

static_assert(join::mechanism_of_v<join::AutoJoin> == join::JoinMechanism::AutoJoin);
static_assert(join::mechanism_of_v<join::ManualJoin> == join::JoinMechanism::ManualJoin);
static_assert(join::mechanism_of_v<join::Detached> == join::JoinMechanism::Detached);
static_assert(join::mechanism_of_v<join::Cloned> == join::JoinMechanism::Cloned);
static_assert(join::mechanism_of_v<join::Forked> == join::JoinMechanism::Forked);
static_assert(join::mechanism_of_v<join::PosixSpawn> == join::JoinMechanism::PosixSpawn);

static_assert(sizeof(join::AutoJoin) == 1);
static_assert(sizeof(join::ManualJoin) == 1);
static_assert(sizeof(join::Detached) == 1);
static_assert(sizeof(join::Cloned) == 1);
static_assert(sizeof(join::Forked) == 1);
static_assert(sizeof(join::PosixSpawn) == 1);

static_assert(!std::is_same_v<join::AutoJoin, join::ManualJoin>);
static_assert(!std::is_same_v<join::AutoJoin, join::Detached>);
static_assert(!std::is_same_v<join::AutoJoin, join::Cloned>);
static_assert(!std::is_same_v<join::AutoJoin, join::Forked>);
static_assert(!std::is_same_v<join::AutoJoin, join::PosixSpawn>);
static_assert(!std::is_same_v<join::ManualJoin, join::Detached>);
static_assert(!std::is_same_v<join::ManualJoin, join::Cloned>);
static_assert(!std::is_same_v<join::ManualJoin, join::Forked>);
static_assert(!std::is_same_v<join::ManualJoin, join::PosixSpawn>);
static_assert(!std::is_same_v<join::Detached, join::Cloned>);
static_assert(!std::is_same_v<join::Detached, join::Forked>);
static_assert(!std::is_same_v<join::Detached, join::PosixSpawn>);
static_assert(!std::is_same_v<join::Cloned, join::Forked>);
static_assert(!std::is_same_v<join::Cloned, join::PosixSpawn>);
static_assert(!std::is_same_v<join::Forked, join::PosixSpawn>);

static_assert(std::is_same_v<join::Default, join::AutoJoin>);

// The tags are final so that a derived type cannot launder a foreign
// identity past the concept.
static_assert(std::is_final_v<join::AutoJoin>);
static_assert(std::is_final_v<join::ManualJoin>);
static_assert(std::is_final_v<join::Detached>);
static_assert(std::is_final_v<join::Cloned>);
static_assert(std::is_final_v<join::Forked>);
static_assert(std::is_final_v<join::PosixSpawn>);

static_assert(join::join_mechanism_count == 6);

// The ordinals are frozen. A new mechanism lands at the next free
// position and an existing position never renumbers, because consumers
// index a per-mechanism slot by the underlying value and a shifted
// ordinal would alias two mechanisms onto one slot.
static_assert(std::to_underlying(join::JoinMechanism::AutoJoin) == 0);
static_assert(std::to_underlying(join::JoinMechanism::ManualJoin) == 1);
static_assert(std::to_underlying(join::JoinMechanism::Detached) == 2);
static_assert(std::to_underlying(join::JoinMechanism::Cloned) == 3);
static_assert(std::to_underlying(join::JoinMechanism::Forked) == 4);
static_assert(std::to_underlying(join::JoinMechanism::PosixSpawn) == 5);

static_assert(join::name_of(join::JoinMechanism::AutoJoin) == "AutoJoin");
static_assert(join::name_of(join::JoinMechanism::ManualJoin) == "ManualJoin");
static_assert(join::name_of(join::JoinMechanism::Detached) == "Detached");
static_assert(join::name_of(join::JoinMechanism::Cloned) == "Cloned");
static_assert(join::name_of(join::JoinMechanism::Forked) == "Forked");
static_assert(join::name_of(join::JoinMechanism::PosixSpawn) == "PosixSpawn");

}  // namespace

int main() { return 0; }
