"""ste-prose — the docstrings and the comments of the package are in Simplified Technical English.

THE PROSE
    The rule reads each docstring and each comment of the modules of the
    package and of the masters (assay.guard.engines.meta.package).  A comment
    and the comments on the lines after it, at the same column, are one text.
    A text in backticks or in double quotation marks is a code item or a
    quoted text.
    It counts as one word, and the rule reads no word of it.  The rule reads
    no line of code: a line where less than 60 % of the words are words of
    letters, and a line with one of the characters = { } < > |, or with "["
    and "]".

THE SENTENCES
    A sentence ends at a word that ends with ".", "?" or "!", at an empty
    line, at a line of code, at a line that ends with ":", and at a heading of
    capital letters.  A sentence also ends before a list item, and before a
    line with less indent than the line before it.  A parameter line of a
    section Args: or Raises:, such as "path: The path", starts a list item.  Two spaces or more
    between two words of a line divide the cells of a table, and a sentence
    ends there too.

THE RULE
    The rule refuses:
      refused-word   a word of the table of the STE dictionary
                     (assay.guard.engines.meta.ste_dictionary), or a shape of
                     such a word, that no term row of the registry holds.  The
                     rule reads no word with "_", "/", ".", a digit or a capital
                     letter after its first letter, because such a word is a
                     name of the code.  It reads no rule ID of the registry,
                     such as one-implementation
      semicolon      a semicolon in a line of prose
      contraction    a word with the apostrophe of a contraction: "n't", "'re",
                     "'ve", "'ll", "'m", "'d", or "'s" after a pronoun
      `thus`         the word "thus", which the rules of the project refuse
      long-sentence  a sentence of more than 25 words
      no-complexity  the module of a rule, or a function of a reader of
                     utils/assay/readers/ that lists a directory or the files of
                     git, with no "Complexity:" in its docstring
    The entity of a finding is the function or the class that holds the text.
    For a refused word, the entity also gives the word: FUNCTION: WORD.

Complexity: linear in the length of the docstrings and the comments.
"""

import ast
from collections.abc import Iterator
from dataclasses import dataclass

from assay.core import report as check_report
from assay.guard.engines.meta import determinism, rule_implementations, ste_dictionary
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext
from assay.readers import python as python_reader

RULE = "ste-prose"
LONGEST_SENTENCE = 25
# The part of the words of a line of prose that are words of letters, at the minimum.
PROSE_WORDS = 0.6
CODE_CHARACTERS = frozenset("={}<>|")
SPAN_MARKS = frozenset('`"')
SPAN_WORD = "\x00"
SENTENCE_ENDS = (".", "?", "!")
# The marks after the point that ends a sentence, such as the parenthesis of "(refer to L4.)".
CLOSING_MARKS = ")]}'"
LEADING_MARKS = "([{'"
TRAILING_MARKS = ".,:;?!)]}'"
NAME_CHARACTERS = frozenset("_/.\\=:<>@#$%^&*+[]{}|~0123456789")
CONTRACTIONS = ("n't", "'re", "'ve", "'ll", "'m", "'d")
PRONOUNS = frozenset({"it", "that", "there", "what", "here", "let", "he", "she", "who", "where", "how", "this"})
PYTHON_CONSTANTS = frozenset({"True", "False", "None"})
LIST_MARKS = ("- ", "* ", "+ ")
COMPLEXITY = "Complexity:"
ENGINES = "utils/assay/guard/engines/"
READERS = "utils/assay/readers/"
FILE_LISTS = frozenset({"assay.core.git.tracked_files", "assay.core.paths.listing"})


@dataclass(frozen=True, slots=True)
class ProseLine:
    """One line of a docstring or of a comment: its line, its prose with each span replaced, and its holder."""

    line: int
    prose: str
    holder: str


@dataclass(frozen=True, slots=True)
class Sentence:
    """One sentence: its first line, its scope and its words."""

    line: int
    holder: str
    words: tuple[str, ...]


def masked(text: str) -> str:
    """Return a line in which one span word replaces each span in backticks or in double quotation marks."""
    parts: list[str] = []
    closing = ""
    for character in text:
        if closing:
            if character == closing:
                closing = ""
            continue
        if character in SPAN_MARKS:
            closing = character
            parts.append(f" {SPAN_WORD} ")
        else:
            parts.append(character)
    return "".join(parts)


def core(word: str) -> str:
    """Return a word with no mark before it and no mark after it, such as a comma or a parenthesis."""
    return word.lstrip(LEADING_MARKS).rstrip(TRAILING_MARKS)


