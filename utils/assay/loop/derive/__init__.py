"""loop derive — write, compare or print one generated file of the tree.

usage: loop derive TARGET [ARG]...

TARGETS gives the module of each target.  The command calls the function main
of that module with each ARG, unchanged, and it exits with the value that
main returns.  The targets:

    mint-inventory  misc/mint-inventory.md and the HS14 floor (--write, --check, --check-floor)
    mint-model      print the mint model of include/, one line for each mint
    aten-ops        vessel/torch/aten_op_table.h, from the PyTorch tree of TORCH_DIR
"""

import sys

# Each target: the module whose function main writes, compares or prints it.
TARGETS = {
    "mint-inventory": "assay.loop.derive.mint_inventory",
    "mint-model": "assay.loop.derive.mint_model",
    "aten-ops": "assay.loop.derive.aten_ops",
}


def main(argv: list[str]) -> int:
    """Run the module of the target that the command line names.

    Args:
        argv: The words after the subcommand: TARGET, and then the words for its module

    Returns:
        The value that main of the module returns, or 2 on a usage error
    """
    if not argv or argv[0] not in TARGETS:
        print(f"usage: loop derive TARGET [ARG]...  (TARGET is one of: {', '.join(TARGETS)})", file=sys.stderr)
        return 2
    module_name = TARGETS[argv[0]]
    __import__(module_name)
    return sys.modules[module_name].main(argv[1:])
