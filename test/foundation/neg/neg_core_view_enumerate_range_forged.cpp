// foundation/core/Region.h names detail::EnumerateStage, the root stage of
// the walk of enumerate(), as the friend of EnumerateCursor, and it also
// defines the stage.  A unit that includes Region.h therefore cannot give the
// stage a definition of its own and build a cursor with an end past the
// count of its View: the second definition is a redefinition.

#include <foundation/core/Region.h>

#include <cstddef>

template <class Element, std::size_t Count, ::foundation::brand::IsBrand Brand, std::size_t Extent>
class foundation::core::detail::EnumerateStage {
public:
    bool is_forged = true;
};

int main() { return 0; }
