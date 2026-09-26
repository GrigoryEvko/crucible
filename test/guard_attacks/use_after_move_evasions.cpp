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
// name, this->m and (*this).m, a backward goto, an array element, a refill
// through a variable index, at(), a range for, std::get, a reference and a
// structured binding.

#include <utility>

namespace use_after_move_evasions {

struct Token {
    int value;
};
void sink(Token&&);
void read(Token const&);
void steal(Token& token);
Token& same(Token& token);
bool try_keep(Token token);

void evade_move_through_callee(Token token) {
    steal(token);
    read(token);
}

void evade_use_through_pointer(Token token) {
    Token* alias = &token;
    sink(std::move(token));
    read(*alias);
}

void evade_use_through_returned_reference(Token token) {
    Token& alias = same(token);
    sink(std::move(token));
    read(alias);
}

void evade_late_reference_capture(Token token) {
    auto later = [&token] { read(token); };
    sink(std::move(token));
    later();
}

void evade_try_that_moves_on_failure(Token token) {
    if (!try_keep(std::move(token))) {
        read(token);
    }
}

}  // namespace use_after_move_evasions
