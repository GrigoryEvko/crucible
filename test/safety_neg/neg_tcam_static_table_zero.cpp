#include <crucible/cntp/Tcam.h>

namespace tcam = crucible::cntp::tcam;

tcam::TcamRules<0> bad_table{
    tcam::DeclaredTcamTable{tcam::TcamTablePlan{}}};

int main() {
    return static_cast<int>(bad_table.installed_rules());
}
