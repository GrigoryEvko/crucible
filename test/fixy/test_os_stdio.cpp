// Tests of fixy/os/Stdio.h.  The line reader gives each line of its input
// without the newline, tells the end of the input from an empty line,
// gives a last line with no newline, refuses a line longer than its buffer
// and then gives the line after it, and gives the error of a read.  A read
// step over a fixed input stands in the place of standard input, and gives
// the input in parts of a stated size, so each line crosses the end of a
// read.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Stdio.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <cstddef>

namespace {

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using LineResult = ::fixy::Result<::fixy::Option<::fixy::TextView>, ::fixy::os::LineError>;

// The count of the bytes that one read of a test input gives at most.
using PartBytes = ::fixy::Quantity<::fixy::unit::Byte, std::size_t>;

// A read step over a fixed input.  Each read gives at most the part size,
// and a read at the end of the input gives zero.
class InputSteps final {
    ::fixy::TextView input_{};
    PartBytes part_{1};
    std::size_t position_ = 0;

public:
    InputSteps(::fixy::TextView input, PartBytes part) noexcept : input_{input}, part_{part} {}

    [[nodiscard]] ::fixy::Result<std::size_t, ::foundation::os::Errno> read(::fixy::View<char> room) noexcept {
        std::size_t const count = ::fixy::min(::fixy::min(input_.size() - position_, part_.raw()), room.size());
        ::fixy::TextView const part = input_.window(position_, count).expect("the part is inside the input");
        auto source = part.begin();
        for (char& slot : room.window(0, count).expect("the part fits the room")) {
            slot = *source;
            ++source;
        }
        position_ += count;
        return count;
    }
};

// True when the result is the line expected.
[[nodiscard]] bool gives_line(LineResult result, ::fixy::TextView expected) noexcept {
    return std::move(result).match(
        [expected](::fixy::Option<::fixy::TextView> line) noexcept {
            return line.match([expected](::fixy::TextView text) noexcept { return text == expected; },
                              []() noexcept { return false; });
        },
        [](::fixy::os::LineError) noexcept { return false; });
}

// True when the result is the end of the input.
[[nodiscard]] bool gives_end(LineResult result) noexcept {
    return std::move(result).match([](::fixy::Option<::fixy::TextView> line) noexcept { return line.is_none(); },
                                   [](::fixy::os::LineError) noexcept { return false; });
}

// True when the result is the error of a line that does not fit.
[[nodiscard]] bool gives_too_long(LineResult result) noexcept {
    return std::move(result).match([](::fixy::Option<::fixy::TextView>) noexcept { return false; },
                                   [](::fixy::os::LineError error) noexcept {
                                       return error.match([](::foundation::os::Errno) noexcept { return false; },
                                                          [](::fixy::os::LineTooLong) noexcept { return true; });
                                   });
}

// True when the result is the error expected of the kernel.
[[nodiscard]] bool gives_errno(LineResult result, ::foundation::os::Errno expected) noexcept {
    return std::move(result).match(
        [](::fixy::Option<::fixy::TextView>) noexcept { return false; },
        [expected](::fixy::os::LineError error) noexcept {
            return error.match([expected](::foundation::os::Errno code) noexcept { return code == expected; },
                               [](::fixy::os::LineTooLong) noexcept { return false; });
        });
}

// An empty line, a last line with no newline, and the end, which stays the
// end on the next call.  Each part size gives the same lines.
void test_lines_and_the_end(::fixy::check::Tally& checks, PartBytes part) {
    ::fixy::os::LineReader<8> reader;
    InputSteps input{::fixy::TextView{"ab\n\ncd"}, part};
    auto step = [&input](::fixy::View<char> room) noexcept { return input.read(room); };
    CRUCIBLE_TEST_EXPECT(checks, gives_line(reader.next_line_with_(step), ::fixy::TextView{"ab"}));
    CRUCIBLE_TEST_EXPECT(checks, gives_line(reader.next_line_with_(step), ::fixy::TextView{}));
    CRUCIBLE_TEST_EXPECT(checks, gives_line(reader.next_line_with_(step), ::fixy::TextView{"cd"}));
    CRUCIBLE_TEST_EXPECT(checks, gives_end(reader.next_line_with_(step)));
    CRUCIBLE_TEST_EXPECT(checks, gives_end(reader.next_line_with_(step)));
}

// A newline at the end of the input gives no empty line after it, and an
// empty input gives the end at once.
void test_a_newline_at_the_end(::fixy::check::Tally& checks) {
    ::fixy::os::LineReader<8> reader;
    InputSteps input{::fixy::TextView{"x\n"}, PartBytes{4}};
    auto step = [&input](::fixy::View<char> room) noexcept { return input.read(room); };
    CRUCIBLE_TEST_EXPECT(checks, gives_line(reader.next_line_with_(step), ::fixy::TextView{"x"}));
    CRUCIBLE_TEST_EXPECT(checks, gives_end(reader.next_line_with_(step)));

    ::fixy::os::LineReader<8> empty_reader;
    InputSteps empty{::fixy::TextView{}, PartBytes{4}};
    auto empty_step = [&empty](::fixy::View<char> room) noexcept { return empty.read(room); };
    CRUCIBLE_TEST_EXPECT(checks, gives_end(empty_reader.next_line_with_(empty_step)));
}

// A line of the capacity fits.  A longer line gives one error, also when it
// spans some reads, and the next call gives the line after it.  A long last
// line with no newline gives the error, and then the end.
void test_a_long_line(::fixy::check::Tally& checks, PartBytes part) {
    ::fixy::os::LineReader<4> reader;
    InputSteps input{::fixy::TextView{"abcd\nabcde\nfg\naaaaaaaaaaaaaaaaaaaa\nh\nabcdef"}, part};
    auto step = [&input](::fixy::View<char> room) noexcept { return input.read(room); };
    CRUCIBLE_TEST_EXPECT(checks, gives_line(reader.next_line_with_(step), ::fixy::TextView{"abcd"}));
    CRUCIBLE_TEST_EXPECT(checks, gives_too_long(reader.next_line_with_(step)));
    CRUCIBLE_TEST_EXPECT(checks, gives_line(reader.next_line_with_(step), ::fixy::TextView{"fg"}));
    CRUCIBLE_TEST_EXPECT(checks, gives_too_long(reader.next_line_with_(step)));
    CRUCIBLE_TEST_EXPECT(checks, gives_line(reader.next_line_with_(step), ::fixy::TextView{"h"}));
    CRUCIBLE_TEST_EXPECT(checks, gives_too_long(reader.next_line_with_(step)));
    CRUCIBLE_TEST_EXPECT(checks, gives_end(reader.next_line_with_(step)));
}

// An error of the read comes out as its Errno.
void test_an_error_of_the_read(::fixy::check::Tally& checks) {
    ::fixy::os::LineReader<8> reader;
    auto step = [](::fixy::View<char>) noexcept {
        return ::fixy::Result<std::size_t, ::foundation::os::Errno>{::fixy::err(::foundation::os::Errno::eio)};
    };
    CRUCIBLE_TEST_EXPECT(checks, gives_errno(reader.next_line_with_(step), ::foundation::os::Errno::eio));
}

}  // namespace

int main() {
    ::fixy::check::Tally checks{kTest, "test_os_stdio"};
    test_lines_and_the_end(checks, PartBytes{1});
    test_lines_and_the_end(checks, PartBytes{3});
    test_lines_and_the_end(checks, PartBytes{64});
    test_a_newline_at_the_end(checks);
    test_a_long_line(checks, PartBytes{1});
    test_a_long_line(checks, PartBytes{3});
    test_a_long_line(checks, PartBytes{64});
    test_an_error_of_the_read(checks);
    std::move(checks).close();
    ::fixy::check::pass(kTest, "test_os_stdio: all tests passed\n");
    return 0;
}
