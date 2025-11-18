# Phase 5: Advanced UI/UX Features - Roadmap

## Overview

Phase 5 introduces advanced UI/UX features to transform the AQL Query Builder into a professional, intuitive tool with modern interaction patterns.

**Status:** 📋 Planned  
**Estimated Effort:** 8-12 weeks  
**Dependencies:** Zero (WPF built-in features only)

---

## Requirements

### 1. Interactive Help System 📖

**Goal:** Provide comprehensive, context-sensitive help for users.

**Features:**
- **Context-Sensitive Help**
  - Tooltips with rich content (images, formatting)
  - F1 key for context help
  - Help panel with searchable content
  
- **Tutorial Walkthroughs**
  - Step-by-step guided tours
  - Interactive overlays highlighting UI elements
  - Progress tracking
  
- **User Guide**
  - Embedded HTML help viewer
  - Searchable documentation
  - Copy-paste examples
  
- **FAQ & Troubleshooting**
  - Common questions
  - Error resolution guides
  - Best practices

**Implementation:**
```csharp
// WPF ToolTip with rich content
<ToolTip>
    <StackPanel>
        <TextBlock FontWeight="Bold">FOR Clause</TextBlock>
        <TextBlock>Iterates over a collection</TextBlock>
        <TextBlock FontStyle="Italic">Example: FOR u IN users</TextBlock>
    </StackPanel>
</ToolTip>

// Tutorial overlay with Adorner
public class TutorialAdorner : Adorner
{
    // Highlight UI element
    // Show step instructions
    // Navigate between steps
}
```

---

### 2. Grid Layout with Rulers 📐

**Goal:** Professional layout system with precise positioning.

**Features:**
- **Rulers**
  - Horizontal ruler (top)
  - Vertical ruler (left)
  - Current mouse position indicator
  - Unit selection (pixels, logical units)
  
- **Grid Overlay**
  - Customizable grid spacing (5px, 10px, 20px)
  - Toggle grid visibility
  - Grid color and opacity
  - Major/minor grid lines
  
- **Measurements**
  - Display coordinates
  - Show distances during drag
  - Dimension indicators

**Implementation:**
```csharp
// Ruler control
public class RulerControl : Control
{
    public Orientation Orientation { get; set; }
    public double Scale { get; set; } // For zoom
    
    protected override void OnRender(DrawingContext dc)
    {
        // Draw ruler marks
        // Draw numbers
        // Highlight current position
    }
}

// Grid overlay
public class GridOverlay : FrameworkElement
{
    public double GridSpacing { get; set; } = 10;
    public Brush GridBrush { get; set; }
    
    protected override void OnRender(DrawingContext dc)
    {
        // Draw grid lines
        // Major lines every 10 units
        // Minor lines every unit
    }
}
```

---

### 3. Grid Magnetic (Snap-to-Grid) 🧲

**Goal:** Precise alignment with smart snapping.

**Features:**
- **Snap-to-Grid**
  - Snap while dragging
  - Configurable snap distance (5px default)
  - Toggle snap on/off (Alt key)
  
- **Alignment Guides**
  - Vertical alignment lines
  - Horizontal alignment lines
  - Distance markers
  - Edge alignment
  
- **Visual Feedback**
  - Highlight snap targets
  - Show guide lines during drag
  - Animate snap motion

**Implementation:**
```csharp
public class SnapToGridBehavior
{
    public double GridSize { get; set; } = 10;
    public double SnapDistance { get; set; } = 5;
    
    public Point SnapPoint(Point point, bool enableSnap)
    {
        if (!enableSnap) return point;
        
        double x = Math.Round(point.X / GridSize) * GridSize;
        double y = Math.Round(point.Y / GridSize) * GridSize;
        
        // Only snap if within SnapDistance
        if (Math.Abs(x - point.X) <= SnapDistance &&
            Math.Abs(y - point.Y) <= SnapDistance)
        {
            return new Point(x, y);
        }
        
        return point;
    }
}
```

---

### 4. Pan/Zoom/Move Navigation 🔍

**Goal:** Smooth navigation for complex queries.

**Features:**
- **Zoom**
  - Mouse wheel zoom (Ctrl+Wheel)
  - Zoom in/out buttons
  - Zoom slider (25%-400%)
  - Fit to view
  - Zoom to selection
  - Reset view (100%)
  
- **Pan**
  - Middle-mouse drag to pan
  - Spacebar+drag to pan
  - Scroll bars
  - Auto-scroll during drag near edges
  
- **Mini-Map**
  - Bird's eye view of entire canvas
  - Current viewport indicator
  - Click to navigate
  - Drag viewport rectangle

