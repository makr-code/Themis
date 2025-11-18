namespace Themis.AqlQueryBuilder.Models;

/// <summary>
/// Represents a complete AQL query with all its components
/// </summary>
public class AqlQueryModel
{
    public List<ForClause> ForClauses { get; set; } = new();
    public List<LetClause> LetClauses { get; set; } = new();
    public List<FilterClause> FilterClauses { get; set; } = new();
    public List<SortClause> SortClauses { get; set; } = new();
    public LimitClause Limit { get; set; } = new LimitClause { Count = 100 };
    public ReturnClause Return { get; set; } = new ReturnClause();
    public CollectClause? Collect { get; set; }

    /// <summary>
    /// Generates the AQL query string from the model
    /// </summary>
    public string ToAqlString()
    {
        var query = new List<string>();

        // FOR clauses
        foreach (var forClause in ForClauses)
        {
            query.Add($"FOR {forClause.Variable} IN {forClause.Collection}");
        }

        // LET clauses
        foreach (var letClause in LetClauses)
        {
            query.Add($"  LET {letClause.Variable} = {letClause.Expression}");
        }

        // FILTER clauses with AND/OR support (Phase 1.5)
        if (FilterClauses.Any())
        {
            var filterExpressions = new List<string>();
            for (int i = 0; i < FilterClauses.Count; i++)
            {
                var filter = FilterClauses[i];
                var condition = filter.Condition;
                
                if (i > 0)
                {
                    // Add logical operator before condition
                    var logicalOp = filter.LogicalOp switch
                    {
                        LogicalOperator.And => "AND",
                        LogicalOperator.Or => "OR",
                        LogicalOperator.Not => "NOT",
                        _ => "AND"
                    };
                    
                    // For simple case, combine in one FILTER statement
                    if (!filter.IsGrouped)
                    {
                        filterExpressions.Add($"{logicalOp} {condition}");
                    }
                    else
                    {
                        // For grouped filters, create separate FILTER statements
                        query.Add($"  FILTER {condition}");
                    }
                }
                else
                {
                    filterExpressions.Add(condition);
                }
            }
            
            if (filterExpressions.Any())
            {
                query.Add($"  FILTER {string.Join(" ", filterExpressions)}");
            }
        }

        // SORT clauses
        if (SortClauses.Any())
        {
            var sortExpressions = SortClauses.Select(s => 
                $"{s.Expression} {(s.Ascending ? "ASC" : "DESC")}");
            query.Add($"  SORT {string.Join(", ", sortExpressions)}");
        }

        // COLLECT clause
        if (Collect != null)
        {
            var collectParts = new List<string>();
            if (Collect.GroupByFields.Any())
            {
                collectParts.Add(string.Join(", ", Collect.GroupByFields.Select(f => 
                    $"{f.Variable} = {f.Expression}")));
            }
            if (Collect.AggregateFields.Any())
            {
                collectParts.Add("AGGREGATE " + string.Join(", ", Collect.AggregateFields.Select(a => 
                    $"{a.Variable} = {a.Function}({a.Expression})")));
            }
            query.Add($"  COLLECT {string.Join(" ", collectParts)}");
        }

        // LIMIT clause
        if (Limit != null && Limit.Count > 0)
        {
            if (Limit.Offset > 0)
                query.Add($"  LIMIT {Limit.Offset}, {Limit.Count}");
            else
                query.Add($"  LIMIT {Limit.Count}");
        }

        // RETURN clause
        if (Return != null && !string.IsNullOrWhiteSpace(Return.Expression))
        {
            query.Add($"  RETURN {Return.Expression}");
        }

        return string.Join("\n", query);
    }
}

/// <summary>
/// FOR variable IN collection
/// </summary>
public class ForClause
{
    public string Variable { get; set; } = string.Empty;
    public string Collection { get; set; } = string.Empty;
}

