using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Themis.AqlQueryBuilder.Infrastructure;
using Themis.AqlQueryBuilder.Models;

namespace Themis.AqlQueryBuilder.Services;

/// <summary>
/// Implementation of Query History Service using local file storage
/// No external dependencies - uses built-in System.Text.Json
/// </summary>
public class QueryHistoryService : IQueryHistoryService
{
    private readonly string _historyFilePath;
    private List<SavedQuery> _queries = new();

    public QueryHistoryService()
    {
        var appDataFolder = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ThemisDB",
            "AqlQueryBuilder"
        );
        
        Directory.CreateDirectory(appDataFolder);
        _historyFilePath = Path.Combine(appDataFolder, "query_history.json");
        
        LoadFromFile();
    }

    public Result SaveQuery(string name, string aql, QueryType queryType)
    {
        try
        {
            var query = new SavedQuery
            {
                Id = Guid.NewGuid().ToString(),
                Name = name,
                Aql = aql,
                QueryType = queryType,
                SavedAt = DateTime.Now,
                IsFavorite = false
            };

            _queries.Add(query);
            return SaveToFile();
        }
        catch (Exception ex)
        {
            return Result.Failure($"Failed to save query: {ex.Message}");
        }
    }

    public Result<IEnumerable<SavedQuery>> LoadQueries()
    {
        try
        {
            return Result.Success<IEnumerable<SavedQuery>>(_queries.OrderByDescending(q => q.SavedAt));
        }
        catch (Exception ex)
        {
            return Result.Failure<IEnumerable<SavedQuery>>($"Failed to load queries: {ex.Message}");
        }
    }

    public Result DeleteQuery(string id)
    {
        try
        {
            var query = _queries.FirstOrDefault(q => q.Id == id);
            if (query == null)
            {
                return Result.Failure("Query not found");
            }

            _queries.Remove(query);
            return SaveToFile();
        }
        catch (Exception ex)
        {
            return Result.Failure($"Failed to delete query: {ex.Message}");
        }
    }

    public Result ToggleFavorite(string id)
    {
        try
        {
            var query = _queries.FirstOrDefault(q => q.Id == id);
            if (query == null)
            {
                return Result.Failure("Query not found");
            }

            query.IsFavorite = !query.IsFavorite;
            return SaveToFile();
        }
        catch (Exception ex)
        {
            return Result.Failure($"Failed to toggle favorite: {ex.Message}");
        }
    }

    private void LoadFromFile()
    {
        try
        {
            if (File.Exists(_historyFilePath))
            {
                var json = File.ReadAllText(_historyFilePath);
                _queries = JsonSerializer.Deserialize<List<SavedQuery>>(json) ?? new List<SavedQuery>();
            }
        }
        catch
        {
            // If file is corrupt or doesn't exist, start with empty list
            _queries = new List<SavedQuery>();
        }
    }

    private Result SaveToFile()
    {
        try
        {
            var options = new JsonSerializerOptions
            {
                WriteIndented = true
            };
            var json = JsonSerializer.Serialize(_queries, options);
            File.WriteAllText(_historyFilePath, json);
            return Result.Success();
        }
        catch (Exception ex)
        {
            return Result.Failure($"Failed to save to file: {ex.Message}");
        }
    }
}
