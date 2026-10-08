// A capability probe of cmake/PatchedGccProbe.cmake.  Do not build it.
//
// utils/toolchain/gcc/patches/0004-*.patch gives a plugin six events of the
// C++ front end of GCC: PLUGIN_EXPLICIT_INSTANTIATION,
// PLUGIN_SIZEOF_OR_ALIGNOF, PLUGIN_NAME_USE, PLUGIN_USING_FORM,
// PLUGIN_MEMBER_ACCESS and PLUGIN_REFLECTION.  The rule proof-routes of the
// quarantine plugin of utils/tools/quarantine/ reads the first two.  The
// probe compiles with the plugin headers of the compiler, whose plugin.def
// gives each event.  A compiler without the patch refuses each static_assert
// of the six events, because it has no constant for them.
//
// PLUGIN_NAME_USE and PLUGIN_MEMBER_ACCESS are direct events.  The front end
// calls their callbacks through a slot of plugin.h, with no data struct and
// no timer.  The previous form of the patch gave the two events through
// invoke_plugin_callbacks, at approximately 80 instructions for each name
// lookup.  Its plugin.h has no slot, so that compiler refuses the last two
// static_asserts.  The plugin headers declare some types and a macro before
// plugin.h, so the probe declares them first.
#define DEFEVENT(NAME) inline constexpr char NAME##_text[] = #NAME;
#include <plugin.def>
#undef DEFEVENT

static_assert(sizeof(PLUGIN_EXPLICIT_INSTANTIATION_text) > 1);
static_assert(sizeof(PLUGIN_SIZEOF_OR_ALIGNOF_text) > 1);
static_assert(sizeof(PLUGIN_NAME_USE_text) > 1);
static_assert(sizeof(PLUGIN_USING_FORM_text) > 1);
static_assert(sizeof(PLUGIN_MEMBER_ACCESS_text) > 1);
static_assert(sizeof(PLUGIN_REFLECTION_text) > 1);

#include <cstdint>
#include <cstdio>
#include <type_traits>

typedef union tree_node* tree;
typedef std::uint64_t location_t;
enum insert_option {
    NO_INSERT,
    INSERT
};
#define ATTRIBUTE_UNUSED __attribute__((unused))
#include <plugin.h>

static_assert(std::is_same_v<decltype(plugin_name_use_slot.callback), plugin_name_use_func>);
static_assert(std::is_same_v<decltype(plugin_member_access_slot.callback), plugin_member_access_func>);

int main() { return 0; }
