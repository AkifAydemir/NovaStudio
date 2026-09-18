namespace NovaStudio.Models;

internal sealed class GraphEdge
{
    public GraphEdge(string fromId, string toId, string? label = null)
    {
        FromId = fromId;
        ToId = toId;
        Label = label;
    }
    public string FromId { get; }
    public string ToId { get; }
    public string? Label { get; }
}
