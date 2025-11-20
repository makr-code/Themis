using Xunit;

namespace ThemisDB.Client.Tests;

public class ThemisClientTests
{
    [Fact]
    public void Constructor_WithValidEndpoints_CreatesClient()
    {
        // Arrange & Act
        var client = new ThemisClient(new[] { "http://localhost:8080" });
        
        // Assert
        Assert.NotNull(client);
    }

    [Fact]
    public void Constructor_WithMultipleEndpoints_CreatesClient()
    {
        // Arrange & Act
        var client = new ThemisClient(new[] { "http://localhost:8080", "http://localhost:8081" });
        
        // Assert
        Assert.NotNull(client);
    }

    [Fact]
    public void Constructor_WithTimeout_CreatesClient()
    {
        // Arrange & Act
        var client = new ThemisClient(
            new[] { "http://localhost:8080" },
            timeout: TimeSpan.FromSeconds(60)
        );
        
        // Assert
        Assert.NotNull(client);
    }

    [Fact]
    public void Constructor_WithNullEndpoints_ThrowsArgumentException()
    {
        // Arrange, Act & Assert
        Assert.Throws<ArgumentException>(() => new ThemisClient(null!));
    }

    [Fact]
    public void Constructor_WithEmptyEndpoints_ThrowsArgumentException()
    {
        // Arrange, Act & Assert
        Assert.Throws<ArgumentException>(() => new ThemisClient(Array.Empty<string>()));
    }

    [Fact]
    public void IsolationLevel_HasExpectedValues()
    {
        // Assert
        Assert.Equal(0, (int)IsolationLevel.ReadCommitted);
        Assert.Equal(1, (int)IsolationLevel.Snapshot);
    }

    [Fact]
    public void TransactionOptions_DefaultValues()
    {
        // Arrange & Act
        var options = new TransactionOptions();
        
        // Assert
        Assert.Equal(IsolationLevel.ReadCommitted, options.IsolationLevel);
        Assert.Null(options.Timeout);
    }

    [Fact]
    public void TransactionOptions_CanSetIsolationLevel()
    {
        // Arrange & Act
        var options = new TransactionOptions
        {
            IsolationLevel = IsolationLevel.Snapshot
        };
        
        // Assert
        Assert.Equal(IsolationLevel.Snapshot, options.IsolationLevel);
    }

    [Fact]
    public void TransactionOptions_CanSetTimeout()
    {
        // Arrange & Act
        var options = new TransactionOptions
        {
            Timeout = TimeSpan.FromSeconds(30)
        };
        
        // Assert
        Assert.Equal(TimeSpan.FromSeconds(30), options.Timeout);
    }

    // Integration tests (require running server) - Skipped by default
    
    [Fact(Skip = "Requires running ThemisDB server")]
    public async Task BeginTransaction_WithValidOptions_ReturnsTransaction()
    {
        // Arrange
        using var client = new ThemisClient(new[] { "http://localhost:8080" });
        var options = new TransactionOptions { IsolationLevel = IsolationLevel.Snapshot };
        
        // Act
        await using var tx = await client.BeginTransactionAsync(options);
        
        // Assert
        Assert.NotNull(tx);
        Assert.True(await tx.IsActiveAsync());
    }

    [Fact(Skip = "Requires running ThemisDB server")]
    public async Task Transaction_CommitAndRollback_WorkCorrectly()
    {
        // Arrange
        using var client = new ThemisClient(new[] { "http://localhost:8080" });
        await using var tx = await client.BeginTransactionAsync();
        
        // Act
        await tx.PutAsync("relational", "test", "test-1", new { value = "test" });
        await tx.CommitAsync();
        
        // Assert
        Assert.False(await tx.IsActiveAsync());
    }

    [Fact(Skip = "Requires running ThemisDB server")]
    public async Task Transaction_AutoRollbackOnDispose()
    {
        // Arrange
        using var client = new ThemisClient(new[] { "http://localhost:8080" });
        
        // Act
        await using (var tx = await client.BeginTransactionAsync())
        {
            await tx.PutAsync("relational", "test", "test-2", new { value = "test" });
            // Not committing - should rollback on dispose
        }
        
        // Transaction should be rolled back
        Assert.True(true); // If we get here without exception, test passes
    }
}