**Implementation:**
```csharp
public class ZoomPanCanvas : Canvas
{
    public double ZoomLevel { get; set; } = 1.0;
    public Point PanOffset { get; set; }
    
    protected override void OnMouseWheel(MouseWheelEventArgs e)
    {
        if (Keyboard.Modifiers == ModifierKeys.Control)
        {
            // Zoom at mouse position
            Point mouse = e.GetPosition(this);
            double zoom = e.Delta > 0 ? 1.1 : 0.9;
            
            ZoomLevel *= zoom;
            PanOffset = CalculateNewPanOffset(mouse, zoom);
            
            ApplyTransform();
        }
    }
    
    private void ApplyTransform()
    {
        var transform = new TransformGroup();
        transform.Children.Add(new ScaleTransform(ZoomLevel, ZoomLevel));
        transform.Children.Add(new TranslateTransform(PanOffset.X, PanOffset.Y));
        RenderTransform = transform;
    }
}
```

---

### 5. Advanced Mouse Interaction 🖱️

**Goal:** Intuitive mouse-based operations.

**Features:**
- **Multi-Select**
  - Ctrl+click to add/remove
  - Shift+click for range
  - Rubber-band selection (drag rectangle)
  - Select all (Ctrl+A)
  
- **Drag-and-Drop**
  - Drag query components
  - Drag from component palette
  - Drop target indicators
  - Drag preview
  
- **Hover Effects**
  - Highlight on hover
  - Show additional info
  - Preview connections
  - Tooltip hints
  
- **Double-Click Actions**
  - Edit component
  - Expand/collapse
  - Quick actions

**Implementation:**
```csharp
// Multi-select with rubber band
public class SelectionRubberBand : Adorner
{
    private Point startPoint;
    private Point endPoint;
    
    protected override void OnRender(DrawingContext dc)
    {
        var rect = new Rect(startPoint, endPoint);
        var brush = new SolidColorBrush(Colors.LightBlue) { Opacity = 0.3 };
        var pen = new Pen(Brushes.Blue, 1);
        
        dc.DrawRectangle(brush, pen, rect);
    }
}

// Drag-drop with preview
private void OnDragStart(object sender, MouseEventArgs e)
{
    var data = new DataObject("QueryComponent", component);
    
    // Create drag preview
    var preview = new Image { Source = RenderPreview(component) };
    
    DragDrop.DoDragDrop(sender as DependencyObject, data, 
        DragDropEffects.Move);
}
```

---

### 6. Context Menus 📋

**Goal:** Quick access to common operations.

**Features:**
- **Right-Click Menus**
  - Component-specific actions
  - Copy/paste/delete
  - Duplicate
  - Properties
  
- **Quick Actions**
  - Add filter
  - Add sort
  - Clear all
  - Reset to default
  
- **Clipboard**
  - Copy query as AQL
  - Copy as JSON
  - Paste from clipboard
  
- **Keyboard Shortcuts**
  - Ctrl+C, Ctrl+V, Ctrl+X
  - Delete key
  - F2 for rename

**Implementation:**
```csharp
<ContextMenu>
    <MenuItem Header="Copy" Command="{Binding CopyCommand}" 
              InputGestureText="Ctrl+C"/>
    <MenuItem Header="Paste" Command="{Binding PasteCommand}" 
              InputGestureText="Ctrl+V"/>
    <MenuItem Header="Delete" Command="{Binding DeleteCommand}" 
              InputGestureText="Del"/>
    <Separator/>
    <MenuItem Header="Duplicate" Command="{Binding DuplicateCommand}"/>
    <MenuItem Header="Properties" Command="{Binding ShowPropertiesCommand}"/>
</ContextMenu>
```

---

### 7. Cards Layout 🎴

**Goal:** Modern card-based UI for query building.

**Features:**
- **Query Component Cards**
  - FOR clause card
  - FILTER clause card
  - SORT clause card
  - Visual card templates
  
- **Card Operations**
  - Drag to reorder
  - Collapse/expand
  - Add/remove cards
  - Card library/palette
  
- **Card Connections**
  - Visual flow between cards
  - Data flow indicators
  - Connection validation
  
- **Card Layouts**
  - Vertical stack
  - Horizontal flow
  - Grid layout
  - Auto-arrange

**Implementation:**
```csharp
public class QueryCard : ContentControl
{
    public string CardType { get; set; } // FOR, FILTER, SORT, etc.
    public bool IsCollapsed { get; set; }
    public int Order { get; set; }
    
    static QueryCard()
    {
        DefaultStyleKeyProperty.OverrideMetadata(
            typeof(QueryCard),
            new FrameworkPropertyMetadata(typeof(QueryCard)));
    }
}

// Card layout panel
public class CardLayoutPanel : Panel
{
    protected override Size ArrangeOverride(Size finalSize)
    {
        double y = 0;
        foreach (UIElement child in Children)
        {
            if (child is QueryCard card)
            {
                double height = card.IsCollapsed ? 40 : card.DesiredSize.Height;
                child.Arrange(new Rect(0, y, finalSize.Width, height));
                y += height + 10; // 10px spacing
            }
        }
        return finalSize;
    }
}
```

