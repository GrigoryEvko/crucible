// The rule specialization-home of the quarantine plugin refuses a
// specialization of a template of the tree outside the file that defines the
// template.  C++ has no orphan rule, so without the plugin this file compiles,
// and each gate that reads the resource tag SmBudget<7> sees the forged tag.
// The plugin makes the specialization a compile error in each build.

#include <foundation/effects/Resources.h>

template <>
struct foundation::effects::resource::SmBudget<7> {};

int main() { return 0; }
