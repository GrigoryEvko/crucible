"""assay.guard.legacy — the guard scripts of the tree, each one a module with its behavior unchanged.

The field legacy of each rule of utils/registry.txt gives the module of the
rule, and `guard check --rule ID` runs that module.  A rule module of an
engine replaces each legacy module, and this directory then holds no file.
"""
