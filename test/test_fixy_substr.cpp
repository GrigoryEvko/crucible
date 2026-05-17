// ── test_fixy_substr — sentinel TU for fixy/Substr.h ───────────────
//
// Pulls fixy/Substr.h into a TU compiled under project warning flags
// so the header's compilation succeeds and every using-declaration
// resolves to a real substrate symbol.  Witnesses:
//
//   1. fixy::substr::spsc / swmr / chaselev / metalog / chainedge /
//      mpmc / calendar_grid / sharded_calendar_grid / sharded_grid
//      sub-namespaces compile under -Werror without an unknown-name
//      error.  Each declared `using` directive surfaces a real name
//      in its enclosing fixy::substr::<sub> namespace.
//   2. The substrate's namespace path is preserved verbatim — no
//      second-source mint authority is introduced.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_substr_*.cpp.

#include <crucible/fixy/Substr.h>

// Header-level success is the load-bearing claim — if any of the
// using-declarations referenced a non-existent substrate symbol,
// the include above would fail under -Werror.  This TU asserts
// that claim by simply including the header in a -Werror TU and
// instantiating its surface trivially.

namespace fsubstr = ::crucible::fixy::substr;

// Reachability witnesses — name introduction via using-directive
// confirms the namespace path resolves at parse time.  Local
// functions in an anonymous namespace satisfy -Wmissing-declarations.

namespace {

void reach_spsc()                  { using namespace fsubstr::spsc;                  (void)0; }
void reach_swmr()                  { using namespace fsubstr::swmr;                  (void)0; }
void reach_chaselev()              { using namespace fsubstr::chaselev;              (void)0; }
void reach_metalog()               { using namespace fsubstr::metalog;               (void)0; }
void reach_chainedge()             { using namespace fsubstr::chainedge;             (void)0; }
void reach_mpmc()                  { using namespace fsubstr::mpmc;                  (void)0; }
void reach_calendar_grid()         { using namespace fsubstr::calendar_grid;         (void)0; }
void reach_sharded_calendar_grid() { using namespace fsubstr::sharded_calendar_grid; (void)0; }
void reach_sharded_grid()          { using namespace fsubstr::sharded_grid;          (void)0; }

}  // namespace

int main() {
    reach_spsc();
    reach_swmr();
    reach_chaselev();
    reach_metalog();
    reach_chainedge();
    reach_mpmc();
    reach_calendar_grid();
    reach_sharded_calendar_grid();
    reach_sharded_grid();
    return 0;
}
