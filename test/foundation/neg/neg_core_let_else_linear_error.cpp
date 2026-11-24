// CRUCIBLE_LET_ELSE drops the error of a failed Result.  A linear error must
// be consumed, so the macro refuses a Result with a linear error.

#include <foundation/Linear.h>
#include <foundation/core/Choice.h>

namespace {

class[[= ::foundation::linear::linear{}]] Refusal {
public:
    explicit Refusal(int code) noexcept : code_{code} {}
    Refusal(Refusal&& other) noexcept : code_{other.code_} {}
    Refusal(Refusal const&) = delete;
    Refusal& operator=(Refusal const&) = delete;
    Refusal& operator=(Refusal&&) = delete;
    ~Refusal() = default;

    [[nodiscard]] int code() const noexcept { return code_; }

private:
    int code_ = 0;
};

[[nodiscard]] ::foundation::core::Result<int, Refusal> checked(int value) noexcept {
    if (value < 0) return ::foundation::core::err(Refusal{value});
    return value;
}

[[nodiscard]] int checked_or_zero(int value) noexcept {
    CRUCIBLE_LET_ELSE(kept, checked(value), 0);
    return kept;
}

}  // namespace

int main() { return checked_or_zero(4) == 4 ? 0 : 1; }
