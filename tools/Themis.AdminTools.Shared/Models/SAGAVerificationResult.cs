using System.Text.Json.Serialization;

namespace Themis.AdminTools.Shared.Models;

/// <summary>
/// Result of SAGA batch signature verification.
/// </summary>
public class SAGAVerificationResult
{
    /// <summary>
    /// Batch ID that was verified.
    /// </summary>
    [JsonPropertyName("batch_id")]
    public string BatchId { get; set; } = string.Empty;

    /// <summary>
    /// Overall verification result (true if both signature and hash are valid).
    /// </summary>
    [JsonPropertyName("verified")]
    public bool Verified { get; set; }

    /// <summary>
    /// Whether the PKI signature is valid.
    /// </summary>
    [JsonPropertyName("signature_valid")]
    public bool SignatureValid { get; set; }

    /// <summary>
    /// Whether the hash matches the batch content.
    /// </summary>
    [JsonPropertyName("hash_match")]
    public bool HashMatch { get; set; }

    /// <summary>
    /// Human-readable message about the verification result.
    /// </summary>
    [JsonPropertyName("message")]
    public string Message { get; set; } = string.Empty;

    /// <summary>
    /// Display-friendly verification status.
    /// </summary>
    public string StatusDisplay => Verified ? "✓ Verified" : "✗ Failed";

    /// <summary>
    /// Detailed status message.
    /// </summary>
    public string DetailedStatus
    {
        get
        {
            if (Verified)
                return "Batch signature and hash verified successfully.";
            
            var issues = new List<string>();
            if (!SignatureValid)
                issues.Add("Invalid signature");
            if (!HashMatch)
                issues.Add("Hash mismatch");
            
            return $"Verification failed: {string.Join(", ", issues)}";
        }
    }
}
