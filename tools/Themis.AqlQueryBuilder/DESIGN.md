# Multi-Model Visual Query Builder - Design Document

## Executive Summary

This document outlines the design for an enhanced Visual Query Builder that supports ThemisDB's multi-model capabilities: **relational**, **graph**, **vector**, and **geo** data.

## Connection Architecture

The query builder supports multiple connection methods to the Themis database:

### Supported Connection Types

1. **HTTP/HTTPS REST API** (Default)
   - Standard REST endpoints
   - JSON request/response
   - Easy to use, works remotely
   - Endpoint: `/api/query/aql`

2. **TCP Socket Connection**
   - Direct socket communication
   - Lower latency than HTTP
   - Binary protocol support
   - Persistent connection

3. **UDP Connection**
   - Lightweight, connectionless
   - Best for high-throughput scenarios
   - Fire-and-forget queries
   - Reduced overhead

4. **Direct C# API**
   - In-process database access
   - Highest performance
   - No network overhead
   - Requires Themis .NET library

5. **Direct C++ API**
   - Native C++ interop via P/Invoke
   - Full access to C++ API
   - Maximum control and performance
   - Requires native Themis library

### Connection Configuration

```csharp
public class ConnectionConfig {
    ConnectionType Type;        // HTTP, Socket, UDP, DirectCSharp, DirectCpp
    string ServerUrl;           // For HTTP/HTTPS
    string Host;                // For Socket/UDP
    int Port;                   // For Socket/UDP
    string DatabasePath;        // For Direct API
    bool UseSsl;                // SSL/TLS support
    string ApiKey;              // Authentication
    int TimeoutSeconds;         // Connection timeout
}
```

## Research Summary

### Analyzed Solutions

1. **Neo4j Bloom/Browser** - Graph query visualization
   - Visual node-edge patterns
   - Drag-drop graph building
   - Pattern matching with Cypher preview

2. **ArcGIS Query Builder** - Spatial query tools
   - Map-centric interface
   - Drawing tools for spatial selection
   - Spatial relationship operators

3. **Redash/Metabase** - Relational query builders
   - Schema browser with drag-drop
   - Visual JOIN builder
   - Filter construction with operators

4. **Pinecone/Weaviate Consoles** - Vector search interfaces
   - Similarity threshold controls
   - Distance metric selection
   - Hybrid filtering (metadata + vectors)

## Proposed Architecture

### Three-Panel Layout

```
┌─────────────────────────────────────────────────────────────┐
│ Toolbar: [Query Type ▼] [Execute] [Save] [Load] [Help]     │
├───────────┬─────────────────────────────┬───────────────────┤
│  Schema   │     Query Builder Canvas    │   Properties      │
│  Explorer │                             │   Panel           │
│           │  [Tab-based by query type]  │                   │
│  📊 Tables│                             │   • Node/Edge     │
│  🕸️ Graphs│  • Relational               │     properties    │
│  📍 Geo   │  • Graph                    │   • Filter        │
│  🔢 Vector│  • Vector                   │     details       │
│           │  • Geo                      │   • Settings      │
│  [+ New]  │  • Hybrid                   │                   │
├───────────┴─────────────────────────────┴───────────────────┤
│  AQL Preview:                                                │
│  FOR u IN users FILTER u.age > 18 RETURN u                  │
├──────────────────────────────────────────────────────────────┤
│  Results Panel (switchable view):                           │
│  • Grid (relational) • Graph • Map • Similarity scores      │
└──────────────────────────────────────────────────────────────┘
```

## Query Type Interfaces

### 1. Relational Query Builder (Enhanced Current)

**Components:**
- **Collection Selector**: Dropdown with autocomplete
- **JOIN Builder**: Visual diagram showing table relationships
- **Filter Builder**: Drag-drop conditions with AND/OR grouping
- **Aggregation Panel**: GROUP BY, HAVING, aggregate functions
- **Sort/Limit Controls**: Multi-column sort, pagination

**Visual JOIN Builder:**
```
[users] ──────┐
   │          │ user_id == orders.user_id
   │        [orders]
   │          │
   └──────────┘
```

### 2. Graph Query Builder (NEW)