---

### 8. Wiring Options 🔌

**Goal:** Visual query flow with node-based editor.

**Features:**
- **Node-Based Editor**
  - Nodes for each query component
  - Input/output ports
  - Connection lines
  - Node library
  
- **Connections**
  - Bezier curve lines
  - Auto-routing
  - Connection validation
  - Port compatibility
  
- **Visual Feedback**
  - Highlight valid targets
  - Invalid connection indicators
  - Data flow animation
  - Connection preview
  
- **Layout**
  - Auto-layout algorithm
  - Manual positioning
  - Align nodes
  - Distribute evenly

**Implementation:**
```csharp
public class QueryNode : Control
{
    public List<NodePort> InputPorts { get; set; }
    public List<NodePort> OutputPorts { get; set; }
    public Point Position { get; set; }
}

public class NodePort
{
    public string Name { get; set; }
    public PortType Type { get; set; } // Input, Output
    public Type DataType { get; set; }
    public Point Position { get; set; }
}

public class NodeConnection
{
    public NodePort From { get; set; }
    public NodePort To { get; set; }
    
    public void Render(DrawingContext dc)
    {
        // Bezier curve from From.Position to To.Position
        var start = From.Position;
        var end = To.Position;
        
        var controlPoint1 = new Point(start.X + 50, start.Y);
        var controlPoint2 = new Point(end.X - 50, end.Y);
        
        var figure = new PathFigure { StartPoint = start };
        figure.Segments.Add(new BezierSegment(
            controlPoint1, controlPoint2, end, true));
        
        var geometry = new PathGeometry();
        geometry.Figures.Add(figure);
        
        dc.DrawGeometry(null, new Pen(Brushes.Blue, 2), geometry);
    }
}
```

---

### 9. AI Integration (Ollama) 🤖

**Goal:** Natural language query building with AI assistance.

**Features:**
- **Natural Language to AQL**
  - Text input for queries
  - AI conversion to AQL
  - Preview before execution
  - Iterative refinement
  
- **Query Suggestions**
  - Auto-complete for collections
  - Field suggestions
  - Smart defaults
  - Example queries
  
- **Semantic Search**
  - Search collections by description
  - Find similar queries
  - Discover relationships
  
- **Optimization Tips**
  - Query performance hints
  - Index suggestions
  - Alternative approaches
  
- **Explanation**
  - Explain AQL query in plain language
  - Step-by-step breakdown
  - Complexity analysis

**Implementation:**
```csharp
public interface IOllamaService
{
    Task<Result<string>> ConvertToAqlAsync(string naturalLanguage, 
        CancellationToken ct);
    Task<Result<List<string>>> GetSuggestionsAsync(string partial, 
        CancellationToken ct);
    Task<Result<string>> ExplainQueryAsync(string aql, 
        CancellationToken ct);
}

public class OllamaService : IOllamaService
{
    private readonly HttpClient httpClient;
    private readonly string ollamaUrl = "http://localhost:11434";
    
    public async Task<Result<string>> ConvertToAqlAsync(
        string naturalLanguage, CancellationToken ct)
    {
        var prompt = $@"
Convert this natural language query to AQL (Themis Query Language):
'{naturalLanguage}'

Schema:
- users: name, age, email
- products: name, price, category
- orders: user_id, product_id, amount

Return only the AQL query, no explanation.
";
        
        var request = new
        {
            model = "codellama",
            prompt = prompt,
            stream = false
        };
        
        var json = JsonSerializer.Serialize(request);
        var content = new StringContent(json, Encoding.UTF8, 
            "application/json");
        
        var response = await httpClient.PostAsync(
            $"{ollamaUrl}/api/generate", content, ct)
            .ConfigureAwait(false);
        
        if (!response.IsSuccessStatusCode)
        {
            return Result<string>.Failure(
                $"Ollama error: {response.StatusCode}");
        }
        
        var result = await response.Content.ReadAsStringAsync(ct)
            .ConfigureAwait(false);
        var ollamaResponse = JsonSerializer.Deserialize<OllamaResponse>(result);
        
        return Result<string>.Success(ollamaResponse.Response);
    }
}

// UI Component
public class AiQueryPanel : UserControl
{
    private readonly IOllamaService ollamaService;
    
    [RelayCommand]
    private async Task ConvertQueryAsync()
    {
        var nl = NaturalLanguageInput;
        var result = await ollamaService.ConvertToAqlAsync(nl, default);
        
        if (result.IsSuccess)
        {
            GeneratedAql = result.Value;
            ShowPreview = true;
        }
        else
        {
            ErrorMessage = result.Error;
        }
    }
}
```

