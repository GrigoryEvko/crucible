// The type of a reflection is a typedef of namespace std::meta.  The test
// table does not admit it, so the declaration is a std_entity finding.
// check_plugin.py compiles this file with -freflection.

#include <meta>

constexpr std::meta::info meta_info_reflection = ^^int;
