using System.IO;
using System.Text;
using System.Text.Json;
using NovaStudio.Models;
namespace NovaStudio.Services;

internal sealed class NovaWorkspaceService
{
    internal const int ManifestVersion = 2;
    internal const int LegacyManifestVersion = 1;
    internal const int MaxFiles = 16;
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };
    internal sealed record LoadedWorkspace(
    string ManifestPath,
    string RootDirectory,
    NovaProjectManifest Manifest,
    IReadOnlyList<ProjectSourceFile> Files);
    internal LoadedWorkspace Open(string manifestPath)
    {
        var fullManifestPath = Path.GetFullPath(manifestPath);
        var json = File.ReadAllText(fullManifestPath, Encoding.UTF8);
        var manifest = JsonSerializer.Deserialize<NovaProjectManifest>(json, JsonOptions)
        ?? throw new InvalidDataException("Project manifest is empty or invalid.");
        ValidateManifest(manifest);
        if (manifest.Version == LegacyManifestVersion) manifest.Version = ManifestVersion;
        var root = Path.GetDirectoryName(fullManifestPath)
        ?? throw new InvalidDataException("Project manifest has no parent directory.");
        var files = new List<ProjectSourceFile>(manifest.Files.Count);
        for (var i = 0; i < manifest.Files.Count; i++)
        {
            var relative = NormalizeRelativePath(manifest.Files[i].Path);
            var absolute = ResolveProjectPath(root, relative);
            if (!File.Exists(absolute))
                throw new FileNotFoundException($"Project source file is missing: {relative}", absolute);
            files.Add(new ProjectSourceFile((uint)i, relative, File.ReadAllText(absolute, Encoding.UTF8)));
        }
        return new LoadedWorkspace(fullManifestPath, root, manifest, files);
    }
    internal LoadedWorkspace Create(
    string manifestPath,
    string projectName,
    IReadOnlyList<(string Path, string Text)> initialFiles)
    {
        if (initialFiles.Count == 0 || initialFiles.Count > MaxFiles)
            throw new InvalidDataException($"A Nova project must contain 1-{MaxFiles} source files.");
        var fullManifestPath = Path.GetFullPath(manifestPath);
        var root = Path.GetDirectoryName(fullManifestPath)
        ?? throw new InvalidDataException("Project manifest has no parent directory.");

        Directory.CreateDirectory(root);
        var manifest = new NovaProjectManifest
        {
            Version = ManifestVersion,
            Name = string.IsNullOrWhiteSpace(projectName) ? "Nova Project" : projectName.Trim()
        };
        var files = new List<ProjectSourceFile>(initialFiles.Count);
        for (var i = 0; i < initialFiles.Count; i++)
        {
            var relative = NormalizeRelativePath(initialFiles[i].Path);
            EnsureUniquePath(manifest, relative, null);
            var absolute = ResolveProjectPath(root, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(absolute)!);
            if (File.Exists(absolute))
                throw new IOException($"Refusing to overwrite existing project source during create: {relative}");
            File.WriteAllText(absolute, initialFiles[i].Text, new UTF8Encoding(false));
            manifest.Files.Add(new NovaProjectFileEntry { Path = relative });
            files.Add(new ProjectSourceFile((uint)i, relative, initialFiles[i].Text));
        }
        SaveManifest(fullManifestPath, manifest);
        return new LoadedWorkspace(fullManifestPath, root, manifest, files);
    }
    internal void SaveManifest(string manifestPath, NovaProjectManifest manifest)
    {
        ValidateManifest(manifest);
        var json = JsonSerializer.Serialize(manifest, JsonOptions);
        File.WriteAllText(Path.GetFullPath(manifestPath), json + Environment.NewLine, new UTF8Encoding(false));
    }
    internal void SaveFile(string root, ProjectSourceFile file)
    {
        var absolute = ResolveProjectPath(root, file.Name);
        Directory.CreateDirectory(Path.GetDirectoryName(absolute)!);
        File.WriteAllText(absolute, file.Text, new UTF8Encoding(false));
        file.MarkSaved();
    }
    internal void RenameFile(string root, ProjectSourceFile file, string newRelativePath, NovaProjectManifest manifest)
    {
        var normalized = NormalizeRelativePath(newRelativePath);
        EnsureUniquePath(manifest, normalized, file.Name);
        var oldAbsolute = ResolveProjectPath(root, file.Name);
        var newAbsolute = ResolveProjectPath(root, normalized);
        Directory.CreateDirectory(Path.GetDirectoryName(newAbsolute)!);
        if (File.Exists(newAbsolute))
            throw new IOException($"A file already exists at {normalized}.");
        File.Move(oldAbsolute, newAbsolute);
        var entry = manifest.Files.FirstOrDefault(candidate =>
        string.Equals(NormalizeRelativePath(candidate.Path), file.Name, StringComparison.OrdinalIgnoreCase));
        if (entry is null)
            throw new InvalidDataException($"Manifest does not contain {file.Name}.");
        entry.Path = normalized;
        file.Rename(normalized);
    }

    internal string ResolveProjectPath(string root, string relativePath)
    {
        var normalized = NormalizeRelativePath(relativePath);
        var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
        + Path.DirectorySeparatorChar;
        var candidate = Path.GetFullPath(Path.Combine(fullRoot, normalized.Replace('/', Path.DirectorySeparatorChar)));
        if (!candidate.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Project file path escapes the workspace root.");
        return candidate;
    }
    internal string ToRelativeProjectPath(string root, string absolutePath)
    {
        var fullRoot = Path.GetFullPath(root);
        var fullPath = Path.GetFullPath(absolutePath);
        var relative = Path.GetRelativePath(fullRoot, fullPath);
        return NormalizeRelativePath(relative);
    }
    internal static string NormalizeRelativePath(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
            throw new InvalidDataException("Project file path cannot be empty.");
        var normalized = path.Replace('\\', '/').Trim();
        if (normalized.StartsWith("/", StringComparison.Ordinal) || Path.IsPathRooted(normalized))
            throw new InvalidDataException("Project file path must be relative.");
        while (normalized.StartsWith("./", StringComparison.Ordinal)) normalized = normalized[2..];
        if (normalized.Length == 0 || normalized.Split('/').Any(segment => segment == ".."))
            throw new InvalidDataException("Project file path must stay inside the workspace root.");
        if (!normalized.EndsWith(".c", StringComparison.OrdinalIgnoreCase) &&
        !normalized.EndsWith(".h", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Nova project files must use the .c or .h extension.");
        return normalized;
    }
    private static void ValidateManifest(NovaProjectManifest manifest)
    {
        if (manifest.Version != ManifestVersion && manifest.Version != LegacyManifestVersion)
            throw new InvalidDataException($"Unsupported Nova project manifest version {manifest.Version}.");
        if (manifest.Files.Count == 0 || manifest.Files.Count > MaxFiles)
            throw new InvalidDataException($"A Nova project must contain 1-{MaxFiles} source files.");
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var sourceCount = 0;
        foreach (var file in manifest.Files)
        {
            var normalized = NormalizeRelativePath(file.Path);
            file.Path = normalized;
            if (!seen.Add(normalized))
                throw new InvalidDataException($"Duplicate project source path: {normalized}");
            if (normalized.EndsWith(".c", StringComparison.OrdinalIgnoreCase)) sourceCount++;
        }
        if (sourceCount == 0)
            throw new InvalidDataException("A Nova project must contain at least one .c translation unit.");
    }
    private static void EnsureUniquePath(NovaProjectManifest manifest, string candidate, string? except)
    {
        if (manifest.Files.Any(file =>
        !string.Equals(NormalizeRelativePath(file.Path), except, StringComparison.OrdinalIgnoreCase) &&

        string.Equals(NormalizeRelativePath(file.Path), candidate, StringComparison.OrdinalIgnoreCase)))
            throw new InvalidDataException($"Duplicate project source path: {candidate}");
    }
}
