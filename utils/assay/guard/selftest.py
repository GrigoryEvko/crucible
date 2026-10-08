"""guard selftest — run the self-test of one module of the package.

usage: guard selftest MODULE [ARG]...

MODULE is the name of a module under assay, for example core.cache or
readers.tsast.  A test module under assay.tests is its own test: the command
calls its function main with each ARG.  For each other module, the command
calls its function main with --self-test and then each ARG.  The exit status
is the value that main returns.  A guard module runs its self-test through
`guard check --rule ID --self-test`.
"""

import sys


def main(argv: list[str]) -> int:
    """Run the self-test of the module that the command line names.

    Args:
        argv: The words after the subcommand: MODULE, and then the words for the self-test

    Returns:
        The value that main of the module returns, or 2 on a usage error
    """
    if not argv or not all(part.isidentifier() for part in argv[0].split(".")):
        print("usage: guard selftest MODULE [ARG]...  (MODULE is a module under assay, for example core.cache)",
              file=sys.stderr)
        return 2
    name = f"assay.{argv[0]}"
    try:
        __import__(name)
    except ModuleNotFoundError as error:
        if error.name is None or not (name == error.name or name.startswith(f"{error.name}.")):
            raise
        print(f"guard selftest: {name} is not a module of the package.  Give the name of a file under utils/assay, "
              f"with dots in place of the slashes and no suffix.", file=sys.stderr)
        return 2
    module = sys.modules[name]
    words = argv[1:] if argv[0].startswith("tests.") else ["--self-test", *argv[1:]]
    return module.main(words)
