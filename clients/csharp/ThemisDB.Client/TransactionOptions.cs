namespace ThemisDB.Client;

/// <summary>
/// Options for configuring a transaction
/// </summary>
public class TransactionOptions
{
    /// <summary>
    /// Gets or sets the isolation level for the transaction
    /// </summary>
    public IsolationLevel IsolationLevel { get; set; } = IsolationLevel.ReadCommitted;
    
    /// <summary>
    /// Gets or sets the timeout for the transaction
    /// </summary>
    public TimeSpan? Timeout { get; set; }
}
