namespace WebScene.Css;

/// <summary>
/// Used-width distribution shared by native and portable table layout. The
/// caller resolves each column's fixed-layout base assignment first; this
/// helper only assigns positive excess according to CSS Tables 3 section
/// 3.9.3.2.
/// </summary>
public static class CssTableWidthDistribution
{
    public static void DistributeFixedExcess(
        double[] columns,
        IReadOnlyList<CssTableColumnWidthConstraint> constraints,
        double usedWidth)
    {
        ArgumentNullException.ThrowIfNull(columns);
        ArgumentNullException.ThrowIfNull(constraints);
        if (columns.Length != constraints.Count)
        {
            throw new ArgumentException("Every table column must have one width constraint.", nameof(constraints));
        }
        if (!double.IsFinite(usedWidth) || usedWidth < 0 || columns.Length == 0)
        {
            return;
        }

        var excess = usedWidth - columns.Sum();
        if (excess <= 0)
        {
            return;
        }

        var recipients = SelectRecipients(constraints, CssTableColumnWidthKind.Auto, requirePositiveWeight: false);
        if (recipients.Count == 0)
        {
            recipients = SelectRecipients(constraints, CssTableColumnWidthKind.Length, requirePositiveWeight: true);
        }
        if (recipients.Count == 0)
        {
            recipients = SelectRecipients(constraints, CssTableColumnWidthKind.Percent, requirePositiveWeight: true);
        }
        if (recipients.Count == 0)
        {
            recipients = Enumerable.Range(0, columns.Length)
                .Where(index => constraints[index].Kind == CssTableColumnWidthKind.Zero)
                .ToArray();
        }
        if (recipients.Count == 0)
        {
            recipients = Enumerable.Range(0, columns.Length).ToArray();
        }

        var proportional = recipients.All(index => constraints[index].Weight > 0);
        var weight = proportional
            ? recipients.Sum(index => constraints[index].Weight)
            : recipients.Count;
        var distributed = 0d;
        for (var recipient = 0; recipient < recipients.Count; recipient++)
        {
            var column = recipients[recipient];
            var increase = recipient == recipients.Count - 1
                ? excess - distributed
                : excess * (proportional ? constraints[column].Weight : 1d) / weight;
            columns[column] += increase;
            distributed += increase;
        }
    }

    private static IReadOnlyList<int> SelectRecipients(
        IReadOnlyList<CssTableColumnWidthConstraint> constraints,
        CssTableColumnWidthKind kind,
        bool requirePositiveWeight)
        => Enumerable.Range(0, constraints.Count)
            .Where(index => constraints[index].Kind == kind
                            && (!requirePositiveWeight || constraints[index].Weight > 0))
            .ToArray();
}
