"""A planted witness of the held rules.  Each judge calls the function of its holder."""

from assay.loop.history import judge_line


def history_findings(rule, context):
    """Give the findings of the planted lines."""
    return [judge_line(line) for line in context]


def gauge_findings(rule, context):
    """Give the findings of the planted records."""
    from assay.loop import gauge

    return [gauge.judged_record(record) for record in context]


HOLDERS: dict[str, object] = {"loop-history": history_findings, "loop-gauge": gauge_findings}