**Components:**
- **Graph Canvas**: Drag-drop nodes and edges
- **Node Palette**: Available node types
- **Edge Types**: Relationship types
- **Pattern Builder**: Visual pattern construction
- **Traversal Controls**: Min/max depth, direction

**Example Pattern:**
```
(User) -[FOLLOWS]-> (User) -[LIKES]-> (Product)
  |                    |                 |
  └─ name: "Alice"    └─ city: "NYC"   └─ category: "Books"
```

**AQL Generation:**
```aql
FOR user IN users
  FOR follows IN edges
    FILTER follows._from == user._id AND follows._type == "FOLLOWS"
    FOR followed IN users
      FILTER followed._id == follows._to
      FOR likes IN edges
        FILTER likes._from == followed._id AND likes._type == "LIKES"
        ...
```

### 3. Vector Query Builder (NEW)

**Components:**
- **Reference Input**: Upload file, paste text, or select existing item
- **Embedding Model**: Model selector (if multiple available)
- **Similarity Metric**: Cosine, Euclidean, Dot Product
- **K-NN Settings**: Number of results, threshold
- **Hybrid Filters**: Combine vector search with metadata filters

**Interface:**
```
┌─────────────────────────────────────┐
│ Reference Item:                     │
│ [Upload Image] [Paste Text] [ID]   │
│                                     │
│ Similarity Metric: [Cosine ▼]      │
│ Results: [10 ▼]                     │
│ Min Score: [0.7] ──────────○──── 1.0│
│                                     │
│ Metadata Filters (optional):       │
│ + Add Filter                        │
└─────────────────────────────────────┘
```

**AQL Generation:**
```aql
FOR doc IN documents
  LET similarity = VECTOR_SIMILARITY(doc.embedding, @queryVector, "cosine")
  FILTER similarity > 0.7
  SORT similarity DESC
  LIMIT 10
  RETURN {doc: doc, score: similarity}
```

### 4. Geo Query Builder (NEW)

**Components:**
- **Interactive Map**: Leaflet/OpenLayers integration
- **Drawing Tools**: Point, Line, Polygon, Circle, Rectangle
- **Spatial Operators**: 
  - INTERSECTS, CONTAINS, WITHIN
  - DISTANCE (within radius)
  - BUFFER (expand geometry)
- **Layer Manager**: Toggle data layers
- **Coordinate Systems**: WGS84, Web Mercator, etc.

**Interface:**
```
┌─────────────────────────────────────┐
│ 🗺️ [Map View]                       │
│                                     │
│ Drawing Tools: ⚫ 📏 ▭ ⭕ 📐        │
│                                     │
│ Spatial Query:                      │
│ Find points that [WITHIN ▼]        │
│ [Current Selection]                 │
│                                     │
│ Distance: [100] [meters ▼]         │
└─────────────────────────────────────┘
```

**AQL Generation:**
```aql
FOR location IN locations
  FILTER GEO_DISTANCE(location.coordinates, @centerPoint) < 100
  RETURN location
```

### 5. Hybrid Query Builder (NEW)

**Visual Flow Builder:**
```
[Geo Filter] ──→ [Graph Traversal] ──→ [Vector Similarity] ──→ [Results]
   |                   |                      |
   └─ Within 10km     └─ FOLLOWS friends    └─ Similar content
```

**Use Case Example:**
"Find products similar to this image, sold by stores within 5km, recommended by my friends"

## Implementation Phases

### Phase 1: Enhanced Relational Builder ✅ COMPLETED
- [x] Basic FOR/FILTER/SORT
- [x] Schema Browser (left panel) with collection tree
- [x] Multi-table queries (implicit JOINs via multiple FOR clauses)
- [x] Color-coded data types and field visualization
- [x] Real-time AQL query preview
- [x] Query execution via HTTP REST API
- [x] Connection configuration support
- [ ] Visual JOIN diagram (planned for Phase 1.5)
- [ ] Advanced filter grouping with AND/OR (planned for Phase 1.5)
- [ ] Aggregation controls (planned for Phase 1.5)

