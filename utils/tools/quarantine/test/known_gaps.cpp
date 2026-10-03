// The uses that the plugin does not see at this time.  Each line that
// KNOWN_GAPS of check_plugin.py names holds a library entity, and the plugin
// gives no finding there.  A change that closes a gap makes its row fail, and
// that change removes the row and adds the finding to the expectations, so
// the list only becomes shorter.

#include <cstring>
#include <string>
#include <utility>

unsigned long gap_folded = std::strlen("abc") + alignof(std::string) + noexcept(std::string{});

using std::swap;
