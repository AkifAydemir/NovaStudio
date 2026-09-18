namespace NovaStudio.Models;

internal enum GraphNodeKind
{
    Project,
    Source,
    Header,
    TypeAlias,
    Macro,
    Function,
    Global,
    Compiler,
    Runtime,
    Cpu,
    Memory,
    Io,
    Bridge
}
internal sealed class GraphNode
{
    public GraphNode(string id, string title, string subtitle, GraphNodeKind kind, double x, double y, double width = 180, double height = 82)
    {
        Id = id;
        Title = title;
        Subtitle = subtitle;
        Kind = kind;
        X = x;
        Y = y;
        Width = width;
        Height = height;
    }
    public string Id { get; }
    public string Title { get; }
    public string Subtitle { get; }
    public GraphNodeKind Kind { get; }
    public double X { get; set; }
    public double Y { get; set; }
    public double Width { get; }
    public double Height { get; }
}
