namespace Themis.AdminTools.Shared.Models;

public record ComplianceReport
{
    public string Id { get; init; } = string.Empty;
    public string Type { get; init; } = string.Empty; // DSGVO/SOX/HIPAA/ISO27001/PCI-DSS
    public DateTime CreatedAt { get; init; }
    public DateTime GeneratedAt { get; init; }
    public string? CreatedBy { get; init; }
    public DateTime PeriodStart { get; init; }
    public DateTime PeriodEnd { get; init; }
    public string Status { get; init; } = "Ready";
    public int RecordCount { get; init; }
    public string? Description { get; init; }
}

public record ComplianceReportListResponse
{
    public List<ComplianceReport> Items { get; init; } = new();
    public int Total { get; init; }
}
