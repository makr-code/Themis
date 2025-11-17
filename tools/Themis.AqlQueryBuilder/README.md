# Themis AQL Query Builder

## Overview

The **Themis AQL Query Builder** is a visual query editor for constructing AQL (Advanced Query Language) queries for ThemisDB. It provides an intuitive interface for building complex queries without manually writing AQL syntax, with support for multiple connection methods.

## Features

### Phase 1 ✅ Completed

- **Schema Explorer**: Browse collections and fields with type indicators
- **Visual Query Construction**: Build queries using forms and dropdowns
- **Multi-Table Queries**: Implicit JOINs via multiple FOR clauses
- **Real-time Query Preview**: See the generated AQL query as you build it
- **Query Execution**: Execute queries directly against Themis
- **Clause Support**:
  - FOR clauses (collection iteration, implicit JOINs)
  - LET clauses (variable binding)
  - FILTER clauses (conditions with visual operators)
  - SORT clauses (ordering)
  - LIMIT clause (pagination with offset)
  - RETURN clause (result projection)
- **Connection Options**:
  - HTTP/HTTPS REST API
  - TCP Socket (planned)
  - UDP (planned)
  - Direct C# API (planned)
  - Direct C++ API (planned)
- **Modern WPF UI**: Clean, responsive interface with color-coded types

## Connection Methods

The query builder supports multiple ways to connect to ThemisDB:

### 1. HTTP/HTTPS REST API (Default)
```
Server: http://localhost:8080
Endpoint: POST /api/query/aql
```

### 2. TCP Socket Connection (Coming Soon)
```
Host: localhost
Port: 8080
Protocol: Binary
```

### 3. UDP Connection (Coming Soon)
```
Host: localhost
Port: 8080
Protocol: Datagram
```

### 4. Direct C# API (Coming Soon)
```
Type: In-process
Requires: Themis .NET library
```

### 5. Direct C++ API (Coming Soon)
```
Type: Native interop (P/Invoke)
Requires: Themis native library
```

## Prerequisites

- .NET 8 SDK
- Visual Studio 2022 or VS Code with C# Dev Kit
- Running ThemisDB server (for HTTP/Socket/UDP modes)
- OR Themis libraries (for Direct API modes)

## Installation

```powershell
cd tools/Themis.AqlQueryBuilder
dotnet restore
dotnet build
```

## Running

```powershell
cd tools/Themis.AqlQueryBuilder
dotnet run
```

Or open the solution in Visual Studio and press F5.

## Usage

### Building a Query

1. **Browse Schema**: Use the Schema Explorer panel to see available collections and fields

2. **Add FOR Clause**: Click "+ Add FOR Clause" to specify which collection to query
   - Enter a variable name (e.g., `u` for users)
   - Select or enter a collection name (e.g., `users`)

2. **Add FILTER Clause**: Click "+ Add FILTER Clause" to add conditions
   - Set the left operand (e.g., `u.age`)
   - Choose an operator (==, !=, >, <, etc.)
   - Set the right operand (e.g., `18`)

3. **Add SORT Clause**: Click "+ Add SORT Clause" to order results
   - Enter the field to sort by (e.g., `u.name`)
   - Choose ASC or DESC

4. **Set LIMIT**: Specify offset and count for pagination
   - Offset: number of results to skip
   - Count: maximum number of results to return

5. **Configure RETURN**: Specify what to return
   - Whole document: returns the entire document
   - Custom object: returns specific fields
   - Custom expression: write your own return expression

6. **Update Preview**: Click "🔄 Update Query Preview" to see the generated AQL

7. **Execute**: Click "Execute" to run the query against the server

### Example Query

The default sample query generates:

```aql
FOR u IN users
  FILTER u.age > 18
  SORT u.name ASC
  LIMIT 10
  RETURN u
```

This query:
- Iterates over the `users` collection
- Filters for users older than 18
- Sorts by name ascending
- Limits to 10 results
- Returns the entire user document

## Architecture

### Design Pattern: MVVM (Model-View-ViewModel)

- **Models** (`Models/`): Data structures representing AQL query components
  - `AqlQueryModel`: Complete query model
  - `ForClause`, `LetClause`, `FilterClause`, etc.: Individual clause models

- **ViewModels** (`ViewModels/`): Business logic and UI state management
  - `MainViewModel`: Main application logic using CommunityToolkit.Mvvm
  - ObservableCollections for dynamic UI updates
  - RelayCommands for user actions

- **Views** (`Views/`): XAML UI components
  - `MainWindow.xaml`: Main application window
  - Data binding to ViewModel properties

### Key Dependencies

- **CommunityToolkit.Mvvm**: Modern MVVM framework
- **Themis.AdminTools.Shared**: Shared API client and models
- **System.Text.Json**: JSON serialization for API communication

### OOP Best Practices

1. **Separation of Concerns**: Models, ViewModels, and Views are clearly separated
2. **Single Responsibility**: Each class has a focused purpose
3. **Encapsulation**: Private fields with public properties
4. **Composition**: Query model composed of clause objects
5. **Dependency Injection**: ViewModel receives dependencies (planned)
6. **SOLID Principles**: Followed throughout the codebase

## API Requirements

The Themis server must provide:

### POST /api/query/aql

Request body:
```json
{
  "query": "FOR u IN users FILTER u.age > 18 RETURN u",
  "explain": false
}
```

Response:
```json
{
  "results": [...],
  "count": 42,
  "executionTime": "15ms"
}
```

## Configuration

The server URL can be changed in the toolbar. Default is `http://localhost:8080`.

## Future Enhancements

- [ ] Save/load queries from files
- [ ] Query history
- [ ] Syntax highlighting in query preview
- [ ] Auto-completion for collection names and fields
- [ ] COLLECT clause builder (aggregations)
- [ ] Graph traversal query builder
- [ ] Query performance analysis
- [ ] Export results to CSV/JSON
- [ ] Dark theme support

## License

See main project license.
