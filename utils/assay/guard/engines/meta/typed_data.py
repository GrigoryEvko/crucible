"""typed-data — each record is a frozen dataclass with slots, and no public type is a dictionary of strings.

THE RULE
    A record of the package has a set of named fields that does not change.
    A value that changes after its construction is a defect that no type
    shows.  The rule reads each module of the package and each master
    (assay.guard.engines.meta.package), and it refuses:
      not-frozen   a class with the decorator dataclass and no frozen=True
      no-slots     a class with the decorator dataclass and no slots=True
      string-dict  the type dict[str, str], dict[str, object] or
                   dict[str, Any] in the signature of a public function or in
                   a field of a class.  The rule also reads a type in a string
    A public function is a function of a module or a method of a class.  Its
    name and the names of its holders do not start with "_".  A function in a
    function is not public.  A name that starts and ends with "__", such as
    __init__, is public.  A class with a reason to change after its
    construction keeps a ledger row with that reason.  The entity of a
    finding is the full name of the class, of the function or of the field.

Complexity: linear in the length of the files of the scope.
"""

import ast

from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext
from assay.readers import python as python_reader

RULE = "typed-data"
DATACLASS = "dataclasses.dataclass"
# The keywords that a dataclass gives, and the kind of a finding when one is missing.
REQUIRED_KEYWORDS = {"frozen": "not-frozen", "slots": "no-slots"}
DICTIONARIES = frozenset({"dict", "typing.Dict"})
STRING = "str"
REFUSED_VALUES = frozenset({"str", "object", "typing.Any"})


def dataclass_keywords(node: ast.ClassDef, table: python_reader.Bindings) -> dict[str, bool] | None:
    """Return the keywords of the decorator dataclass of a class with the constant True or False.

    A class with no decorator dataclass gives None.
    """
    for decorator in node.decorator_list:
        call = decorator if isinstance(decorator, ast.Call) else None
        if python_reader.dotted(call.func if call is not None else decorator, table) != DATACLASS:
            continue
        keywords = call.keywords if call is not None else []
        return {
            keyword.arg: keyword.value.value
            for keyword in keywords
            if keyword.arg is not None
            and isinstance(keyword.value, ast.Constant)
            and isinstance(keyword.value.value, bool)
        }
    return None


def is_string_dict(node: ast.expr, table: python_reader.Bindings) -> bool:
    """Return True when a type is or holds dict[str, str], dict[str, object] or dict[str, Any], also in a string."""
    for part in ast.walk(node):
        if isinstance(part, ast.Constant) and isinstance(part.value, str):
            try:
                inner = ast.parse(part.value, mode="eval").body
            except SyntaxError:
                continue
            if inner is not part and is_string_dict(inner, table):
                return True
        if not isinstance(part, ast.Subscript) or python_reader.dotted(part.value, table) not in DICTIONARIES:
            continue
        arguments = part.slice.elts if isinstance(part.slice, ast.Tuple) else []
        if (
            len(arguments) == 2
            and python_reader.dotted(arguments[0], table) == STRING
            and python_reader.dotted(arguments[1], table) in REFUSED_VALUES
        ):
            return True
    return False


def is_public(name: str) -> bool:
    """Return True when a name is public: it does not start with "_", or it starts and ends with "__"."""
    return not name.startswith("_") or (name.startswith("__") and name.endswith("__"))


def signature_types(node: ast.FunctionDef | ast.AsyncFunctionDef) -> list[ast.expr]:
    """Return each type of the parameters and of the return value of a function."""
    arguments = node.args
    parameters = [*arguments.posonlyargs, *arguments.args, *arguments.kwonlyargs]
    parameters += [item for item in (arguments.vararg, arguments.kwarg) if item is not None]
    types = [parameter.annotation for parameter in parameters if parameter.annotation is not None]
    return types + ([node.returns] if node.returns is not None else [])


def walk_scope(
    item: package_scope.PythonFile,
    body: list[ast.stmt],
    prefix: str,
    is_open: bool,
    in_class: bool,
    table: python_reader.Bindings,
    found: list[check_report.Finding],
) -> None:
    """Give the findings of the statements of one scope.  `is_open` is True when a function of the scope is public."""
    for node in body:
        if isinstance(node, ast.ClassDef):
            name = f"{prefix}.{node.name}" if prefix else node.name
            keywords = dataclass_keywords(node, table)
            for keyword, kind in REQUIRED_KEYWORDS.items():
                if keywords is not None and keywords.get(keyword) is not True:
                    found.append(
                        check_report.Finding(
                            "error",
                            item.path,
                            node.lineno,
                            RULE,
                            f"the dataclass {name} has no {keyword}=True.  Give "
                            f"the decorator frozen=True and slots=True, or add a ledger row with the reason",
                            kind,
                            name,
                        )
                    )
            walk_scope(item, node.body, name, is_open and is_public(node.name), True, table, found)
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            name = f"{prefix}.{node.name}" if prefix else node.name
            if (
                is_open
                and is_public(node.name)
                and any(is_string_dict(annotation, table) for annotation in signature_types(node))
            ):
                found.append(
                    check_report.Finding(
                        "error",
                        item.path,
                        node.lineno,
                        RULE,
                        f"the public function {name} has a dictionary of strings "
                        f"in its signature.  Give it a record or an enumeration",
                        "string-dict",
                        name,
                    )
                )
            walk_scope(item, node.body, name, False, False, table, found)
        elif (
            in_class
            and isinstance(node, ast.AnnAssign)
            and isinstance(node.target, ast.Name)
            and is_string_dict(node.annotation, table)
        ):
            name = f"{prefix}.{node.target.id}"
            found.append(
                check_report.Finding(
                    "error",
                    item.path,
                    node.lineno,
                    RULE,
                    f"the field {name} is a dictionary of strings.  Give it a record or an enumeration",
                    "string-dict",
                    name,
                )
            )
        else:
            for child in ast.iter_child_nodes(node):
                if isinstance(child, (ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
                    walk_scope(item, [child], prefix, is_open, in_class, table, found)


def file_findings(item: package_scope.PythonFile) -> list[check_report.Finding]:
    """Give the findings of one file."""
    found: list[check_report.Finding] = []
    walk_scope(item, item.parsed.tree.body, "", True, False, python_reader.bindings(item.parsed), found)
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each dataclass that is not frozen with slots, and each public dictionary of strings."""
    return package_scope.each_file(context, RULE, file_findings)
