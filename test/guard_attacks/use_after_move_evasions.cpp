// Uses after move that the use_after_move guard does not report.
//
// This file is not compiled.  The guard scans it with the rest of the
// tree, and test_use_after_move_attacks.py scans it alone.  Each function
// is one evasion: legal C++ that uses a name after moving it, in a shape
// the guard does not read.  When the guard learns a shape, it reports the
// function here, the tree-wide guard turns red, and the function and its
// ledger row in test_use_after_move_attacks.py are deleted.  The ledger
// only shrinks.

#include <utility>

namespace use_after_move_evasions {

struct Token {
    int value;
};
void sink(Token&&);
void read(Token const&);

// The guard reads tokens with the preprocessor lines removed, so a move
// inside a macro is not a move to it.
#define USE_AFTER_MOVE_GIVE(x) std::move(x)
void evade_macro(Token token) {
    sink(USE_AFTER_MOVE_GIVE(token));
    read(token);
}

// The guard knows std::move, not move reached by a using-declaration.
void evade_using_declaration(Token token) {
    using std::move;
    sink(move(token));
    read(token);
}

// A namespace alias spells std::move under another name.
void evade_namespace_alias(Token token) {
    namespace standard = std;
    sink(standard::move(token));
    read(token);
}

// move_if_noexcept and forward_like give an rvalue as std::move does.
void evade_move_if_noexcept(Token token) {
    sink(std::move_if_noexcept(token));
    read(token);
}

void evade_forward_like(Token token) {
    sink(std::forward_like<Token&&>(token));
    read(token);
}

// Parentheses around the name hide it from the key the guard records.
void evade_parentheses(Token token) {
    sink(std::move((token)));
    read(token);
}

// One member reached as token_ and as this->token_ is two keys.
struct Holder {
    Token token_;
    void evade_this_arrow() {
        sink(std::move(token_));
        read(this->token_);
    }
    void evade_this_dereference() {
        sink(std::move(this->token_));
        read((*this).token_);
    }
};

// A backward goto is a loop that the guard does not walk twice.
void evade_backward_goto(Token token, bool again) {
again_label:
    sink(std::move(token));
    if (again) goto again_label;
}

// An element of an array has no key.
void evade_array_element(Token (&tokens)[2]) {
    sink(std::move(tokens[0]));
    read(tokens[0]);
}

}  // namespace use_after_move_evasions