def is_code(text: str) -> bool:
    """Return True when a line of the result of masked() is a line of code (THE PROSE)."""
    words = text.split()
    if any(character in CODE_CHARACTERS for character in text) or ("[" in text and "]" in text):
        return True
    letters = sum(1 for word in words if word == SPAN_WORD or core(word).replace("-", "").isalpha())
    return bool(words) and letters < PROSE_WORDS * len(words)


def is_heading(text: str) -> bool:
    """Return True when a line is a heading of capital letters, such as THE RULE."""
    stripped = text.strip()
    return any(character.isalpha() for character in stripped) and stripped == stripped.upper()


def is_list_item(text: str) -> bool:
    """Return True when a line starts a list item.

    A list item starts with a mark, or with a number and a point or a
    parenthesis.  A parameter line of a docstring section starts with a name
    and a colon, and it starts a list item too.
    """
    stripped = text.lstrip()
    head, _, rest = stripped.partition(" ")
    is_numbered = head[:-1].isdigit() and head[-1:] in (".", ")")
    is_parameter = head.endswith(":") and all(part.isidentifier() for part in head[:-1].lstrip("*").split("."))
    return stripped.startswith(LIST_MARKS) or (bool(rest) and (is_numbered or is_parameter))


def cells(text: str) -> list[str]:
    """Return the cells of a line: its text after the indent, divided at two spaces or more."""
    return [cell for cell in text.strip().split("  ") if cell.strip()]


def sentences(lines: list[ProseLine]) -> Iterator[Sentence]:
    """Give each sentence of a text of prose lines (THE SENTENCES).

    Complexity: linear in the length of the text.
    """
    words: list[str] = []
    start = 0
    holder = ""
    indent = 0
    for item in [*lines, ProseLine(0, "", "")]:
        prose = item.prose
        is_skipped = not prose.strip() or is_code(prose) or is_heading(prose)
        is_dedent = len(prose) - len(prose.lstrip()) < indent
        if words and (is_skipped or is_dedent or is_list_item(prose)):
            yield Sentence(start, holder, tuple(words))
            words = []
        if is_skipped:
            continue
        indent = len(prose) - len(prose.lstrip())
        for index, cell in enumerate(cells(prose)):
            if index and words:
                yield Sentence(start, holder, tuple(words))
                words = []
            for word in cell.split():
                if not words:
                    start, holder = item.line, item.holder
                words.append(word)
                if word.rstrip(CLOSING_MARKS).endswith(SENTENCE_ENDS):
                    yield Sentence(start, holder, tuple(words))
                    words = []
        if words and prose.rstrip().endswith(":"):
            yield Sentence(start, holder, tuple(words))
            words = []


def word_count(sentence: Sentence) -> int:
    """Return the number of the words of a sentence: each word with a letter or a digit, and each span word."""
    return sum(1 for word in sentence.words if word == SPAN_WORD or any(character.isalnum() for character in word))


def prose_texts(item: package_scope.PythonFile) -> list[list[ProseLine]]:
    """Return each docstring and each text of comments of one file, as lines of the result of masked()."""
    texts = [
        [
            ProseLine(docstring.line + index, masked(text), docstring.holder)
            for index, text in enumerate(docstring.body.split("\n"))
        ]
        for docstring in python_reader.docstrings(item.parsed)
    ]
    current: list[ProseLine] = []
    previous: python_reader.Comment | None = None
    for comment in python_reader.comments(item.parsed):
        if previous is not None and (comment.line != previous.line + 1 or comment.column != previous.column):
            texts.append(current)
            current = []
        current.append(ProseLine(comment.line, masked(comment.body), item.holder(comment.line)))
        previous = comment
    return [*texts, current] if current else texts


def is_name(word: str) -> bool:
    """Return True when a word is a name of the code.

    A name of the code is a constant of Python, or a word with a character of
    a name.  A word with a capital letter after its first letter is a name too.
    """
    return (
        word in PYTHON_CONSTANTS
        or any(character in NAME_CHARACTERS for character in word)
        or any(character.isupper() for character in word[1:])
    )