/// <summary>
/// LET variable = expression
/// </summary>
public class LetClause
{
    public string Variable { get; set; } = string.Empty;
    public string Expression { get; set; } = string.Empty;
}

/// <summary>
/// FILTER condition
/// </summary>
public class FilterClause
{
    public string Condition { get; set; } = string.Empty;
    public FilterOperator Operator { get; set; } = FilterOperator.Equals;
    public string LeftOperand { get; set; } = string.Empty;
    public string RightOperand { get; set; } = string.Empty;
    
    // For grouped filters (Phase 1.5)
    public LogicalOperator LogicalOp { get; set; } = LogicalOperator.And;
    public bool IsGrouped { get; set; }
    public int GroupLevel { get; set; }

    public void UpdateCondition()
    {
        Condition = Operator switch
        {
            FilterOperator.Equals => $"{LeftOperand} == {RightOperand}",
            FilterOperator.NotEquals => $"{LeftOperand} != {RightOperand}",
            FilterOperator.GreaterThan => $"{LeftOperand} > {RightOperand}",
            FilterOperator.GreaterThanOrEqual => $"{LeftOperand} >= {RightOperand}",
            FilterOperator.LessThan => $"{LeftOperand} < {RightOperand}",
            FilterOperator.LessThanOrEqual => $"{LeftOperand} <= {RightOperand}",
            FilterOperator.In => $"{LeftOperand} IN {RightOperand}",
            FilterOperator.Contains => $"CONTAINS({LeftOperand}, {RightOperand})",
            _ => Condition
        };
    }
}

/// <summary>
/// Logical operators for combining filters
/// </summary>
public enum LogicalOperator
{
    And,
    Or,
    Not
}

/// <summary>
/// Filter operators
/// </summary>
public enum FilterOperator
{
    Equals,
    NotEquals,
    GreaterThan,
    GreaterThanOrEqual,
    LessThan,
    LessThanOrEqual,
    In,
    Contains
}

/// <summary>
/// SORT expression [ASC|DESC]
/// </summary>
public class SortClause
{
    public string Expression { get; set; } = string.Empty;
    public bool Ascending { get; set; } = true;
}

/// <summary>
/// LIMIT [offset,] count
/// </summary>
public class LimitClause
{
    public int Offset { get; set; }
    public int Count { get; set; } = 100;
}

/// <summary>
/// RETURN expression
/// </summary>
public class ReturnClause
{
    public string Expression { get; set; } = string.Empty;
    public ReturnType Type { get; set; } = ReturnType.WholeDocument;
    public List<string> Fields { get; set; } = new();

    public void UpdateExpression()
    {
        Expression = Type switch
        {
            ReturnType.WholeDocument => ForClauses.FirstOrDefault()?.Variable ?? "doc",
            ReturnType.CustomObject when Fields.Any() => 
                "{" + string.Join(", ", Fields.Select(f => $"{f}: {ForClauses.FirstOrDefault()?.Variable ?? "doc"}.{f}")) + "}",
            ReturnType.Custom => Expression,
            _ => Expression
        };
    }

    private List<ForClause> ForClauses { get; set; } = new();

    public void SetForClauses(List<ForClause> forClauses)
    {
        ForClauses = forClauses;
    }
}

/// <summary>
/// Return type options
/// </summary>
public enum ReturnType
{
    WholeDocument,
    CustomObject,
    Custom
}

/// <summary>
/// COLLECT clause for aggregation
/// </summary>
public class CollectClause
{
    public List<GroupByField> GroupByFields { get; set; } = new();
    public List<AggregateField> AggregateFields { get; set; } = new();
}

public class GroupByField
{
    public string Variable { get; set; } = string.Empty;
    public string Expression { get; set; } = string.Empty;
}

public class AggregateField
{
    public string Variable { get; set; } = string.Empty;
    public string Function { get; set; } = "COUNT"; // COUNT, SUM, AVG, MIN, MAX
    public string Expression { get; set; } = "1";
}
