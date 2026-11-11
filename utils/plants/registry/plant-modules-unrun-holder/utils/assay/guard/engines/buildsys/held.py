"""A planted witness of the held rules.  The judge of loop-gauge calls a different function of the holder."""


def gauge_findings(rule, context):
    """Give the findings of the planted records."""
    from assay.loop import gauge

    return [gauge.other_record(record) for record in context]


HOLDERS = {"loop-gauge": gauge_findings}