def word_findings(
    item: package_scope.PythonFile, sentence: Sentence, exempt: frozenset[str], rule_ids: frozenset[str]
) -> Iterator[check_report.Finding]:
    """Give the refused words, the contractions and each word "thus" of one sentence."""
    for word in sentence.words:
        bare = core(word).replace("’", "'")
        if not bare or bare == SPAN_WORD or is_name(bare.replace("'", "")) or bare in rule_ids:
            continue
        lower = bare.lower()
        if lower.endswith(CONTRACTIONS) or (lower.endswith("'s") and lower[:-2] in PRONOUNS):
            yield check_report.Finding(
                "error",
                item.path,
                sentence.line,
                RULE,
                f"the word {bare} is a contraction.  Write the full words",
                "contraction",
                sentence.holder,
            )
            continue
        if lower == "thus":
            yield check_report.Finding(
                "error",
                item.path,
                sentence.line,
                RULE,
                "the rules of the project refuse the word thus.  Start a new sentence",
                "thus",
                sentence.holder,
            )
            continue
        for part in lower.split("-"):
            base = (
                ste_dictionary.refused_base(part.removesuffix("'s"), exempt) if part.isalpha() or "'" in part else None
            )
            if base is not None:
                alternative = ste_dictionary.REFUSED[base][1]
                shape = "" if part == base else f", a shape of {base}"
                yield check_report.Finding(
                    "error",
                    item.path,
                    sentence.line,
                    RULE,
                    f"STE does not approve the word {bare}{shape}, and the "
                    f"dictionary gives: {alternative}.  Write the sentence again, or give {base} a term row when the "
                    f"project uses it as a technical noun or verb",
                    "refused-word",
                    f"{sentence.holder}: {base}",
                )


def semicolon_findings(item: package_scope.PythonFile, lines: list[ProseLine]) -> Iterator[check_report.Finding]:
    """Give each semicolon of a line of prose of one text."""
    for line in lines:
        if ";" in line.prose and not is_code(line.prose):
            yield check_report.Finding(
                "error",
                item.path,
                line.line,
                RULE,
                "a semicolon joins two sentences.  Write two sentences",
                "semicolon",
                line.holder,
            )


def lists_files(node: ast.FunctionDef | ast.AsyncFunctionDef, table: python_reader.Bindings) -> bool:
    """Return True when a function lists a directory or the files of git."""
    for call in ast.walk(node):
        if isinstance(call, ast.Call) and (
            determinism.is_listing(call, table) or python_reader.dotted(call.func, table) in FILE_LISTS
        ):
            return True
    return False


def complexity_findings(context: MetaContext, item: package_scope.PythonFile) -> Iterator[check_report.Finding]:
    """Give the module of a rule and each reader function that lists files, with no Complexity: in its docstring."""
    if item.path.startswith(ENGINES):
        has_function, family = rule_implementations.declared_rules(context.root / item.path)
        module_doc = ast.get_docstring(item.parsed.tree) or ""
        if (has_function or family) and COMPLEXITY not in module_doc:
            yield check_report.Finding(
                "error",
                item.path,
                1,
                RULE,
                f"the module of a rule has no {COMPLEXITY} line in its docstring.  Give the cost of the rule",
                "no-complexity",
                python_reader.MODULE_SCOPE,
            )
    if not item.path.startswith(READERS):
        return
    table = python_reader.bindings(item.parsed)
    for name, node in python_reader.scope_nodes(item.parsed.tree):
        if (
            isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
            and lists_files(node, table)
            and COMPLEXITY not in (ast.get_docstring(node) or "")
        ):
            yield check_report.Finding(
                "error",
                item.path,
                node.lineno,
                RULE,
                f"the reader function {name} lists files, and its docstring has no {COMPLEXITY} line.  Give its cost",
                "no-complexity",
                name,
            )


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each word, semicolon, contraction, long sentence and missing cost of the prose of the package."""
    exempt = frozenset(term.word for term in context.registry.terms)
    rule_ids = frozenset(context.registry.rules)

    def file_findings(item: package_scope.PythonFile) -> list[check_report.Finding]:
        found = list(complexity_findings(context, item))
        for lines in prose_texts(item):
            found += semicolon_findings(item, lines)
            for sentence in sentences(lines):
                found += word_findings(item, sentence, exempt, rule_ids)
                count = word_count(sentence)
                if count > LONGEST_SENTENCE:
                    found.append(
                        check_report.Finding(
                            "error",
                            item.path,
                            sentence.line,
                            RULE,
                            f"the sentence has {count} words, and "
                            f"a sentence has {LONGEST_SENTENCE} words at the maximum.  Write two sentences",
                            "long-sentence",
                            sentence.holder,
                        )
                    )
        return found

    return package_scope.each_file(context, RULE, file_findings)
