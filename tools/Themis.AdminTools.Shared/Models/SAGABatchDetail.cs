using System.Text.Json.Serialization;

namespace Themis.AdminTools.Shared.Models;

/// <summary>
/// Detailed information about a SAGA batch including all steps.
/// </summary>
public class SAGABatchDetail
{
    /// <summary>
    /// Unique batch identifier.
    /// </summary>
    [JsonPropertyName("batch_id")]
    public string BatchId { get; set; } = string.Empty;

    /// <summary>
    /// ISO 8601 timestamp when the batch was created.
    /// </summary>
    [JsonPropertyName("timestamp")]
    public string Timestamp { get; set; } = string.Empty;

    /// <summary>
    /// Number of SAGA steps in this batch.
    /// </summary>
    [JsonPropertyName("entry_count")]
    public int EntryCount { get; set; }

    /// <summary>
    /// Base64-encoded PKI signature.
    /// </summary>
    [JsonPropertyName("signature")]
    public string Signature { get; set; } = string.Empty;

    /// <summary>
    /// SHA-256 hash of the batch content (hex encoded).
    /// </summary>
    [JsonPropertyName("hash")]
    public string Hash { get; set; } = string.Empty;

    /// <summary>
    /// Array of SAGA steps in the batch.
    /// </summary>
    [JsonPropertyName("steps")]
    public List<SAGAStep> Steps { get; set; } = new();
}

/// <summary>
/// Represents a single SAGA step.
/// </summary>
public class SAGAStep
{
    /// <summary>
    /// ISO 8601 timestamp of the step execution.
    /// </summary>
    [JsonPropertyName("timestamp")]
    public string Timestamp { get; set; } = string.Empty;

    /// <summary>
    /// Unique SAGA identifier.
    /// </summary>
    [JsonPropertyName("saga_id")]
    public string SagaId { get; set; } = string.Empty;

    /// <summary>
    /// Name of the SAGA step (e.g., "ReserveInventory").
    /// </summary>
    [JsonPropertyName("step_name")]
    public string StepName { get; set; } = string.Empty;

    /// <summary>
    /// Step execution status (e.g., "COMPLETED", "FAILED", "COMPENSATED").
    /// </summary>
    [JsonPropertyName("status")]
    public string Status { get; set; } = string.Empty;

    /// <summary>
    /// Correlation ID for distributed tracing.
    /// </summary>
    [JsonPropertyName("correlation_id")]
    public string? CorrelationId { get; set; }

    /// <summary>
    /// Additional metadata as JSON string.
    /// </summary>
    [JsonPropertyName("metadata")]
    public string? Metadata { get; set; }

    /// <summary>
    /// Formatted timestamp for display.
    /// </summary>
    public string TimestampFormatted
    {
        get
        {
            if (DateTime.TryParse(Timestamp, out var dt))
            {
                return dt.ToLocalTime().ToString("HH:mm:ss.fff");
            }
            return Timestamp;
        }
    }
}
