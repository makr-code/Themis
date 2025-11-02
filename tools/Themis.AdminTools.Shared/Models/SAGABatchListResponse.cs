using System.Text.Json.Serialization;

namespace Themis.AdminTools.Shared.Models;

/// <summary>
/// Response from GET /api/saga/batches endpoint.
/// </summary>
public class SAGABatchListResponse
{
    /// <summary>
    /// Total number of batches available.
    /// </summary>
    [JsonPropertyName("total_count")]
    public int TotalCount { get; set; }

    /// <summary>
    /// List of batch information.
    /// </summary>
    [JsonPropertyName("batches")]
    public List<SAGABatchInfo> Batches { get; set; } = new();
}

/// <summary>
/// Response from POST /api/saga/flush endpoint.
/// </summary>
public class SAGAFlushResponse
{
    /// <summary>
    /// Status of the flush operation.
    /// </summary>
    [JsonPropertyName("status")]
    public string Status { get; set; } = string.Empty;

    /// <summary>
    /// Human-readable message.
    /// </summary>
    [JsonPropertyName("message")]
    public string Message { get; set; } = string.Empty;

    /// <summary>
    /// ID of the flushed batch (if available).
    /// </summary>
    [JsonPropertyName("batch_id")]
    public string? BatchId { get; set; }
}
