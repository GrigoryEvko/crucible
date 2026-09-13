#include <crucible/fixy/Substr.h>

namespace fsubstr = ::crucible::fixy::substr;

// A using-directive naming a namespace that does not exist fails to parse, so
// the functions below are the witness that every substrate namespace resolves.
// They live in an anonymous namespace to satisfy -Wmissing-declarations.

namespace {

void reach_spsc() {
    using namespace fsubstr::spsc;
    (void)0;
}
void reach_swmr() {
    using namespace fsubstr::swmr;
    (void)0;
}
void reach_chaselev() {
    using namespace fsubstr::chaselev;
    (void)0;
}
void reach_metalog() {
    using namespace fsubstr::metalog;
    (void)0;
}
void reach_chainedge() {
    using namespace fsubstr::chainedge;
    (void)0;
}
void reach_mpmc() {
    using namespace fsubstr::mpmc;
    (void)0;
}
void reach_mpsc() {
    using namespace fsubstr::mpsc;
    (void)0;
}
void reach_calendar_grid() {
    using namespace fsubstr::calendar_grid;
    (void)0;
}
void reach_sharded_calendar_grid() {
    using namespace fsubstr::sharded_calendar_grid;
    (void)0;
}
void reach_sharded_grid() {
    using namespace fsubstr::sharded_grid;
    (void)0;
}

}  // namespace

int main() {
    reach_spsc();
    reach_swmr();
    reach_chaselev();
    reach_metalog();
    reach_chainedge();
    reach_mpmc();
    reach_mpsc();
    reach_calendar_grid();
    reach_sharded_calendar_grid();
    reach_sharded_grid();
    return 0;
}
