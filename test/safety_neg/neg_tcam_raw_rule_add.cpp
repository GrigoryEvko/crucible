#include <crucible/cntp/Tcam.h>

int main() {
    namespace tcam = crucible::cntp::tcam;

    tcam::TcamRules<4> table{
        tcam::DeclaredTcamTable{tcam::TcamTablePlan{}}};
    tcam::TcamFlowRule raw{};
    auto added = table.add_rule(raw);
    (void)added;
    return 0;
}
