// The corpus gate read the variable template is_in_corpus_v, which read the
// matcher templates of the entries and the traits is_internal_,
// is_row_observable_, is_stale_ and is_ghost_.  A unit could specialize each
// of them and admit a refused binding.  The gate now reads the concept
// IsInCorpus over the sealed premises, and each matcher is a member function
// that is not a template.  This file names the old variable template, and
// the name does not resolve.

#include <fixy/Corpus.h>

using fixy::corpus::is_in_corpus_v;

int main() { return 0; }
