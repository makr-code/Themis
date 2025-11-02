namespace Themis.AdminTools.Shared.Models;

public record AuditLogEntry
{
    public long Id { get; init; }
    public DateTime Timestamp { get; init; }
    public string User { get; init; } = string.Empty;
    public string Action { get; init; } = string.Empty;
    public string EntityType { get; init; } = string.Empty;
    public string EntityId { get; init; } = string.Empty;
    public string? OldValue { get; init; }
    public string? NewValue { get; init; }
    public string? IpAddress { get; init; }
    public string? SessionId { get; init; }
    public bool Success { get; init; }
    public string? ErrorMessage { get; init; }
}

public record AuditLogFilter
{
    public DateTime? StartDate { get; init; }
    public DateTime? EndDate { get; init; }
    public string? User { get; init; }
    public string? Action { get; init; }
    public string? EntityType { get; init; }
    public string? EntityId { get; init; }
    public bool? SuccessOnly { get; init; }
    public int Page { get; init; } = 1;
    public int PageSize { get; init; } = 100;
}

public record AuditLogResponse
{
    public List<AuditLogEntry> Entries { get; init; } = new();
    public int TotalCount { get; init; }
    public int Page { get; init; }
    public int PageSize { get; init; }
    public bool HasMore { get; init; }
}
