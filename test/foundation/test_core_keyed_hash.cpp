// Tests of foundation/core/KeyedHash.h: keyed_hash is BLAKE3 in its keyed
// mode with 32 bytes of output.  The oracle is the file test_vectors.json of
// the official BLAKE3 repository at the commit 657284b1: the key is the text
// "whats the Elvish word for friend", and the input of length n holds the
// bytes i % 251.  The scalar arm of a constant evaluation and the arm of the
// rows at run time give the same bytes.

#include <foundation/core/KeyedHash.h>

#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::Hash256;
using ::foundation::core::Index;
using ::foundation::core::keyed_hash;
using ::foundation::core::KeyedHashKeyBytes;
using ::foundation::core::TextView;
using ::foundation::core::View;

constexpr std::size_t kLongest = 102400;

// The input of the vectors: the byte i % 251 at i.
template <std::size_t Length>
struct VectorInput final {
    FixedArray<std::byte, Length> bytes{};

    constexpr VectorInput() noexcept {
        for (auto const index : decltype(bytes)::indices()) {
            bytes.at(index) = static_cast<std::byte>(index.raw() % 251);
        }
    }
};

// The longest input of the vectors that one constant evaluation hashes.
constexpr std::size_t kConstantLongest = 2049;

constexpr KeyedHashKeyBytes vector_key() noexcept {
    constexpr TextView text{"whats the Elvish word for friend"};
    FixedArray<std::byte, 32> bytes{};
    std::size_t index = 0;
    for (char const letter : text) {
        bytes.at(Index<32>::check(index).expect("the key has 32 letters")) = static_cast<std::byte>(letter);
        ++index;
    }
    return KeyedHashKeyBytes{bytes.view()};
}

// The value of one hex digit.
[[nodiscard]] constexpr std::uint64_t digit_of(char letter) noexcept {
    return letter <= '9' ? static_cast<std::uint64_t>(letter - '0') : static_cast<std::uint64_t>(letter - 'a' + 10);
}

// The Hash256 of 64 hex digits: each 16 digits are the 8 bytes of one word,
// the first byte first.
[[nodiscard]] constexpr Hash256 hash_of_hex(TextView hex) noexcept {
    FixedArray<std::uint64_t, 4> words{};
    std::size_t at = 0;
    for (char const letter : hex) {
        std::size_t const byte = at / 2;
        std::uint64_t const nibble = digit_of(letter);
        std::uint64_t const shift = 8 * (byte % 8) + (at % 2 == 0 ? 4 : 0);
        words.at(Index<4>::check(byte / 8).expect("64 digits")) |= nibble << shift;
        ++at;
    }
    return Hash256{words.at(::foundation::core::index<0>()), words.at(::foundation::core::index<1>()),
                   words.at(::foundation::core::index<2>()), words.at(::foundation::core::index<3>())};
}

struct Vector final {
    std::size_t length = 0;
    TextView hex{};
};

// The first 32 bytes of the field keyed_hash of each case of the file.
constexpr FixedArray<Vector, 16> kVectors{
    Vector{0, "92b2b75604ed3c761f9d6f62392c8a9227ad0ea3f09573e783f1498a4ed60d26"},
    Vector{1, "6d7878dfff2f485635d39013278ae14f1454b8c0a3a2d34bc1ab38228a80c95b"},
    Vector{63, "bb1eb5d4afa793c1ebdd9fb08def6c36d10096986ae0cfe148cd101170ce37ae"},
    Vector{64, "ba8ced36f327700d213f120b1a207a3b8c04330528586f414d09f2f7d9ccb7e6"},
    Vector{65, "c0a4edefa2d2accb9277c371ac12fcdbb52988a86edc54f0716e1591b4326e72"},
    Vector{1023, "c951ecdf03288d0fcc96ee3413563d8a6d3589547f2c2fb36d9786470f1b9d6e"},
    Vector{1024, "75c46f6f3d9eb4f55ecaaee480db732e6c2105546f1e675003687c31719c7ba4"},
    Vector{1025, "357dc55de0c7e382c900fd6e320acc04146be01db6a8ce7210b7189bd664ea69"},
    Vector{2048, "879cf1fa2ea0e79126cb1063617a05b6ad9d0b696d0d757cf053439f60a99dd1"},
    Vector{2049, "9f29700902f7c86e514ddc4df1e3049f258b2472b6dd5267f61bf13983b78dd5"},
    Vector{3072, "044a0e7b172a312dc02a4c9a818c036ffa2776368d7f528268d2e6b5df191770"},
    Vector{3073, "68dede9bef00ba89e43f31a6825f4cf433389fedae75c04ee9f0cf16a427c95a"},
    Vector{8192, "dc9637c8845a770b4cbf76b8daec0eebf7dc2eac11498517f08d44c8fc00d58a"},
    Vector{8193, "954a2a75420c8d6547e3ba5b98d963e6fa6491addc8c023189cc519821b4a1f5"},
    Vector{31744, "efa53b389ab67c593dba624d898d0f7353ab99e4ac9d42302ee64cbf9939a419"},
    Vector{102400, "1c35d1a5811083fd7119f5d5d1ba027b4d01c0c6c49fb6ff2cf75393ea5db4a7"},
};

