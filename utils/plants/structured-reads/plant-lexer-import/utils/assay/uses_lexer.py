"""A planted module that imports the lexer that blanks comments."""

from assay.readers import tsast

import cxx_lex

USED = (tsast, cxx_lex)
