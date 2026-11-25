// The make of append_with captures a linear end by move.  The list calls make
// and does not consume it, so the end would end with make.  append_with refuses
// a make that holds a linear value, and the deleted twin gives the reason.

#include <foundation/Linear.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <utility>

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

struct AppendRole final {};

class LogWriter;
using Proof = ::foundation::SoleWriterProof<LogWriter, AppendRole>;
using Numbers = ::foundation::core::LiveList<int, Proof>;

class LogWriter final {
public:
    static void append(Numbers& list, End end) {
        list.append_with(Proof{}, ::foundation::effects::testing::test().alloc,
                         [held = std::move(end)](std::size_t) noexcept { return held.handle; });
    }
};

int main() {
    Numbers list;
    LogWriter::append(list, End{9});
    return static_cast<int>(list.prefix().size());
}