---

## Implementation Phases

### Phase 5.1: Help & Navigation (2 weeks)

**Week 1:**
- Context-sensitive help system
- Rich tooltips
- Tutorial overlay framework

**Week 2:**
- Pan/Zoom/Move implementation
- Mini-map navigator
- Context menus

**Deliverables:**
- Interactive help system
- Full navigation support
- Context menus for all components

---

### Phase 5.2: Grid & Rulers (2-3 weeks)

**Week 1:**
- Ruler controls (horizontal/vertical)
- Grid overlay implementation
- Toggle controls

**Week 2:**
- Snap-to-grid logic
- Alignment guides
- Visual feedback

**Week 3 (if needed):**
- Fine-tuning
- Performance optimization
- Edge cases

**Deliverables:**
- Professional grid system
- Smart snapping
- Alignment guides

---

### Phase 5.3: Cards & Wiring (3-4 weeks)

**Week 1-2:**
- Card-based UI components
- Card layout panel
- Drag-drop reordering

**Week 3-4:**
- Node-based editor
- Connection lines (Bezier)
- Auto-routing algorithm

**Deliverables:**
- Complete card system
- Visual flow editor
- Connection wiring

---

### Phase 5.4: AI Integration (3-4 weeks)

**Week 1:**
- Ollama HTTP client
- Service interface
- Error handling

**Week 2:**
- NL to AQL conversion
- Prompt engineering
- Response parsing

**Week 3:**
- Query suggestions
- Auto-complete
- Semantic search

**Week 4 (if needed):**
- Optimization tips
- Query explanation
- Fine-tuning

**Deliverables:**
- Ollama integration
- AI-powered query building
- Smart suggestions

---

## Technical Stack (Zero Dependencies)

**WPF Built-in Features:**
- `Canvas` - Grid, rulers, node editor
- `Adorner` - Alignment guides, overlays
- `DragDrop` - Drag-drop operations
- `ToolTip` - Rich tooltips
- `ContextMenu` - Right-click menus
- `PathGeometry` - Bezier curves
- `ScaleTransform` - Zoom
- `TranslateTransform` - Pan

**System Libraries:**
- `System.Net.Http` - Ollama HTTP client
- `System.Text.Json` - JSON serialization
- `System.Windows.Media` - Rendering

**Custom Controls:**
- `RulerControl`
- `GridOverlay`
- `ZoomPanCanvas`
- `QueryCard`
- `QueryNode`
- `NodeConnection`

---

## Priority & Complexity

### High Priority (Start First)

**Quick Wins:**
1. ✅ Context menus (1 week)
2. ✅ Pan/Zoom (1 week)
3. ✅ Interactive help (1 week)

### Medium Priority (Next)

**Professional UX:**
4. ⚠️ Grid with rulers (2 weeks)
5. ⚠️ Snap-to-grid (1 week)
6. ⚠️ Cards layout (2 weeks)

### Low Priority (Later)

**Advanced Features:**
7. ⚠️⚠️ Visual wiring (3 weeks)
8. ⚠️⚠️ AI integration (3 weeks)

---

## Success Criteria

**Phase 5.1:**
- [ ] Context help accessible via F1
- [ ] Zoom works smoothly (25%-400%)
- [ ] Pan with middle-mouse
- [ ] Context menus on all components

**Phase 5.2:**
- [ ] Rulers show accurate measurements
- [ ] Grid overlay toggleable
- [ ] Snap-to-grid configurable
- [ ] Alignment guides appear during drag

**Phase 5.3:**
- [ ] Cards can be dragged/reordered
- [ ] Nodes connect with lines
- [ ] Auto-layout works
- [ ] Visual flow makes sense

**Phase 5.4:**
- [ ] Ollama responds in <2 seconds
- [ ] NL to AQL conversion >80% accurate
- [ ] Suggestions are relevant
- [ ] Query explanation is clear

---

## Risk Mitigation

**Risk:** Ollama not available on all deployments  
**Mitigation:** Make AI optional, graceful degradation

**Risk:** Visual wiring too complex  
**Mitigation:** Start with simple cards, wiring is enhancement

**Risk:** Performance with many nodes/connections  
**Mitigation:** Virtualization, lazy rendering, limit nodes

**Risk:** Grid/snapping feels sluggish  
**Mitigation:** Optimize render loop, use GPU acceleration

---

## Next Steps

1. **Review roadmap** with team
2. **Prioritize features** based on user needs
3. **Start with Phase 5.1** for quick wins
4. **Iterate based on feedback**

**Estimated Total: 8-12 weeks for all phases**

**Ready to begin implementation! 🚀**
