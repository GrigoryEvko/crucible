#include "CodeAction.h"

#include <iostream>
#include <string>

int main() {
    namespace lsp = ::crucible::tools::lsp_code_action;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::cout << lsp::to_lsp_code_action(line) << '\n';
    }
    return std::cout.good() ? 0 : 1;
}