// The keyed hash of the first length bytes of the input.
template <std::size_t Length>
[[nodiscard]] constexpr Hash256 hash_of_prefix(VectorInput<Length> const& input, std::size_t length) noexcept {
    View<std::byte const> const message = input.bytes.view().window(0, length).expect("the length is in the input");
    return keyed_hash(vector_key(), message);
}

// The arm of a constant evaluation gives the vectors of up to three chunks.
constexpr bool constant_arm_gives_the_vectors() noexcept {
    VectorInput<kConstantLongest> const input{};
    for (Vector const& vector : kVectors) {
        if (vector.length > kConstantLongest) continue;
        if (hash_of_prefix(input, vector.length) != hash_of_hex(vector.hex)) return false;
    }
    return true;
}

void test_vectors() {
    static VectorInput<kLongest> const input{};
    for (Vector const& vector : kVectors) {
        CRUCIBLE_FATAL_INVARIANT(hash_of_prefix(input, vector.length) == hash_of_hex(vector.hex));
    }
}

void test_constant_arm() { static_assert(constant_arm_gives_the_vectors()); }

// The bytes of an output are the bytes of the vector, in the order of the
// hex digits: the digits 2i and 2i + 1 are the byte i.
constexpr bool bytes_follow_the_vector() noexcept {
    Vector const& vector = kVectors.at(::foundation::core::index<1>());
    FixedArray<std::byte, 32> const bytes = hash_of_hex(vector.hex).bytes();
    auto cursor = bytes.begin();
    std::uint64_t high = 0;
    bool is_high_digit = true;
    for (char const letter : vector.hex) {
        if (is_high_digit) {
            high = digit_of(letter);
        } else {
            if (static_cast<std::uint64_t>(*cursor) != ((high << 4) | digit_of(letter))) return false;
            ++cursor;
        }
        is_high_digit = !is_high_digit;
    }
    return true;
}

void test_bytes_of_an_output() { static_assert(bytes_follow_the_vector()); }

// Two keys give two hashes, and so do two messages that differ in one byte.
void test_key_and_message_take_part() {
    static VectorInput<kLongest> input{};
    FixedArray<std::byte, 32> other_key_bytes{};
    other_key_bytes.at(::foundation::core::index<0>()) = std::byte{1};
    KeyedHashKeyBytes const other_key{other_key_bytes.view()};
    View<std::byte const> const message = input.bytes.view().window(0, 200).expect("200 bytes");
    CRUCIBLE_FATAL_INVARIANT(keyed_hash(vector_key(), message) != keyed_hash(other_key, message));
    Hash256 const before = keyed_hash(vector_key(), message);
    // The byte at 150 holds 150, and the change puts 7 in its place.
    input.bytes.at(Index<kLongest>::check(150).expect("a byte of the message")) = std::byte{7};
    CRUCIBLE_FATAL_INVARIANT(keyed_hash(vector_key(), message) != before);
    input.bytes.at(Index<kLongest>::check(150).expect("a byte of the message")) = std::byte{150};
}

}  // namespace

int main() {
    test_vectors();
    test_constant_arm();
    test_bytes_of_an_output();
    test_key_and_message_take_part();
    return 0;
}