### Phase 1.5: Connection Options & Advanced Relational ✅ COMPLETED
- [x] HTTP/HTTPS REST API connection
- [x] Connection type selector (HTTP, Socket, UDP, Direct C#, Direct C++)
- [x] Connection status indicator
- [x] Test connection button
- [x] Filter grouping with AND/OR/NOT logic
- [x] COLLECT/AGGREGATE clause builder
- [ ] TCP Socket connection implementation (model ready)
- [ ] UDP connection implementation (model ready)
- [ ] Direct C# API integration (model ready)
- [ ] Direct C++ API integration (P/Invoke - model ready)
- [ ] Visual JOIN diagram with drag-drop (planned for Phase 1.6)

### Phase 2: Graph Query Builder
- [ ] Graph canvas component
- [ ] Node/Edge palette
- [ ] Drag-drop pattern builder
- [ ] Traversal syntax generation
- [ ] Graph result visualization

### Phase 3: Vector Query Builder
- [ ] Reference item input
- [ ] Similarity controls UI
- [ ] Vector function support in AQL
- [ ] Similarity score display
- [ ] Hybrid query support

### Phase 4: Geo Query Builder
- [ ] Map component integration (Mapsui)
- [ ] Drawing tools
- [ ] Spatial operator builder
- [ ] Geometry input/output
- [ ] Map-based result display

### Phase 5: Hybrid & Polish
- [ ] Query flow builder
- [ ] Multi-model composition
- [ ] Query templates library
- [ ] Save/load queries
- [ ] Export/share functionality

## Technical Stack

### Core Technologies
- **.NET 8** - Framework
- **WPF** - UI Framework
- **MVVM** - Architecture pattern
- **CommunityToolkit.Mvvm** - MVVM helpers

### Specialized Components

#### Graph Visualization
```xml
<PackageReference Include="Microsoft.Msagl.WpfGraphControl" Version="1.1.7" />
<PackageReference Include="GraphShape" Version="2.0.0" />
```

#### Map Control
```xml
<PackageReference Include="Mapsui.Wpf" Version="4.1.0" />
<PackageReference Include="BruTile" Version="4.1.0" />
```

#### Enhanced UI Controls
```xml
<PackageReference Include="Extended.Wpf.Toolkit" Version="4.5.0" />
<PackageReference Include="MaterialDesignThemes" Version="4.9.0" />
```

#### Schema/Tree Components
```xml
<PackageReference Include="Syncfusion.SfTreeView.WPF" Version="24.1.41" />
<!-- Or use built-in WPF TreeView with custom templates -->
```

## UI/UX Guidelines

### Design Principles

1. **Progressive Disclosure**
   - Simple mode for beginners
   - Advanced mode reveals full power
   - Contextual help throughout

2. **Immediate Feedback**
   - Real-time AQL preview
   - Validation as you type
   - Visual query validation

3. **Discoverability**
   - Icon-based toolbars
   - Tooltips everywhere
   - Example query gallery

4. **Consistency**
   - Unified color scheme for data types
   - Consistent interaction patterns
   - Shared components across tabs

### Color Coding

- 📊 **Relational**: Blue (#007ACC)
- 🕸️ **Graph**: Purple (#8B5CF6)
- 🔢 **Vector**: Orange (#F97316)
- 📍 **Geo**: Green (#10B981)

### Keyboard Shortcuts

- `Ctrl+Enter`: Execute query
- `Ctrl+S`: Save query
- `Ctrl+N`: New query
- `Ctrl+/`: Toggle comments
- `F5`: Refresh schema
- `Ctrl+K`: Command palette

## Next Steps

1. **Immediate** (Phase 1 completion):
   - Add Schema Explorer panel
   - Implement visual JOIN builder
   - Enhanced filter grouping UI

2. **Short-term** (Phase 2):
   - Research and select graph library
   - Prototype graph canvas
   - Implement basic graph patterns

3. **Medium-term** (Phases 3-4):
   - Vector similarity UI
   - Map integration
   - Spatial query tools

4. **Long-term** (Phase 5):
   - Hybrid query composition
   - Query templates
   - Advanced features

## References

- Neo4j Bloom: https://neo4j.com/product/bloom/
- ArcGIS Query Builder: https://doc.arcgis.com/
- Redash: https://github.com/getredash/redash
- GraphQL Editor: https://github.com/graphql-editor/graphql-editor
- MSAGL: https://github.com/microsoft/automatic-graph-layout
- Mapsui: https://github.com/Mapsui/Mapsui
