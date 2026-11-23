// A class outside the base states that no door builds its empty value.  A
// public nested class has a static function that takes a slot by a
// reference that is not const and writes the empty word into its private
// field.  The frame door with_mut gives the payload in place, so a full
// Option would read as empty.  A function of a nested class with a writable
// parameter that holds the slot is refused (the check of the statement of
// foundation/core/Choice.h).  The walk of the doors reads each nested class
// one time, so it refuses the class, and the Option of the class does not
// compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class NestedWriterSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit NestedWriterSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Writer {
        static constexpr void clear(NestedWriterSlot& slot) noexcept { slot.raw_ = 0xFFFFFFFFu; }
    };

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr NestedWriterSlot
    niche_empty(::foundation::core::niche_key<NestedWriterSlot> const&) noexcept {
        return NestedWriterSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<NestedWriterSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() { return ::foundation::core::Option<NestedWriterSlot>{}.is_some() ? 1 : 0; }
