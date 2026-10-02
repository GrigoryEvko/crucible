// A source file of the base.  With the test directory as the source root, the
// uses of the library below are not findings.  check_plugin.py compiles this
// file a second time with src/ as the root.  The file is quarantined there,
// and each use below is a finding.

#include <cstring>
#include <vector>

namespace probe {

std::vector<int> floor_numbers;
char floor_bytes[8] = {};

void clear_floor(char* bytes) { std::memset(bytes, 0, 1); }

}  // namespace probe
