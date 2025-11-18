using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Themis.AqlQueryBuilder.Infrastructure;
using Themis.AqlQueryBuilder.Models;

namespace Themis.AqlQueryBuilder.Services;

/// <summary>
/// Service interface for executing AQL queries
/// </summary>
public interface IAqlQueryService
{
    /// <summary>
    /// Executes an AQL query and returns the result
    /// </summary>
    Task<Result<string>> ExecuteQueryAsync(string aql, CancellationToken ct = default);
    
    /// <summary>
    /// Tests the connection to the ThemisDB server
    /// </summary>
    Task<Result> TestConnectionAsync(CancellationToken ct = default);
}

/// <summary>
/// Service interface for managing schema information
/// </summary>
public interface ISchemaService
{
    /// <summary>
    /// Loads schema collections from the server
    /// </summary>
    Task<Result<IEnumerable<SchemaCollection>>> LoadSchemaAsync(CancellationToken ct = default);
    
    /// <summary>
    /// Gets a specific collection by name
    /// </summary>
    Result<SchemaCollection?> GetCollectionByName(string name);
}

/// <summary>
/// Service interface for managing query history
/// </summary>
public interface IQueryHistoryService
{
    /// <summary>
    /// Saves a query to history
    /// </summary>
    Result SaveQuery(string name, string aql, QueryType queryType);
    
    /// <summary>
    /// Loads all saved queries
    /// </summary>
    Result<IEnumerable<SavedQuery>> LoadQueries();
    
    /// <summary>
    /// Deletes a saved query
    /// </summary>
    Result DeleteQuery(string id);
    
    /// <summary>
    /// Marks a query as favorite
    /// </summary>
    Result ToggleFavorite(string id);
}

/// <summary>
/// Represents a saved query
/// </summary>
public class SavedQuery
{
    public string Id { get; set; } = string.Empty;
    public string Name { get; set; } = string.Empty;
    public string Aql { get; set; } = string.Empty;
    public QueryType QueryType { get; set; }
    public System.DateTime SavedAt { get; set; }
    public bool IsFavorite { get; set; }
}
