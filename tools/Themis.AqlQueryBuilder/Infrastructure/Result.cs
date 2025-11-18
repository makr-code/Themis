using System;
using System.Collections.Generic;
using System.Linq;

namespace Themis.AqlQueryBuilder.Infrastructure;

/// <summary>
/// Result pattern for explicit error handling without exceptions
/// Follows railway-oriented programming pattern
/// </summary>
public class Result
{
    public bool IsSuccess { get; }
    public bool IsFailure => !IsSuccess;
    public string Error { get; }

    protected Result(bool isSuccess, string error)
    {
        if (isSuccess && error != string.Empty)
            throw new InvalidOperationException();
        if (!isSuccess && error == string.Empty)
            throw new InvalidOperationException();

        IsSuccess = isSuccess;
        Error = error;
    }

    public static Result Success() => new Result(true, string.Empty);
    public static Result Failure(string error) => new Result(false, error);

    public static Result<T> Success<T>(T value) => new Result<T>(value, true, string.Empty);
    public static Result<T> Failure<T>(string error) => new Result<T>(default!, false, error);
}

/// <summary>
/// Generic result with a value
/// </summary>
public class Result<T> : Result
{
    public T Value { get; }

    protected internal Result(T value, bool isSuccess, string error)
        : base(isSuccess, error)
    {
        Value = value;
    }
}

/// <summary>
/// Result with multiple errors
/// </summary>
public class ValidationResult : Result
{
    public IReadOnlyList<string> Errors { get; }

    protected ValidationResult(bool isSuccess, IEnumerable<string> errors)
        : base(isSuccess, errors.FirstOrDefault() ?? string.Empty)
    {
        Errors = errors.ToList().AsReadOnly();
    }

    public static ValidationResult Success() => new ValidationResult(true, Array.Empty<string>());
    public static ValidationResult Failure(params string[] errors) => new ValidationResult(false, errors);
    public static ValidationResult Failure(IEnumerable<string> errors) => new ValidationResult(false, errors);
}
