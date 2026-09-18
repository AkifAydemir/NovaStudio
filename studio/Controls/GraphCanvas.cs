using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using NovaStudio.Models;
namespace NovaStudio.Controls;

public sealed class GraphCanvas : FrameworkElement
{
    private readonly List<GraphNode> _nodes = new();
    private readonly List<GraphEdge> _edges = new();
    private readonly HashSet<string> _runtimeNodeIds = new(StringComparer.Ordinal);
    private readonly HashSet<string> _runtimeEdgeKeys = new(StringComparer.Ordinal);
    private readonly HashSet<string> _breakpointNodeIds = new(StringComparer.Ordinal);
    private readonly Typeface _titleTypeface = new("Segoe UI Semibold");
    private readonly Typeface _bodyTypeface = new("Segoe UI");
    private double _zoom = 1.0;
    private Vector _pan = new(70, 45);
    private Point _lastMouse;
    private GraphNode? _draggedNode;
    private bool _isPanning;
    private bool _runtimeOverlayEnabled = true;
    private string? _activeNodeId;
    private string? _activityNodeId;
    private string? _selectedNodeId;
    internal event Action<GraphNode?>? NodeSelected;
    internal event Action<GraphNode>? NodeActivated;
    internal event Action<GraphNode>? NodeMoved;
    internal event Action<double>? ZoomChanged;
    internal IReadOnlyList<GraphNode> Nodes => _nodes;
    internal bool RuntimeOverlayEnabled
    {
        get => _runtimeOverlayEnabled;
        set
        {
            if (_runtimeOverlayEnabled == value) return;
            _runtimeOverlayEnabled = value;
            InvalidateVisual();
        }
    }
    internal string? ActiveNodeId
    {
        get => _activeNodeId;
        set
        {
            if (_activeNodeId == value) return;
            _activeNodeId = value;
            InvalidateVisual();
        }
    }
    internal void SetGraph(IEnumerable<GraphNode> nodes, IEnumerable<GraphEdge> edges)
    {
        var previousSelection = _selectedNodeId;
        _nodes.Clear();
        _nodes.AddRange(nodes);
        _edges.Clear();
        _edges.AddRange(edges);
        _selectedNodeId = previousSelection is not null && _nodes.Any(node => node.Id == previousSelection)
        ? previousSelection
        : _nodes.FirstOrDefault()?.Id;
        InvalidateVisual();
        NodeSelected?.Invoke(_nodes.FirstOrDefault(node => node.Id == _selectedNodeId));
    }
    internal void SetRuntimeState(
    IEnumerable<string> activeNodeIds,
    IEnumerable<(string FromId, string ToId)> activeEdges,
    string? activityNodeId)
    {
        _runtimeNodeIds.Clear();
        foreach (var id in activeNodeIds) _runtimeNodeIds.Add(id);
        _runtimeEdgeKeys.Clear();
        foreach (var edge in activeEdges) _runtimeEdgeKeys.Add(EdgeKey(edge.FromId, edge.ToId));
        _activityNodeId = activityNodeId;
        InvalidateVisual();
    }
    internal void SetBreakpointNodes(IEnumerable<string> nodeIds)
    {
        _breakpointNodeIds.Clear();
        foreach (var id in nodeIds) _breakpointNodeIds.Add(id);
        InvalidateVisual();
    }
    internal void ResetView()
    {
        _zoom = 1.0;
        _pan = new Vector(70, 45);
        ZoomChanged?.Invoke(_zoom);
        InvalidateVisual();
    }
    protected override void OnRender(DrawingContext dc)
    {
        base.OnRender(dc);
        dc.DrawRectangle(new SolidColorBrush(Color.FromRgb(8, 11, 15)), null, new Rect(RenderSize));
        DrawGrid(dc);
        var transform = new MatrixTransform(new Matrix(_zoom, 0, 0, _zoom, _pan.X, _pan.Y));
        dc.PushTransform(transform);
        foreach (var edge in _edges) DrawEdge(dc, edge);
        foreach (var node in _nodes) DrawNode(dc, node);
        dc.Pop();
    }
    private void DrawGrid(DrawingContext dc)
    {
        const double spacing = 28.0;
        var pen = new Pen(new SolidColorBrush(Color.FromRgb(18, 24, 29)), 1.0);
        var scaled = spacing * _zoom;
        if (scaled < 12) return;
        var startX = _pan.X % scaled;
        var startY = _pan.Y % scaled;
        for (var x = startX; x < ActualWidth; x += scaled)
            dc.DrawLine(pen, new Point(x, 0), new Point(x, ActualHeight));
        for (var y = startY; y < ActualHeight; y += scaled)
            dc.DrawLine(pen, new Point(0, y), new Point(ActualWidth, y));
    }
    private void DrawEdge(DrawingContext dc, GraphEdge edge)
    {
        var from = _nodes.FirstOrDefault(node => node.Id == edge.FromId);
        var to = _nodes.FirstOrDefault(node => node.Id == edge.ToId);
        if (from is null || to is null) return;
        var a = NodeCenter(from);
        var b = NodeCenter(to);
        var dx = Math.Max(60, Math.Abs(b.X - a.X) * 0.45);
        var direction = Math.Sign(b.X - a.X);
        if (direction == 0) direction = 1;
        var geometry = new StreamGeometry();
        using (var context = geometry.Open())
        {
            context.BeginFigure(a, false, false);
            context.BezierTo(
            new Point(a.X + dx * direction, a.Y),
            new Point(b.X - dx * direction, b.Y),
            b,
            true,
            false);
        }
        geometry.Freeze();
        var runtimeEdge = RuntimeOverlayEnabled && _runtimeEdgeKeys.Contains(EdgeKey(edge.FromId, edge.ToId));
        if (runtimeEdge)
        {
            dc.DrawGeometry(null, new Pen(new SolidColorBrush(Color.FromArgb(42, 80, 229, 154)), 7.0), geometry);
            dc.DrawGeometry(null, new Pen(new SolidColorBrush(Color.FromRgb(91, 226, 164)), 2.4), geometry);
        }
        else
        {
            dc.DrawGeometry(null, new Pen(new SolidColorBrush(Color.FromRgb(48, 59, 69)), 1.5), geometry);
        }
        if (!string.IsNullOrWhiteSpace(edge.Label) && (runtimeEdge || nodeLabelIsUseful(edge.Label)))
            DrawEdgeLabel(dc, edge.Label!, a, b, runtimeEdge);
    }
    private static bool nodeLabelIsUseful(string label)
    => label is "calls" or "reads" or "writes" or "includes";
    private void DrawEdgeLabel(DrawingContext dc, string label, Point a, Point b, bool runtimeEdge)
    {
        var text = new FormattedText(
        label,
        System.Globalization.CultureInfo.InvariantCulture,
        FlowDirection.LeftToRight,
        _bodyTypeface,
        8,
        new SolidColorBrush(runtimeEdge ? Color.FromRgb(105, 231, 174) : Color.FromRgb(103, 116, 128)),
        VisualTreeHelper.GetDpi(this).PixelsPerDip);
        var x = (a.X + b.X) / 2.0 - text.Width / 2.0;
        var y = (a.Y + b.Y) / 2.0 - text.Height / 2.0;
        dc.DrawText(text, new Point(x, y));
    }
    private void DrawNode(DrawingContext dc, GraphNode node)
    {
        var rect = new Rect(node.X, node.Y, node.Width, node.Height);
        var isSelected = node.Id == _selectedNodeId;
        var isFocused = node.Id == _activeNodeId;
        var isRuntime = RuntimeOverlayEnabled && _runtimeNodeIds.Contains(node.Id);
        var isActivity = RuntimeOverlayEnabled && node.Id == _activityNodeId;
        var isBreakpoint = _breakpointNodeIds.Contains(node.Id);
        var fillColor = NodeFill(node.Kind, isActivity);
        var fill = new SolidColorBrush(fillColor);
        var borderColor = isRuntime
        ? Color.FromRgb(91, 226, 164)
        : isFocused
        ? Color.FromRgb(103, 205, 157)
        : isSelected
        ? Color.FromRgb(126, 151, 163)
        : Color.FromRgb(43, 54, 64);
        var border = new Pen(new SolidColorBrush(borderColor), isRuntime || isFocused || isSelected ? 2.0 : 1.0);
        if (isRuntime)
            DrawNodeGlow(dc, node, rect);
        if (IsCircular(node.Kind))
            dc.DrawEllipse(fill, border, NodeCenter(node), node.Width / 2.0, node.Height / 2.0);
        else
            dc.DrawRoundedRectangle(fill, border, rect, NodeCornerRadius(node.Kind), NodeCornerRadius(node.Kind));
        DrawNodeText(dc, node, isRuntime, isActivity);
        if (isBreakpoint) DrawBreakpointBadge(dc, node);
    }
    private static Color NodeFill(GraphNodeKind kind, bool isActivity)
    {
        if (isActivity) return Color.FromRgb(18, 48, 36);
        return kind switch
        {
            GraphNodeKind.Project => Color.FromRgb(20, 26, 31),
            GraphNodeKind.Source => Color.FromRgb(18, 25, 29),
            GraphNodeKind.Header => Color.FromRgb(21, 25, 29),
            GraphNodeKind.TypeAlias => Color.FromRgb(22, 27, 31),
            GraphNodeKind.Macro => Color.FromRgb(24, 27, 30),
            GraphNodeKind.Function => Color.FromRgb(18, 28, 27),
            GraphNodeKind.Global => Color.FromRgb(23, 27, 29),
            GraphNodeKind.Compiler => Color.FromRgb(17, 31, 27),
            GraphNodeKind.Runtime => Color.FromRgb(14, 34, 28),
            GraphNodeKind.Cpu => Color.FromRgb(17, 31, 29),
            GraphNodeKind.Memory => Color.FromRgb(22, 27, 28),
            GraphNodeKind.Io => Color.FromRgb(19, 29, 28),
            _ => Color.FromRgb(19, 24, 29)
        };
    }
    private void DrawNodeGlow(DrawingContext dc, GraphNode node, Rect rect)
    {
        var glow = new Pen(new SolidColorBrush(Color.FromArgb(55, 71, 219, 139)), 9.0);
        if (IsCircular(node.Kind))
            dc.DrawEllipse(null, glow, NodeCenter(node), node.Width / 2.0 + 2, node.Height / 2.0 + 2);
        else
            dc.DrawRoundedRectangle(null, glow, rect, NodeCornerRadius(node.Kind) + 2, NodeCornerRadius(node.Kind) + 2);
    }
    private void DrawNodeText(DrawingContext dc, GraphNode node, bool isRuntime, bool isActivity)
    {
        var activeColor = isRuntime ? Color.FromRgb(105, 231, 174) : Color.FromRgb(125, 139, 147);
        if (IsCircular(node.Kind))
        {
            var kind = Formatted(node.Kind.ToString().ToUpperInvariant(), 8, _bodyTypeface, activeColor, node.Width - 18, TextAlignment.Center);
            var title = Formatted(node.Title, 13, _titleTypeface, Color.FromRgb(232, 239, 236), node.Width - 18, TextAlignment.Center);
            var subtitle = Formatted(node.Subtitle, 8.5, _bodyTypeface, Color.FromRgb(132, 146, 149), node.Width - 22, TextAlignment.Center);
            dc.DrawText(kind, new Point(node.X + 9, node.Y + 22));
            dc.DrawText(title, new Point(node.X + 9, node.Y + 43));
            dc.DrawText(subtitle, new Point(node.X + 11, node.Y + 67));
            return;
        }
        var kindText = Formatted(node.Kind.ToString().ToUpperInvariant(), 9, _bodyTypeface, activeColor, node.Width - 28, TextAlignment.Left);
        var titleText = Formatted(node.Title, 14, _titleTypeface, Color.FromRgb(232, 236, 239), node.Width - 28, TextAlignment.Left);
        var subtitleText = Formatted(node.Subtitle, 10, _bodyTypeface, isActivity ? Color.FromRgb(129, 219, 174) : Color.FromRgb(125, 139, 147), node.Width - 28, TextAlignment.Left);
        dc.DrawText(kindText, new Point(node.X + 14, node.Y + 11));
        dc.DrawText(titleText, new Point(node.X + 14, node.Y + 31));
        dc.DrawText(subtitleText, new Point(node.X + 14, node.Y + 55));
    }
    private FormattedText Formatted(string text, double size, Typeface typeface, Color color, double maxWidth, TextAlignment alignment)
    => new(
    text,
    System.Globalization.CultureInfo.InvariantCulture,
    FlowDirection.LeftToRight,
    typeface,
    size,
    new SolidColorBrush(color),
    VisualTreeHelper.GetDpi(this).PixelsPerDip)
    {
        MaxTextWidth = Math.Max(20, maxWidth),
        Trimming = TextTrimming.CharacterEllipsis,
        TextAlignment = alignment
    };
    private void DrawBreakpointBadge(DrawingContext dc, GraphNode node)
    {
        var center = new Point(node.X + node.Width - 10, node.Y + 11);
        dc.DrawEllipse(new SolidColorBrush(Color.FromRgb(255, 102, 116)), new Pen(new SolidColorBrush(Color.FromRgb(255, 189, 196)), 1), center, 5, 5);
    }
    private static bool IsCircular(GraphNodeKind kind) => kind is GraphNodeKind.Runtime or GraphNodeKind.Cpu;
    private static double NodeCornerRadius(GraphNodeKind kind) => kind is GraphNodeKind.Memory or GraphNodeKind.Io or GraphNodeKind.Global ? 9 : 14;
    private static Point NodeCenter(GraphNode node) => new(node.X + node.Width / 2.0, node.Y + node.Height / 2.0);
    private static string EdgeKey(string fromId, string toId) => fromId + "\u001f" + toId;
    protected override void OnMouseWheel(MouseWheelEventArgs e)
    {
        var cursor = e.GetPosition(this);
        var before = ScreenToWorld(cursor);
        var factor = e.Delta > 0 ? 1.12 : 1.0 / 1.12;
        _zoom = Math.Clamp(_zoom * factor, 0.35, 2.5);
        _pan = new Vector(cursor.X - before.X * _zoom, cursor.Y - before.Y * _zoom);
        ZoomChanged?.Invoke(_zoom);
        InvalidateVisual();
        e.Handled = true;
    }
    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        Focus();
        _lastMouse = e.GetPosition(this);
        var world = ScreenToWorld(_lastMouse);
        _draggedNode = HitTestNode(world);
        if (_draggedNode is not null)
        {
            _selectedNodeId = _draggedNode.Id;
            NodeSelected?.Invoke(_draggedNode);
            if (e.ClickCount >= 2)
            {
                NodeActivated?.Invoke(_draggedNode);
                _draggedNode = null;
                InvalidateVisual();
                e.Handled = true;
                return;
            }
        }
        else
        {
            _selectedNodeId = null;
            _isPanning = true;
            NodeSelected?.Invoke(null);
        }
        CaptureMouse();
        InvalidateVisual();
        e.Handled = true;
    }
    protected override void OnMouseMove(MouseEventArgs e)
    {
        if (!IsMouseCaptured) return;
        var current = e.GetPosition(this);
        var delta = current - _lastMouse;
        _lastMouse = current;
        if (_draggedNode is not null)
        {
            _draggedNode.X += delta.X / _zoom;
            _draggedNode.Y += delta.Y / _zoom;
        }
        else if (_isPanning)
        {
            _pan += delta;
        }
        InvalidateVisual();
    }
    protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
    {
        var movedNode = _draggedNode;
        _draggedNode = null;
        _isPanning = false;
        if (IsMouseCaptured) ReleaseMouseCapture();
        if (movedNode is not null) NodeMoved?.Invoke(movedNode);
        e.Handled = true;
    }
    private Point ScreenToWorld(Point screen) => new((screen.X - _pan.X) / _zoom, (screen.Y - _pan.Y) / _zoom);
    private GraphNode? HitTestNode(Point point)
    {
        for (var i = _nodes.Count - 1; i >= 0; --i)
        {
            var node = _nodes[i];
            if (!IsCircular(node.Kind))
            {
                if (new Rect(node.X, node.Y, node.Width, node.Height).Contains(point)) return node;
                continue;
            }
            var center = NodeCenter(node);
            var dx = (point.X - center.X) / (node.Width / 2.0);
            var dy = (point.Y - center.Y) / (node.Height / 2.0);
            if (dx * dx + dy * dy <= 1.0) return node;
        }
        return null;
    }
}
