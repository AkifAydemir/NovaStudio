namespace NovaStudio.Models;

internal sealed class NovaProjectManifest
{
    public int Version { get; set; } = 1;
    public string Name { get; set; } = "Nova Project";
    public List<NovaProjectFileEntry> Files { get; set; } = new();
    public Dictionary<string, NovaGraphPosition> Graph { get; set; } = new(StringComparer.Ordinal);
}
internal sealed class NovaProjectFileEntry
{
    public string Path { get; set; } = string.Empty;
}
internal sealed class NovaGraphPosition
{
    public double X { get; set; }
    public double Y { get; set; }
}
