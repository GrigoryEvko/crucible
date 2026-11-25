// The end of a LiveList ends its elements with no consume, so a linear element
// is refused.

#include <foundation/Linear.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>

namespace {
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] End {
    int handle = 0;
    constexpr explicit End(int value) noexcept : handle{value} {}
    constexpr End(End&&) noexcept = default;
    constexpr End& operator=(End&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    End(End const&) = delete("a linear end of the fixture does not copy");
    End& operator=(End const&) = delete("a linear end of the fixture does not copy");
};
}  // namespace

class LogWriter;
struct AppendRole final {};

int main() {
    // The size is the one use of the type.
    static_cast<void>(sizeof(foundation::core::LiveList<End, foundation::SoleWriterProof<LogWriter, AppendRole>>));
    return 0;
}
