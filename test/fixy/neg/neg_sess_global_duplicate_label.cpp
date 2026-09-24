// A receiver identifies the branch of a choice by its label.  Two
// branches with one label leave the receiver unable to tell them apart,
// even when their payload types differ.
//
// The two roles are distinct and the choice has branches, so the only
// clause that can refuse is the distinct-label clause.

#include <fixy/session/Global.h>

namespace {

namespace g = ::fixy::session::global;

struct Client {};
struct Server {};
struct Request {};

using Ambiguous = g::Comm<Client, Server, g::Branch<Request, int, g::End>, g::Branch<Request, char, g::End>>;

constexpr int declare_protocol() noexcept {
    g::ensure_global_well_formed<Ambiguous>();
    return 0;
}

}  // namespace

int main() { return declare_protocol(); }
