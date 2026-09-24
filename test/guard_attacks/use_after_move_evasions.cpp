// Uses after move that the use_after_move guard does not report.
//
// This file is not compiled.  The guard scans it with the rest of the
// tree, and test_use_after_move_attacks.py scans it alone.  Each function
// is one evasion: legal C++ that uses a name after moving it, in a shape
// the guard does not read.  When the guard learns a shape, it reports the
// function here, the tree-wide guard turns red, and the function and its
// ledger row in test_use_after_move_attacks.py are deleted.  The ledger
// only shrinks.
//
// The self-test of the guard holds each shape that the guard learned from
// this file: a move inside a macro, a move reached by a using-declaration or
// a namespace alias, move_if_noexcept and forward_like, a parenthesised
// name, this->m and (*this).m, a backward goto, and an array element.

#include <array>
#include <cstddef>
#include <tuple>
#include <utility>

namespace use_after_move_evasions {

struct Token {
    int value;
};
void sink(Token&&);
void read(Token const&);

// An assignment through a variable index refills one element, which need
// not be the element that was moved.
void evade_refill_other_element(std::array<Token, 2>& tokens, std::size_t other) {
    sink(std::move(tokens[0]));
    tokens[other] = Token{};
    read(tokens[0]);
}

// at(0) names the element that tokens[0] moved.
void evade_read_through_at(std::array<Token, 2>& tokens) {
    sink(std::move(tokens[0]));
    read(tokens.at(0));
}

// A range for reads every element, the moved one with them.
void evade_read_through_range_for(std::array<Token, 2>& tokens) {
    sink(std::move(tokens[0]));
    for (Token const& token : tokens) read(token);
}

// get<0> names the same element on both lines.
void evade_read_through_get(std::tuple<Token, int>& pair) {
    sink(std::move(std::get<0>(pair)));
    read(std::get<0>(pair));
}

// A reference names the object that it binds.
void evade_read_through_alias(Token& token) {
    Token& alias = token;
    sink(std::move(alias));
    read(token);
}

// A structured binding names a member of the object.
struct Holder {
    Token token;
};
void evade_read_through_binding(Holder& holder) {
    auto& [bound] = holder;
    sink(std::move(bound));
    read(holder.token);
}

}  // namespace use_after_move_evasions
