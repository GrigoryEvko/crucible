// The uses that the plugin does not see at this time.  Each line that
// KNOWN_GAPS of check_plugin.py names holds a library entity, and the plugin
// gives no finding there.  A change that closes a gap makes its row fail, and
// that change removes the row and adds the finding to the expectations, so
// the list only becomes shorter.

#include <cstring>
#include <string>
#include <utility>
#include <vector>

template <class T>
void gap_sink(T&& value) {
    static_cast<void>(value);
}

void gap_temporary() { gap_sink(std::vector<int>{1, 2}); }

unsigned long gap_size() { return sizeof(std::string); }

unsigned long gap_folded = std::strlen("abc");

using std::swap;
