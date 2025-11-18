using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Themis.AqlQueryBuilder.Infrastructure;
using Themis.AqlQueryBuilder.Models;

namespace Themis.AqlQueryBuilder.Services;

/// <summary>
/// Implementation of Schema Service
/// Provides sample schema data (can be extended to load from server)
/// </summary>
public class SchemaService : ISchemaService
{
    private readonly List<SchemaCollection> _collections = new();

    public SchemaService()
    {
        InitializeSampleSchema();
    }

    public Task<Result<IEnumerable<SchemaCollection>>> LoadSchemaAsync(CancellationToken ct = default)
    {
        // In a real implementation, this would load from the server
        // For now, return the sample schema
        return Task.FromResult(Result.Success<IEnumerable<SchemaCollection>>(_collections.AsEnumerable()));
    }

    public Result<SchemaCollection?> GetCollectionByName(string name)
    {
        var collection = _collections.FirstOrDefault(c => c.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
        return Result.Success<SchemaCollection?>(collection);
    }

    private void InitializeSampleSchema()
    {
        // Users collection (Relational)
        _collections.Add(new SchemaCollection
        {
            Name = "users",
            CollectionType = CollectionType.Relational,
            Fields = new List<SchemaField>
            {
                new() { Name = "_id", DataType = "string", IsIndexed = true },
                new() { Name = "name", DataType = "string", IsIndexed = true },
                new() { Name = "email", DataType = "string", IsIndexed = true },
                new() { Name = "age", DataType = "number" },
                new() { Name = "city", DataType = "string" },
                new() { Name = "premium", DataType = "boolean" }
            }
        });

        // Products collection (Relational)
        _collections.Add(new SchemaCollection
        {
            Name = "products",
            CollectionType = CollectionType.Relational,
            Fields = new List<SchemaField>
            {
                new() { Name = "_id", DataType = "string", IsIndexed = true },
                new() { Name = "name", DataType = "string", IsIndexed = true },
                new() { Name = "category", DataType = "string", IsIndexed = true },
                new() { Name = "price", DataType = "number" },
                new() { Name = "description", DataType = "string" },
                new() { Name = "featured", DataType = "boolean" }
            }
        });

        // Stores collection (Geo)
        _collections.Add(new SchemaCollection
        {
            Name = "stores",
            CollectionType = CollectionType.Geo,
            Fields = new List<SchemaField>
            {
                new() { Name = "_id", DataType = "string", IsIndexed = true },
                new() { Name = "name", DataType = "string" },
                new() { Name = "location", DataType = "geo", IsIndexed = true },
                new() { Name = "category", DataType = "string" },
                new() { Name = "type", DataType = "string" }
            }
        });

        // Follows collection (Graph)
        _collections.Add(new SchemaCollection
        {
            Name = "follows",
            CollectionType = CollectionType.Graph,
            Fields = new List<SchemaField>
            {
                new() { Name = "_from", DataType = "string", IsIndexed = true },
                new() { Name = "_to", DataType = "string", IsIndexed = true },
                new() { Name = "_type", DataType = "string", IsIndexed = true },
                new() { Name = "since", DataType = "date" }
            }
        });
    }
}
