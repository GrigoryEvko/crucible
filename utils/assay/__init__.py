"""assay — the Python package of the checks, the build and test loop and the tools of the tree.

The three masters in utils/bin (guard, loop and setup) are the only commands.
Each master puts utils/ on the module path, imports one module of this package
and calls its function main.  No module of the package is a command.
"""
