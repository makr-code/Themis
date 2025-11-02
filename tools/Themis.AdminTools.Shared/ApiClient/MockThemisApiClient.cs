using Themis.AdminTools.Shared.Models;

namespace Themis.AdminTools.Shared.ApiClient;

/// <summary>
/// Mock-Implementation des API-Clients für Tests ohne laufenden Server.
/// </summary>
public class MockThemisApiClient : IDisposable
{
    private readonly Random _random = new();

    public Task<ApiResponse<AuditLogResponse>> GetAuditLogsAsync(
        AuditLogFilter filter,
        CancellationToken cancellationToken = default)
    {
        // Simuliere Netzwerk-Latenz
        Thread.Sleep(500);

        var entries = GenerateMockEntries(filter);
        var response = new AuditLogResponse
        {
            Entries = entries,
            TotalCount = 1234,
            Page = filter.Page,
            PageSize = filter.PageSize,
            HasMore = filter.Page * filter.PageSize < 1234
        };

        return Task.FromResult(new ApiResponse<AuditLogResponse>
        {
            Success = true,
            Data = response,
            StatusCode = 200
        });
    }

    public Task<ApiResponse<byte[]>> ExportAuditLogsToCsvAsync(
        AuditLogFilter filter,
        CancellationToken cancellationToken = default)
    {
        Thread.Sleep(1000);

        var csv = "Id,Timestamp,User,Action,EntityType,EntityId,OldValue,NewValue,Success,IpAddress,ErrorMessage\n";
        var entries = GenerateMockEntries(filter);
        
        foreach (var entry in entries)
        {
            csv += $"{entry.Id}," +
                   $"{entry.Timestamp:yyyy-MM-dd HH:mm:ss}," +
                   $"{entry.User}," +
                   $"{entry.Action}," +
                   $"{entry.EntityType}," +
                   $"{entry.EntityId}," +
                   $"\"{entry.OldValue}\"," +
                   $"\"{entry.NewValue}\"," +
                   $"{entry.Success}," +
                   $"{entry.IpAddress}," +
                   $"\"{entry.ErrorMessage}\"\n";
        }

        var bytes = System.Text.Encoding.UTF8.GetBytes(csv);
        return Task.FromResult(new ApiResponse<byte[]>
        {
            Success = true,
            Data = bytes,
            StatusCode = 200
        });
    }

    private List<AuditLogEntry> GenerateMockEntries(AuditLogFilter filter)
    {
        var entries = new List<AuditLogEntry>();
        var users = new[] { "admin", "user1", "user2", "system", "api_client" };
        var actions = new[] { "CREATE", "UPDATE", "DELETE", "READ", "QUERY" };
        var entityTypes = new[] { "Document", "Index", "Collection", "User", "Config" };

        for (int i = 0; i < filter.PageSize && i < 100; i++)
        {
            var timestamp = DateTime.Now.AddMinutes(-_random.Next(0, 10080)); // Letzte 7 Tage
            var user = users[_random.Next(users.Length)];
            var action = actions[_random.Next(actions.Length)];
            var entityType = entityTypes[_random.Next(entityTypes.Length)];
            var success = _random.Next(100) > 5; // 95% Erfolgsrate

            entries.Add(new AuditLogEntry
            {
                Id = (filter.Page - 1) * filter.PageSize + i + 1,
                Timestamp = timestamp,
                User = user,
                Action = action,
                EntityType = entityType,
                EntityId = Guid.NewGuid().ToString(),
                OldValue = action == "UPDATE" ? $"old_value_{i}" : null,
                NewValue = action != "DELETE" ? $"new_value_{i}" : null,
                Success = success,
                IpAddress = $"192.168.1.{_random.Next(1, 255)}",
                SessionId = Guid.NewGuid().ToString(),
                ErrorMessage = !success ? "Simulated error" : null
            });
        }

        // Filter anwenden
        if (!string.IsNullOrEmpty(filter.User))
            entries = entries.Where(e => e.User.Contains(filter.User, StringComparison.OrdinalIgnoreCase)).ToList();
        
        if (!string.IsNullOrEmpty(filter.Action))
            entries = entries.Where(e => e.Action.Contains(filter.Action, StringComparison.OrdinalIgnoreCase)).ToList();
        
        if (!string.IsNullOrEmpty(filter.EntityType))
            entries = entries.Where(e => e.EntityType.Contains(filter.EntityType, StringComparison.OrdinalIgnoreCase)).ToList();
        
        if (filter.SuccessOnly == true)
            entries = entries.Where(e => e.Success).ToList();

        return entries.OrderByDescending(e => e.Timestamp).ToList();
    }

    public void Dispose()
    {
        GC.SuppressFinalize(this);
    }
}
