// The probe of utils/toolchain/gcc/patches/0006-*.patch.  The patched
// compiler shows the field of each capture of a closure to a read with
// unchecked access, and refuses it to a read with the access of this scope.
// The stock compiler shows no field, and refuses the first static_assert.

#include <meta>

constexpr int seed = 3;
inline constexpr auto copies_seed = [held = seed] { return held; };

static_assert(
    std::meta::nonstatic_data_members_of(^^decltype(copies_seed), std::meta::access_context::unchecked()).size() == 1);
static_assert(std::meta::type_of(std::meta::nonstatic_data_members_of(^^decltype(copies_seed),
                                                                      std::meta::access_context::unchecked())[0])
              == ^^int);
static_assert(
    std::meta::nonstatic_data_members_of(^^decltype(copies_seed), std::meta::access_context::current()).empty());
