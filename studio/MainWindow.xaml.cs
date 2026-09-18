using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows;
using Microsoft.Win32;
using NovaStudio.Interop;
using NovaStudio.Models;
using NovaStudio.Services;
namespace NovaStudio;

public partial class MainWindow : Window
{
    private const byte OpHalt = 0x01;
    private const byte OpMovRi = 0x10;
    private const byte OpStore8 = 0x60;
    private const uint MmioConsole = 0x000F0000;
    private const uint NovaCFirstSavedRegister = 1u;
    private const uint NovaCLastSavedRegister = 14u;
    private const uint NovaCCallInstructionSize = 5u;
    private const int SourceStepBudget = 100_000;
    private enum SourceStepMode
    {
        Into,
        Over,
        Out
    }
    private const string DefaultMainSource = """
#include "../include/nova.h"
int main(void) {
typedef Score LocalScore;
LocalScore values[2];
struct Pair pair = { .tag = { .byte = 'Z' }, .history = { 1, 2 } };
values[0] = 7;
values[1] = factorial(4);
pair.left = sum2(values);
pair.right = global_bias;
global_cursor = (int*)0;
putchar(NOVA_CONSOLE_CHAR);
return pair.left + pair.right + global_hits;
}
""";
    private const string DefaultMathSource = """
#include "../include/nova.h"
static int one(void) {
return 1;
}
int factorial(int n) {
typedef int LocalCount;
static volatile LocalCount calls = 0;
calls += 1;
global_hits = global_hits + 1;
if (n <= 1) {
return NOVA_FACTOR_BASE;
}
return n * factorial(n - 1);
}
int sum2(int* values) {
return NOVA_SUM(values[0], values[1]);
}

""";
    private const string DefaultStateSource = """
#include "../include/nova.h"
const Score global_bias = 3;
volatile Score global_hits;
int * restrict global_cursor;
""";
    private const string DefaultApiHeader = """
#ifndef NOVA_H
#define NOVA_H
#include "types.h"
#define NOVA_FACTOR_BASE 1
#define NOVA_CONSOLE_CHAR 'A'
#define NOVA_SUM(a, b) ((a) + (b))
int factorial(int n);
int sum2(int* values);
extern const Score global_bias;
extern volatile Score global_hits;
extern int * restrict global_cursor;
#endif
""";
    private const string DefaultTypesHeader = """
#ifndef NOVA_TYPES_H
#define NOVA_TYPES_H
typedef int Score;
union PairTag {
int whole;
char byte;
};
struct Pair {
int left;
int right;
union PairTag tag;
int history[2];
};
#endif
""";
    private readonly ObservableCollection<RegisterRow> _registers = new();
    private readonly ObservableCollection<DisassemblyRow> _disassembly = new();
    private readonly ObservableCollection<HeapBlockRow> _heapBlocks = new();
    private readonly ObservableCollection<SourceMapRow> _sourceMapRows = new();
    private readonly ObservableCollection<WatchRow> _watches = new();
    private readonly ObservableCollection<StackFrameRow> _callStack = new();
    private readonly ObservableCollection<ProjectSourceFile> _projectFiles = new();
    private NovaCompilerNative.SourceMapEntry[] _sourceMapEntries = Array.Empty<NovaCompilerNative.SourceMapEntry>();
    private NovaCompilerNative.DebugSymbol[] _debugSymbols = Array.Empty<NovaCompilerNative.DebugSymbol>();
    private NovaCompilerNative.DebugFunction[] _debugFunctions = Array.Empty<NovaCompilerNative.DebugFunction>();
    private NovaCompilerNative.DebugGlobal[] _debugGlobals = Array.Empty<NovaCompilerNative.DebugGlobal>();
    private NovaCompilerNative.SymbolReference[] _symbolReferences = Array.Empty<NovaCompilerNative.SymbolReference>();
    private NovaCompilerNative.IncludeDependency[] _includeDependencies = Array.Empty<NovaCompilerNative.IncludeDependency>();
    private NovaCompilerNative.DebugString[] _debugStrings = Array.Empty<NovaCompilerNative.DebugString>();
    private NovaCompilerNative.DeclarationMetadata[] _declarations = Array.Empty<NovaCompilerNative.DeclarationMetadata>();
    private NovaCompilerNative.MacroDefinition[] _macros = Array.Empty<NovaCompilerNative.MacroDefinition>();
    private nint _vm;
    private byte[] _demoProgram = Array.Empty<byte>();
    private uint? _selectedNativeFrameIndex;
    private bool _refreshingCallStackSelection;
    private ProjectSourceFile? _activeProjectFile;
    private bool _switchingProjectFile;
    private readonly NovaWorkspaceService _workspace = new();
    private NovaProjectManifest _projectManifest = new();
    private string? _projectManifestPath;
    private string? _projectRoot;
    private bool _manifestDirty;
    private bool _closingConfirmed;
    private bool _graphRuntimeOverlayEnabled = true;
    public MainWindow()
    {
        InitializeComponent();
        InitializeDefaultProject();
        ProjectFileList.ItemsSource = _projectFiles;
        RegisterList.ItemsSource = _registers;
        DisassemblyList.ItemsSource = _disassembly;

        HeapBlockList.ItemsSource = _heapBlocks;
        SourceMapList.ItemsSource = _sourceMapRows;
        WatchList.ItemsSource = _watches;
        CallStackList.ItemsSource = _callStack;
        ProjectFileList.SelectedIndex = 0;
        for (var i = 0; i < 16; i++)
            _registers.Add(new RegisterRow($"R{i}", "0x00000000"));
        ArchitectureGraph.NodeSelected += OnGraphNodeSelected;
        ArchitectureGraph.NodeActivated += OnGraphNodeActivated;
        ArchitectureGraph.NodeMoved += OnGraphNodeMoved;
        ArchitectureGraph.ZoomChanged += zoom => ZoomText.Text = $"{zoom * 100:0}% zoom";
        InitializeArchitectureGraph(preserveCurrentPositions: false);
        RefreshWorkspaceChrome();
        Loaded += OnLoaded;
        Closing += OnClosing;
        Closed += OnClosed;
    }
    private void InitializeDefaultProject()
    {
        _projectManifest = new NovaProjectManifest
        {
            Version = NovaWorkspaceService.ManifestVersion,
            Name = "Nova Project"
        };
        _projectManifest.Files.Add(new NovaProjectFileEntry { Path = "src/main.c" });
        _projectManifest.Files.Add(new NovaProjectFileEntry { Path = "src/math.c" });
        _projectManifest.Files.Add(new NovaProjectFileEntry { Path = "src/state.c" });
        _projectManifest.Files.Add(new NovaProjectFileEntry { Path = "include/nova.h" });
        _projectManifest.Files.Add(new NovaProjectFileEntry { Path = "include/types.h" });
        _projectFiles.Clear();
        _projectFiles.Add(new ProjectSourceFile(0u, "src/main.c", DefaultMainSource));
        _projectFiles.Add(new ProjectSourceFile(1u, "src/math.c", DefaultMathSource));
        _projectFiles.Add(new ProjectSourceFile(2u, "src/state.c", DefaultStateSource));
        _projectFiles.Add(new ProjectSourceFile(3u, "include/nova.h", DefaultApiHeader));
        _projectFiles.Add(new ProjectSourceFile(4u, "include/types.h", DefaultTypesHeader));
        foreach (var file in _projectFiles)
            file.Text += string.Empty;
        _projectManifestPath = null;
        _projectRoot = null;
        _manifestDirty = true;
    }
    private ProjectSourceFile? ProjectFile(uint fileId)
    => _projectFiles.FirstOrDefault(file => file.FileId == fileId);
    private void SyncActiveSourceFromEditor()
    {
        if (_activeProjectFile is not null && !_switchingProjectFile)
            _activeProjectFile.Text = SourceEditor.Text;
    }
    private void OpenProjectFile(ProjectSourceFile file, bool selectInList = true)
    {
        if (_activeProjectFile?.FileId == file.FileId) return;
        SyncActiveSourceFromEditor();
        _switchingProjectFile = true;
        _activeProjectFile = file;
        SourceEditor.Text = file.Text;

        SourceFileTitle.Text = $"{file.Name} · NovaC Vol.21 {(file.IsHeader ? "declaration header" : "translation unit")}";
        SourceTab.Header = $"SOURCE · {System.IO.Path.GetFileName(file.Name)}";
        if (selectInList && !ReferenceEquals(ProjectFileList.SelectedItem, file))
            ProjectFileList.SelectedItem = file;
        _switchingProjectFile = false;
    }
    private void OpenProjectFile(uint fileId, bool selectInList = true)
    {
        var file = ProjectFile(fileId);
        if (file is not null) OpenProjectFile(file, selectInList);
    }
    private void ProjectFileList_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (_switchingProjectFile || ProjectFileList.SelectedItem is not ProjectSourceFile file) return;
        OpenProjectFile(file, selectInList: false);
        SetStatus($"Opened {file.Name}.");
    }
    private void SourceEditor_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        if (_switchingProjectFile || _activeProjectFile is null) return;
        _activeProjectFile.Text = SourceEditor.Text;
        RefreshWorkspaceChrome();
    }
    private void OnGraphNodeMoved(GraphNode node)
    {
        _projectManifest.Graph[node.Id] = new NovaGraphPosition { X = node.X, Y = node.Y };
        _manifestDirty = true;
        RefreshWorkspaceChrome();
    }
    private void CaptureGraphPositions(bool markDirty)
    {
        var liveIds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var node in ArchitectureGraph.Nodes)
        {
            liveIds.Add(node.Id);
            _projectManifest.Graph[node.Id] = new NovaGraphPosition { X = node.X, Y = node.Y };
        }
        foreach (var stale in _projectManifest.Graph.Keys.Where(key => !liveIds.Contains(key)).ToArray())
            _projectManifest.Graph.Remove(stale);
        if (markDirty) _manifestDirty = true;
    }
    private GraphNode CreatePositionedNode(
    string id,
    string title,
    string subtitle,
    GraphNodeKind kind,
    double defaultX,
    double defaultY,
    double width = 180,
    double height = 82)
    {
        var x = defaultX;
        var y = defaultY;
        if (_projectManifest.Graph.TryGetValue(id, out var position))

        {
            x = position.X;
            y = position.Y;
        }
        return new GraphNode(id, title, subtitle, kind, x, y, width, height);
    }
    private bool HasUnsavedChanges()
    => _manifestDirty || _projectFiles.Any(file => file.IsDirty);
    private void RefreshWorkspaceChrome()
    {
        var dirty = HasUnsavedChanges();
        var name = string.IsNullOrWhiteSpace(_projectManifest.Name) ? "Nova Project" : _projectManifest.Name;
        WorkspaceTitleText.Text = $"{name}{(dirty ? " *" : string.Empty)}";
        WorkspacePathText.Text = _projectManifestPath is null
        ? " / unsaved workspace"
        : $" / {_projectManifestPath}";
        ProjectCardTitle.Text = $"◈ {name}";
        ProjectCardSubtitle.Text = _projectManifestPath is null
        ? $"C / NovaVM target · {_projectFiles.Count} files · unsaved"
        : $"C / NovaVM target · {_projectFiles.Count} files · {Path.GetFileName(_projectManifestPath)}";
        Title = $"Nova Studio — {name}{(dirty ? " *" : string.Empty)}";
    }
    private void ReindexProjectFiles()
    {
        for (var i = 0; i < _projectFiles.Count; i++)
            _projectFiles[i].ReassignFileId((uint)i);
    }
    private void SyncManifestFileEntries()
    {
        _projectManifest.Files.Clear();
        foreach (var file in _projectFiles)
            _projectManifest.Files.Add(new NovaProjectFileEntry { Path = file.Name });
    }
    private void PersistManifestStructure()
    {
        if (_projectManifestPath is null) return;
        SyncManifestFileEntries();
        _workspace.SaveManifest(_projectManifestPath, _projectManifest);
        _manifestDirty = false;
    }
    private void ApplyLoadedWorkspace(NovaWorkspaceService.LoadedWorkspace workspace)
    {
        _projectManifestPath = workspace.ManifestPath;
        _projectRoot = workspace.RootDirectory;
        _projectManifest = workspace.Manifest;
        _manifestDirty = false;
        _switchingProjectFile = true;
        _activeProjectFile = null;
        _projectFiles.Clear();
        foreach (var file in workspace.Files)
            _projectFiles.Add(file);
        ReindexProjectFiles();
        _switchingProjectFile = false;
        ClearSourceDebugViews();

        ProjectFileList.SelectedIndex = _projectFiles.Count > 0 ? 0 : -1;
        if (_projectFiles.Count > 0)
            OpenProjectFile(_projectFiles[0]);
        InitializeArchitectureGraph(preserveCurrentPositions: false);
        RefreshWorkspaceChrome();
    }
    private bool EnsureWorkspaceOnDisk()
    {
        if (_projectManifestPath is not null && _projectRoot is not null) return true;
        var dialog = new SaveFileDialog
        {
            Title = "Create Nova Project",
            Filter = "Nova Project (*.novaproj)|*.novaproj|All files (*.*)|*.*",
            FileName = "NovaProject.novaproj",
            AddExtension = true,
            DefaultExt = ".novaproj"
        };
        if (dialog.ShowDialog(this) != true) return false;
        SyncActiveSourceFromEditor();
        CaptureGraphPositions(markDirty: false);
        var initial = _projectFiles.Select(file => (file.Name, file.Text)).ToArray();
        var projectName = Path.GetFileNameWithoutExtension(dialog.FileName);
        var workspace = _workspace.Create(dialog.FileName, projectName, initial);
        workspace.Manifest.Graph = new Dictionary<string, NovaGraphPosition>(_projectManifest.Graph, StringComparer.Ordinal);
        _workspace.SaveManifest(workspace.ManifestPath, workspace.Manifest);
        ApplyLoadedWorkspace(workspace);
        SetStatus($"Created workspace {workspace.ManifestPath}.");
        return true;
    }
    private bool SaveAllWorkspace()
    {
        if (!EnsureWorkspaceOnDisk()) return false;
        SyncActiveSourceFromEditor();
        CaptureGraphPositions(markDirty: false);
        SyncManifestFileEntries();
        try
        {
            foreach (var file in _projectFiles)
                _workspace.SaveFile(_projectRoot!, file);
            _workspace.SaveManifest(_projectManifestPath!, _projectManifest);
            _manifestDirty = false;
            RefreshWorkspaceChrome();
            RecompileWorkspaceGraphAfterMutation("Save All complete");
            SetStatus($"Saved {_projectFiles.Count} source files and workspace manifest.");
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Nova Studio — Save failed", MessageBoxButton.OK, MessageBoxImage.Error);
            SetStatus($"Save failed: {ex.Message}");
            return false;
        }
    }
    private bool ConfirmWorkspaceSwitch()
    {

        if (!HasUnsavedChanges()) return true;
        var choice = MessageBox.Show(
        this,
        "The current Nova workspace has unsaved changes. Save them before continuing?",
        "Nova Studio",
        MessageBoxButton.YesNoCancel,
        MessageBoxImage.Warning);
        return choice switch
        {
            MessageBoxResult.Yes => SaveAllWorkspace(),
            MessageBoxResult.No => true,
            _ => false
        };
    }
    private void RecompileWorkspaceGraphAfterMutation(string reason)
    {
        if (_vm == 0) return;
        try
        {
            if (!CompileCurrentProject(loadProgram: true, clearConsole: true))
                SetStatus($"{reason}; semantic rebuild reported compiler diagnostics.");
        }
        catch (DllNotFoundException)
        {
            SetStatus($"{reason}; workspace updated, compiler DLL is unavailable for semantic rebuild.");
        }
    }
    private void NewProject_Click(object sender, RoutedEventArgs e)
    {
        if (!ConfirmWorkspaceSwitch()) return;
        var dialog = new SaveFileDialog
        {
            Title = "Create Nova Project",
            Filter = "Nova Project (*.novaproj)|*.novaproj|All files (*.*)|*.*",
            FileName = "NovaProject.novaproj",
            AddExtension = true,
            DefaultExt = ".novaproj"
        };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            var name = Path.GetFileNameWithoutExtension(dialog.FileName);
            var workspace = _workspace.Create(dialog.FileName, name, new[]
            {
("src/main.c", DefaultMainSource),
("src/math.c", DefaultMathSource),
("src/state.c", DefaultStateSource),
("include/nova.h", DefaultApiHeader),
("include/types.h", DefaultTypesHeader)
});
            ApplyLoadedWorkspace(workspace);
            RecompileWorkspaceGraphAfterMutation("Project created");
            SetStatus($"Created {workspace.ManifestPath}.");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Nova Studio — New Project", MessageBoxButton.OK, MessageBoxImage.Error);
        }

    }
    private void OpenProject_Click(object sender, RoutedEventArgs e)
    {
        if (!ConfirmWorkspaceSwitch()) return;
        var dialog = new OpenFileDialog
        {
            Title = "Open Nova Project",
            Filter = "Nova Project (*.novaproj)|*.novaproj|All files (*.*)|*.*"
        };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            var workspace = _workspace.Open(dialog.FileName);
            ApplyLoadedWorkspace(workspace);
            RecompileWorkspaceGraphAfterMutation("Project opened");
            SetStatus($"Opened {workspace.ManifestPath}.");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Nova Studio — Open Project", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
    private void Save_Click(object sender, RoutedEventArgs e)
    {
        if (!EnsureWorkspaceOnDisk()) return;
        SyncActiveSourceFromEditor();
        try
        {
            if (_activeProjectFile is not null)
                _workspace.SaveFile(_projectRoot!, _activeProjectFile);
            CaptureGraphPositions(markDirty: false);
            SyncManifestFileEntries();
            _workspace.SaveManifest(_projectManifestPath!, _projectManifest);
            _manifestDirty = false;
            RefreshWorkspaceChrome();
            RecompileWorkspaceGraphAfterMutation("Save complete");
            SetStatus(_activeProjectFile is null ? "Workspace manifest saved." : $"Saved {_activeProjectFile.Name}.");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Nova Studio — Save failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
    private void SaveAll_Click(object sender, RoutedEventArgs e) => SaveAllWorkspace();
    private void AddFile_Click(object sender, RoutedEventArgs e)
    {
        if (!EnsureWorkspaceOnDisk()) return;
        if (_projectFiles.Count >= NovaWorkspaceService.MaxFiles)
        {
            MessageBox.Show(this, $"NovaC currently supports at most {NovaWorkspaceService.MaxFiles} .c/.h project files.", "Nova Studio", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        var dialog = new SaveFileDialog
        {
            Title = "Add C Source or Header",

            Filter = "Nova C files (*.c;*.h)|*.c;*.h|C source (*.c)|*.c|C header (*.h)|*.h",
            AddExtension = true,
            DefaultExt = ".c",
            InitialDirectory = _projectRoot,
            FileName = "module.c"
        };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            var relative = _workspace.ToRelativeProjectPath(_projectRoot!, dialog.FileName);
            relative = NovaWorkspaceService.NormalizeRelativePath(relative);
            if (_projectFiles.Any(file => string.Equals(file.Name, relative, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException($"{relative} is already part of this project.");
            _workspace.ResolveProjectPath(_projectRoot!, relative);
            var isHeader = relative.EndsWith(".h", StringComparison.OrdinalIgnoreCase);
            var text = File.Exists(dialog.FileName)
            ? File.ReadAllText(dialog.FileName, Encoding.UTF8)
            : isHeader ? "/* Nova declaration header */\n" : "int module(void) {\n return 0;\n}\n";
            if (!File.Exists(dialog.FileName)) File.WriteAllText(dialog.FileName, text, new UTF8Encoding(false));
            var file = new ProjectSourceFile((uint)_projectFiles.Count, relative, text);
            _projectFiles.Add(file);
            SyncManifestFileEntries();
            _manifestDirty = true;
            PersistManifestStructure();
            ProjectFileList.SelectedItem = file;
            OpenProjectFile(file);
            InitializeArchitectureGraph();
            RecompileWorkspaceGraphAfterMutation($"Added {relative}");
            RefreshWorkspaceChrome();
            SetStatus($"Added {relative}.");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Nova Studio — Add File", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
    private void RenameFile_Click(object sender, RoutedEventArgs e)
    {
        if (ProjectFileList.SelectedItem is not ProjectSourceFile file || !EnsureWorkspaceOnDisk()) return;
        var newName = TextPromptWindow.Show(this, "Rename Project File", "Project-relative .c/.h path:", file.Name);
        if (string.IsNullOrWhiteSpace(newName) || string.Equals(newName, file.Name, StringComparison.Ordinal)) return;
        if (!file.IsHeader && newName.EndsWith(".h", StringComparison.OrdinalIgnoreCase) &&
        _projectFiles.Count(candidate => !candidate.IsHeader) <= 1)
        {
            MessageBox.Show(this, "A Nova project must retain at least one .c translation unit.", "Nova Studio", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        try
        {
            SyncActiveSourceFromEditor();
            if (file.IsDirty) _workspace.SaveFile(_projectRoot!, file);
            var oldNodeId = file.NodeId;
            _workspace.RenameFile(_projectRoot!, file, newName, _projectManifest);
            var newNodeId = file.NodeId;
            if (_projectManifest.Graph.Remove(oldNodeId, out var position))
                _projectManifest.Graph[newNodeId] = position;
            SyncManifestFileEntries();
            _manifestDirty = true;
            PersistManifestStructure();

            if (ReferenceEquals(_activeProjectFile, file))
            {
                _activeProjectFile = null;
                OpenProjectFile(file);
            }
            InitializeArchitectureGraph();
            RecompileWorkspaceGraphAfterMutation($"Renamed project file to {file.Name}");
            RefreshWorkspaceChrome();
            SetStatus($"Renamed project file to {file.Name}.");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Nova Studio — Rename File", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
    private void RemoveFile_Click(object sender, RoutedEventArgs e)
    {
        if (ProjectFileList.SelectedItem is not ProjectSourceFile file || !EnsureWorkspaceOnDisk()) return;
        if (_projectFiles.Count <= 1 || (!file.IsHeader && _projectFiles.Count(candidate => !candidate.IsHeader) <= 1))
        {
            MessageBox.Show(this, "A Nova project must keep at least one .c translation unit.", "Nova Studio", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        var choice = MessageBox.Show(
        this,
        $"Remove {file.Name}?\n\nYes: remove and delete the file from disk.\nNo: remove from the project only.",
        "Nova Studio — Remove File",
        MessageBoxButton.YesNoCancel,
        MessageBoxImage.Warning);
        if (choice == MessageBoxResult.Cancel) return;
        try
        {
            SyncActiveSourceFromEditor();
            var deleteAbsolute = choice == MessageBoxResult.Yes
            ? _workspace.ResolveProjectPath(_projectRoot!, file.Name)
            : null;
            _projectManifest.Graph.Remove(file.NodeId);
            var index = _projectFiles.IndexOf(file);
            _projectFiles.Remove(file);
            ReindexProjectFiles();
            SyncManifestFileEntries();
            _manifestDirty = true;
            PersistManifestStructure();
            if (deleteAbsolute is not null && File.Exists(deleteAbsolute)) File.Delete(deleteAbsolute);
            _activeProjectFile = null;
            var next = _projectFiles[Math.Clamp(index, 0, _projectFiles.Count - 1)];
            ProjectFileList.SelectedItem = next;
            OpenProjectFile(next);
            ClearSourceDebugViews();
            InitializeArchitectureGraph(preserveCurrentPositions: false);
            RecompileWorkspaceGraphAfterMutation($"Removed {file.Name}");
            RefreshWorkspaceChrome();
            SetStatus($"Removed {file.Name} from the project.");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Nova Studio — Remove File", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void InitializeArchitectureGraph(bool preserveCurrentPositions = true)
    {
        if (preserveCurrentPositions && ArchitectureGraph.Nodes.Count > 0)
            CaptureGraphPositions(markDirty: false);
        var nodes = new List<GraphNode>
{
CreatePositionedNode("project", _projectManifest.Name, $"{_projectFiles.Count(file => !file.IsHeader)} sources / {_projectFiles.Count(file => file.IsHeader)} headers / persistent semantic graph", GraphNodeKind.Project, 40, 250, 220, 92),
CreatePositionedNode("studio", "Nova Studio", "persistent architecture-first workbench", GraphNodeKind.Bridge, 760, 35, 220, 82),
CreatePositionedNode("compiler", "NovaC Frontend", $"refs {_symbolReferences.Length} / includes {_includeDependencies.Length} / macros {_macros.Length} / decls {_declarations.Length} / strings {_debugStrings.Length}", GraphNodeKind.Compiler, 760, 170, 220, 88),
CreatePositionedNode("vm", "NovaVM", "32-bit language-neutral runtime", GraphNodeKind.Runtime, 1050, 150, 120, 120),
CreatePositionedNode("cpu", "CPU", "R0-R15 · PC · SP", GraphNodeKind.Cpu, 1058, 320, 104, 104),
CreatePositionedNode("memory", "Memory", "1 MiB + static data @ 0x000E0000", GraphNodeKind.Memory, 1025, 470, 190, 76),
CreatePositionedNode("mmio", "MMIO Console", "0x000F0000", GraphNodeKind.Io, 1025, 585, 190, 76)
};
        var edges = new List<GraphEdge>
{
new("project", "compiler", "project compile"),
new("compiler", "vm", "Nova bytecode"),
new("vm", "studio", "runtime/debug"),
new("vm", "cpu"),
new("vm", "memory"),
new("vm", "mmio")
};
        for (var i = 0; i < _projectFiles.Count; i++)
        {
            var file = _projectFiles[i];
            nodes.Add(CreatePositionedNode(
            file.NodeId,
            Path.GetFileName(file.Name),
            file.IsHeader ? $"{file.Name} · declarations" : file.Name,
            file.IsHeader ? GraphNodeKind.Header : GraphNodeKind.Source,
            40,
            55 + i * 115,
            220,
            78));
            edges.Add(new GraphEdge("project", file.NodeId, "contains"));
        }
        for (var i = 0; i < _debugFunctions.Length; i++)
        {
            var function = _debugFunctions[i];
            var file = ProjectFile(function.FileId);
            var fileLabel = Path.GetFileName(file?.Name ?? $"file#{function.FileId}");
            var functionDecl = FunctionDeclaration(i);
            var functionLinkage = DeclarationLinkageLabel(functionDecl);
            nodes.Add(CreatePositionedNode(
            FunctionNodeId(i),
            function.Name,
            $"{fileLabel}:{function.DeclarationLine} · {functionLinkage} · 0x{function.BytecodeStart:X4}",
            GraphNodeKind.Function,
            335,
            45 + i * 92,
            245,
            72));
            if (file is not null)
                edges.Add(new GraphEdge(file.NodeId, FunctionNodeId(i), "defines"));
        }
        for (var i = 0; i < _debugGlobals.Length; i++)

        {
            var global = _debugGlobals[i];
            var file = ProjectFile(global.FileId);
            var globalDecl = GlobalDeclaration(i);
            var globalLinkage = DeclarationLinkageLabel(globalDecl);
            var globalQualifiers = DeclarationQualifierSuffix(globalDecl);
            var globalTentative = declarationHasFlag(globalDecl, NovaCompilerNative.DeclFlagTentative) ? " · tentative" : string.Empty;
            nodes.Add(CreatePositionedNode(
            GlobalNodeId(i),
            global.Name,
            $"0x{global.Address:X8} · {globalLinkage}{globalQualifiers}{globalTentative} · {DebugGlobalTypeName(global)} · {global.ByteSize} B",
            GraphNodeKind.Global,
            620,
            300 + i * 86,
            225,
            70));
            if (file is not null)
                edges.Add(new GraphEdge(file.NodeId, GlobalNodeId(i), "global"));
        }
        var typedefDeclarations = _declarations.Where(declaration => declaration.Kind == NovaCompilerNative.DeclTypedef).ToArray();
        for (var i = 0; i < typedefDeclarations.Length; i++)
        {
            var alias = typedefDeclarations[i];
            var file = ProjectFile(alias.FileId);
            var fileLabel = Path.GetFileName(file?.Name ?? $"file#{alias.FileId}");
            var qualifierLabel = DeclarationQualifierPrefix(alias);
            nodes.Add(CreatePositionedNode(
            TypedefNodeId(alias),
            alias.Name,
            $"{fileLabel}:{alias.Line} · typedef {qualifierLabel}{DeclarationTypeName(alias.Type)}{(declarationHasFlag(alias, NovaCompilerNative.DeclFlagBlockScope) ? " · block" : string.Empty)}",
            GraphNodeKind.TypeAlias,
            620,
            300 + (_debugGlobals.Length + i) * 86,
            225,
            70));
            if (file is not null) edges.Add(new GraphEdge(file.NodeId, TypedefNodeId(alias), "typedef"));
        }
        var staticLocalDeclarations = _declarations.Where(declaration => declaration.Kind == NovaCompilerNative.DeclStaticLocal).ToArray();
        for (var i = 0; i < staticLocalDeclarations.Length; i++)
        {
            var local = staticLocalDeclarations[i];
            var file = ProjectFile(local.FileId);
            var fileLabel = Path.GetFileName(file?.Name ?? $"file#{local.FileId}");
            nodes.Add(CreatePositionedNode(
            StaticLocalNodeId(local),
            local.Name,
            $"{fileLabel}:{local.Line} · local static{DeclarationQualifierSuffix(local)} · {DeclarationTypeName(local.Type)}",
            GraphNodeKind.Global,
            620,
            300 + (_debugGlobals.Length + typedefDeclarations.Length + staticLocalDeclarations.Length + i) * 86,
            225,
            70));
            if (file is not null) edges.Add(new GraphEdge(file.NodeId, StaticLocalNodeId(local), "static local"));
        }
        for (var i = 0; i < _macros.Length; i++)
        {
            var macro = _macros[i];
            var file = ProjectFile(macro.FileId);
            var fileLabel = Path.GetFileName(file?.Name ?? $"file#{macro.FileId}");
            var signature = macro.Kind == NovaCompilerNative.MacroFunction ? $"({macro.ParameterCount} params)" : "object";
            var replacement = string.IsNullOrWhiteSpace(macro.Replacement) ? "<empty>" : macro.Replacement;
            nodes.Add(CreatePositionedNode(
            MacroNodeId(macro),
            macro.Name,
            $"{fileLabel}:{macro.Line} · macro {signature} · {replacement}",
            GraphNodeKind.Macro,
            620,
            300 + (_debugGlobals.Length + typedefDeclarations.Length + staticLocalDeclarations.Length + i) * 86,
            245,
            70));
            if (file is not null) edges.Add(new GraphEdge(file.NodeId, MacroNodeId(macro), "defines"));
        }
        for (var i = 0; i < _debugStrings.Length; i++)
        {
            var literal = _debugStrings[i];
            var file = ProjectFile(literal.FileId);
            var preview = string.IsNullOrEmpty(literal.Preview) ? "string literal" : $"\"{literal.Preview}\"";
            nodes.Add(CreatePositionedNode(
            StringNodeId(i),
            preview,
            $"0x{literal.Address:X8} · {literal.ByteSize} B · line {literal.Line}",
            GraphNodeKind.Memory,
            620,
            300 + (_debugGlobals.Length + typedefDeclarations.Length + i) * 86,
            225,
            70));
            if (file is not null) edges.Add(new GraphEdge(file.NodeId, StringNodeId(i), "static string"));
        }
        var dependencyKeys = new HashSet<string>(StringComparer.Ordinal);
        foreach (var include in _includeDependencies)
        {
            var fromFile = ProjectFile(include.FromFileId);
            var toFile = ProjectFile(include.ToFileId);
            if (fromFile is null || toFile is null) continue;
            edges.Add(new GraphEdge(fromFile.NodeId, toFile.NodeId, "includes"));
            dependencyKeys.Add($"{fromFile.FileId}>{toFile.FileId}");
        }
        foreach (var reference in _symbolReferences)
        {
            if (reference.FunctionIndex >= (uint)_debugFunctions.Length) continue;
            var fromFunction = FunctionNodeId((int)reference.FunctionIndex);
            string? targetNode = null;
            string label;
            if (reference.Kind == NovaCompilerNative.RefFunctionCall && reference.TargetIndex < (uint)_debugFunctions.Length)
            {
                targetNode = FunctionNodeId((int)reference.TargetIndex);
                label = "calls";
            }
            else if ((reference.Kind == NovaCompilerNative.RefGlobalRead || reference.Kind == NovaCompilerNative.RefGlobalWrite) &&
            reference.TargetIndex < (uint)_debugGlobals.Length)
            {
                targetNode = GlobalNodeId((int)reference.TargetIndex);
                label = reference.Kind == NovaCompilerNative.RefGlobalWrite ? "writes" : "reads";
            }

            else continue;
            edges.Add(new GraphEdge(fromFunction, targetNode, label));
            var fromFile = ProjectFile(reference.FileId);
            var targetFile = ProjectFile(reference.TargetFileId);
            if (fromFile is not null && targetFile is not null && fromFile.FileId != targetFile.FileId)
            {
                var key = $"{fromFile.FileId}>{targetFile.FileId}";
                if (dependencyKeys.Add(key))
                    edges.Add(new GraphEdge(fromFile.NodeId, targetFile.NodeId, "depends"));
            }
        }
        ArchitectureGraph.SetGraph(nodes, edges);
    }
    private NovaCompilerNative.DeclarationMetadata? FunctionDeclaration(int index)
    {
        foreach (var declaration in _declarations)
        {
            if (declaration.Kind == NovaCompilerNative.DeclFunction && declaration.TargetIndex == (uint)index)
                return declaration;
        }
        return null;
    }
    private NovaCompilerNative.DeclarationMetadata? GlobalDeclaration(int index)
    {
        foreach (var declaration in _declarations)
        {
            if (declaration.Kind == NovaCompilerNative.DeclGlobal && declaration.TargetIndex == (uint)index)
                return declaration;
        }
        return null;
    }
    private string StableFileName(uint fileId)
    => ProjectFile(fileId)?.Name ?? $"file#{fileId}";
    private string FunctionNodeId(int index)
    {
        if (index < 0 || index >= _debugFunctions.Length) return $"function:#{index}";
        var declaration = FunctionDeclaration(index);
        return declaration is { } functionDecl && functionDecl.Linkage == NovaCompilerNative.LinkageInternal
        ? $"function:{StableFileName(functionDecl.FileId)}:{_debugFunctions[index].Name}"
        : $"function:{_debugFunctions[index].Name}";
    }
    private string GlobalNodeId(int index)
    {
        if (index < 0 || index >= _debugGlobals.Length) return $"global:#{index}";
        var declaration = GlobalDeclaration(index);
        return declaration is { } globalDecl && globalDecl.Linkage == NovaCompilerNative.LinkageInternal
        ? $"global:{StableFileName(globalDecl.FileId)}:{_debugGlobals[index].Name}"
        : $"global:{_debugGlobals[index].Name}";
    }
    private string TypedefNodeId(NovaCompilerNative.DeclarationMetadata declaration)
    => declarationHasFlag(declaration, NovaCompilerNative.DeclFlagBlockScope)
    ? $"typedef:{StableFileName(declaration.FileId)}:{declaration.Line}:{declaration.Column}:{declaration.Name}"
    : $"typedef:{StableFileName(declaration.FileId)}:{declaration.Name}";
    private string StaticLocalNodeId(NovaCompilerNative.DeclarationMetadata declaration)
    => $"static-local:{StableFileName(declaration.FileId)}:{declaration.Line}:{declaration.Column}:{declaration.Name}";
    private string MacroNodeId(NovaCompilerNative.MacroDefinition macro)
    => $"macro:{StableFileName(macro.FileId)}:{macro.Line}:{macro.Column}:{macro.Name}";
    private string StringNodeId(int index)
    => index >= 0 && index < _debugStrings.Length
    ? $"string:{_debugStrings[index].FileId}:{_debugStrings[index].Line}:{_debugStrings[index].Column}"
    : $"string:#{index}";
    private void OnGraphNodeSelected(GraphNode? node)
    {
        SelectedNodeTitle.Text = node?.Title ?? "Nothing selected";
        SelectedNodeSubtitle.Text = node?.Subtitle ?? "Drag the background to pan the architecture graph.";
        if (node is null) return;
        if (node.Id.StartsWith("source:", StringComparison.Ordinal))
        {
            var name = node.Id["source:".Length..];
            var file = _projectFiles.FirstOrDefault(candidate => string.Equals(candidate.Name, name, StringComparison.Ordinal));
            if (file is not null) OpenProjectFile(file);
            return;
        }
        if (node.Id.StartsWith("function:", StringComparison.Ordinal))
        {
            for (var i = 0; i < _debugFunctions.Length; i++)
            {
                if (!string.Equals(FunctionNodeId(i), node.Id, StringComparison.Ordinal)) continue;
                var function = _debugFunctions[i];
                var declaration = FunctionDeclaration(i);
                OpenProjectFile(function.FileId);
                HighlightSourceLine(function.DeclarationLine);
                SetStatus($"Function {function.Name} · {DeclarationLinkageLabel(declaration)} · file {function.FileId} · line {function.DeclarationLine}.");
                break;
            }
            return;
        }
        if (node.Id.StartsWith("global:", StringComparison.Ordinal))
        {
            for (var i = 0; i < _debugGlobals.Length; i++)
            {
                if (!string.Equals(GlobalNodeId(i), node.Id, StringComparison.Ordinal)) continue;
                var global = _debugGlobals[i];
                var declaration = GlobalDeclaration(i);
                OpenProjectFile(global.FileId);
                HighlightSourceLine(global.DeclarationLine);
                SetStatus($"Global {global.Name} · {DeclarationLinkageLabel(declaration)} @ 0x{global.Address:X8} · {DebugGlobalTypeName(global)} · {global.ByteSize} B.");
                break;
            }
            return;
        }
        if (node.Id.StartsWith("typedef:", StringComparison.Ordinal))
        {
            foreach (var declaration in _declarations)
            {
                if (declaration.Kind != NovaCompilerNative.DeclTypedef || !string.Equals(TypedefNodeId(declaration), node.Id, StringComparison.Ordinal)) continue;
                OpenProjectFile(declaration.FileId);
                HighlightSourceLine(declaration.Line);
                SetStatus($"Typedef {declaration.Name} · {DeclarationTypeName(declaration.Type)} · file {declaration.FileId}:{declaration.Line}.");
                break;
            }
            return;
        }
        if (node.Id.StartsWith("static-local:", StringComparison.Ordinal))
        {
            foreach (var declaration in _declarations)
            {
                if (declaration.Kind != NovaCompilerNative.DeclStaticLocal || !string.Equals(StaticLocalNodeId(declaration), node.Id, StringComparison.Ordinal)) continue;
                OpenProjectFile(declaration.FileId);
                HighlightSourceLine(declaration.Line);
                SetStatus($"Local static {declaration.Name} · {DeclarationQualifierPrefix(declaration)}{DeclarationTypeName(declaration.Type)} · file {declaration.FileId}:{declaration.Line}.");
                break;
            }
            return;
        }
        if (node.Id.StartsWith("macro:", StringComparison.Ordinal))
        {
            foreach (var macro in _macros)
            {
                if (!string.Equals(MacroNodeId(macro), node.Id, StringComparison.Ordinal)) continue;
                OpenProjectFile(macro.FileId);
                HighlightSourceLine(macro.Line);
                var kind = macro.Kind == NovaCompilerNative.MacroFunction ? $"function-like/{macro.ParameterCount}" : "object-like";
                SetStatus($"Macro {macro.Name} · {kind} · file {macro.FileId}:{macro.Line} · {macro.Replacement}");
                break;
            }
            return;
        }
        if (node.Id.StartsWith("string:", StringComparison.Ordinal))
        {
            for (var i = 0; i < _debugStrings.Length; i++)
            {
                if (!string.Equals(StringNodeId(i), node.Id, StringComparison.Ordinal)) continue;
                var literal = _debugStrings[i];
                OpenProjectFile(literal.FileId);
                HighlightSourceLine(literal.Line);
                SetStatus($"String static data @ 0x{literal.Address:X8} · {literal.ByteSize} B · file {literal.FileId}:{literal.Line}.");
                break;
            }
        }
    }
    private void OnGraphNodeActivated(GraphNode node)
    {
        OnGraphNodeSelected(node);
        if (node.Kind is GraphNodeKind.Source or GraphNodeKind.Header or GraphNodeKind.Function or GraphNodeKind.Global or GraphNodeKind.TypeAlias or GraphNodeKind.Macro)
        {
            SourceTab.IsSelected = true;
            SetStatus($"Opened source context for {node.Title} from the architecture graph.");
            return;
        }
        if (node.Id == "memory")
            SetStatus("Memory node is live; use WATCH and HEAP in the runtime inspector for current VM-backed values.");
        else if (node.Id == "vm" || node.Id == "cpu" || node.Id == "mmio")
            SetStatus($"{node.Title} is a live NovaVM runtime entity; runtime overlay follows actual execution state.");
    }
    private void GraphMode_Click(object sender, RoutedEventArgs e)
    {
        _graphRuntimeOverlayEnabled = !_graphRuntimeOverlayEnabled;
        ArchitectureGraph.RuntimeOverlayEnabled = _graphRuntimeOverlayEnabled;
        GraphModeButton.Content = _graphRuntimeOverlayEnabled ? "Runtime overlay: ON" : "Runtime overlay: OFF";
        if (!_graphRuntimeOverlayEnabled)
            GraphRuntimeText.Text = "ARCHITECTURE · semantic graph";
        else if (_vm != 0)
            RefreshGraphRuntimeState(NovaNative.nova_vm_get_pc(_vm), NovaNative.nova_vm_get_last_opcode(_vm), mmioActivity: false);
        SetStatus(_graphRuntimeOverlayEnabled
        ? "Runtime overlay enabled; graph glow and call edges follow compiler/VM source-of-truth state."
        : "Architecture mode enabled; live runtime overlay is hidden, semantic graph remains unchanged.");
    }
    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            _vm = NovaNative.nova_vm_create();
            if (_vm == 0)
            {
                SetStatus("NovaVM allocation failed.");
                return;
            }
            if (_activeProjectFile is null && _projectFiles.Count > 0)
                OpenProjectFile(_projectFiles[0]);
            try
            {
                if (!CompileCurrentProject(loadProgram: true, clearConsole: true))
                    return;
                SetStatus($"NovaVM connected. {NovaCompilerNative.Version} compiled {_projectFiles.Count} translation units successfully.");
            }
            catch (DllNotFoundException)
            {
                _demoProgram = BuildConsoleProgram("NOVA VM ONLINE\n");
                PopulateDisassembly();
                LoadDemoProgram(clearConsole: true);
                CompileResultText.Text = "nova_compiler.dll missing";
                SetDiagnostic("Compiler dependency missing; Studio is running the NovaVM fallback bytecode demo only.", select: true);
                SetStatus("NovaVM connected; compiler DLL not found, using fallback bytecode demo.");
            }
        }
        catch (DllNotFoundException)
        {
            SetDiagnostic("nova_vm.dll not found. Build the root CMake project; NovaStudio.csproj expects ..\\build\\nova_vm.dll and copies it beside NovaStudio.exe when present.", select: true);
            SetStatus("nova_vm.dll not found. Build native/NovaVM first and copy the DLL beside NovaStudio.exe.");
        }
        catch (Exception ex)
        {
            SetStatus($"Runtime initialization failed: {ex.Message}");
        }
    }

    private void OnClosing(object? sender, CancelEventArgs e)
    {
        if (_closingConfirmed || !HasUnsavedChanges()) return;
        var choice = MessageBox.Show(
        this,
        "The current Nova workspace has unsaved changes. Save before closing?",
        "Nova Studio",
        MessageBoxButton.YesNoCancel,
        MessageBoxImage.Warning);
        if (choice == MessageBoxResult.Cancel)
        {
            e.Cancel = true;
            return;
        }
        if (choice == MessageBoxResult.Yes && !SaveAllWorkspace())
        {
            e.Cancel = true;
            return;
        }
        _closingConfirmed = true;
    }
    private void OnClosed(object? sender, EventArgs e)
    {
        if (_vm == 0) return;
        NovaNative.nova_vm_destroy(_vm);
        _vm = 0;
    }
    private void Compile_Click(object sender, RoutedEventArgs e)
    {
        if (!EnsureVm()) return;
        try
        {
            if (CompileCurrentProject(loadProgram: true, clearConsole: true))
                SetStatus("Compile succeeded. Bytecode loaded at 0x00000000.");
        }
        catch (DllNotFoundException)
        {
            CompileResultText.Text = "compiler DLL missing";
            SetDiagnostic("nova_compiler.dll not found. Build the root CMake project; NovaStudio.csproj copies ..\\build\\nova_compiler.dll beside NovaStudio.exe when present.", select: true);
            SetStatus("nova_compiler.dll not found. Build the root CMake project and copy the DLL beside NovaStudio.exe.");
        }
    }
    private bool CompileCurrentProject(bool loadProgram, bool clearConsole)
    {
        SyncActiveSourceFromEditor();
        var buffer = new byte[checked((int)NovaCompilerNative.MaxBytecode)];
        var sourceMap = new NovaCompilerNative.SourceMapEntry[checked((int)NovaCompilerNative.MaxSourceMap)];
        var symbols = new NovaCompilerNative.DebugSymbol[checked((int)NovaCompilerNative.MaxDebugSymbols)];
        var functions = new NovaCompilerNative.DebugFunction[checked((int)NovaCompilerNative.MaxFunctions)];
        var globals = new NovaCompilerNative.DebugGlobal[checked((int)NovaCompilerNative.MaxGlobals)];
        var references = new NovaCompilerNative.SymbolReference[checked((int)NovaCompilerNative.MaxReferences)];
        var includes = new NovaCompilerNative.IncludeDependency[checked((int)NovaCompilerNative.MaxIncludeDependencies)];
        var debugStrings = new NovaCompilerNative.DebugString[checked((int)NovaCompilerNative.MaxDebugStrings)];
        var declarations = new NovaCompilerNative.DeclarationMetadata[checked((int)NovaCompilerNative.MaxDeclarations)];
        var macros = new NovaCompilerNative.MacroDefinition[checked((int)NovaCompilerNative.MaxMacros)];
        var units = _projectFiles
        .Select(file => new NovaCompilerNative.TranslationUnit { Name = file.Name, Source = file.Text })
        .ToArray();

        var status = NovaCompilerNative.nova_compile_project_debug_ex5(
        units,
        (uint)units.Length,
        buffer,
        (uint)buffer.Length,
        out var result,
        out var diagnostic,
        sourceMap,
        (uint)sourceMap.Length,
        out var sourceMapCount,
        symbols,
        (uint)symbols.Length,
        out var symbolCount,
        functions,
        (uint)functions.Length,
        out var functionCount,
        globals,
        (uint)globals.Length,
        out var globalCount,
        references,
        (uint)references.Length,
        out var referenceCount,
        includes,
        (uint)includes.Length,
        out var includeCount,
        debugStrings,
        (uint)debugStrings.Length,
        out var debugStringCount,
        declarations,
        (uint)declarations.Length,
        out var declarationCount,
        macros,
        (uint)macros.Length,
        out var macroCount);
        if (status != NovaCompilerNative.Ok)
        {
            var errorFile = ProjectFile(diagnostic.FileId);
            var fileName = errorFile?.Name ?? $"file#{diagnostic.FileId}";
            if (errorFile is not null)
            {
                OpenProjectFile(errorFile);
                HighlightSourceLine(diagnostic.Line);
                ArchitectureGraph.ActiveNodeId = errorFile.NodeId;
            }
            else
            {
                ArchitectureGraph.ActiveNodeId = "compiler";
            }
            CompileResultText.Text = $"error {status} · {System.IO.Path.GetFileName(fileName)}:{diagnostic.Line}:{diagnostic.Column}";
            SetDiagnostic($"{fileName}:{diagnostic.Line}:{diagnostic.Column}\nerror {status}: {diagnostic.Message}", select: true);
            SetStatus($"Compile error {status} in {fileName}:{diagnostic.Line}:{diagnostic.Column}: {diagnostic.Message}");
            ClearSourceDebugViews();
            return false;
        }
        _demoProgram = buffer.AsSpan(0, checked((int)result.BytecodeSize)).ToArray();
        _sourceMapEntries = sourceMap.AsSpan(0, checked((int)sourceMapCount)).ToArray();
        _debugSymbols = symbols.AsSpan(0, checked((int)symbolCount)).ToArray();
        _debugFunctions = functions.AsSpan(0, checked((int)functionCount)).ToArray();
        _debugGlobals = globals.AsSpan(0, checked((int)globalCount)).ToArray();
        _symbolReferences = references.AsSpan(0, checked((int)referenceCount)).ToArray();
        _includeDependencies = includes.AsSpan(0, checked((int)includeCount)).ToArray();
        _debugStrings = debugStrings.AsSpan(0, checked((int)debugStringCount)).ToArray();
        _declarations = declarations.AsSpan(0, checked((int)declarationCount)).ToArray();
        _macros = macros.AsSpan(0, checked((int)macroCount)).ToArray();
        CompileResultText.Text = $"{units.Length} files · {result.BytecodeSize} B · funcs {functionCount} · globals {globalCount} · decls {declarationCount} · macros {macroCount} · refs {referenceCount} · includes {includeCount} · strings {debugStringCount}";
        SetDiagnostic($"No compiler diagnostics.\n{units.Length} project files · {result.BytecodeSize} byte image · {functionCount} functions · {globalCount} globals.");
        PopulateDisassembly();

        PopulateSourceDebugViews();
        InitializeArchitectureGraph();
        ArchitectureGraph.ActiveNodeId = "compiler";
        if (loadProgram)
        {
            NovaNative.nova_vm_clear_breakpoints(_vm);
            RefreshBreakpointVisuals();
            LoadDemoProgram(clearConsole);
        }
        return true;
    }
    private void CenterGraph_Click(object sender, RoutedEventArgs e) => ArchitectureGraph.ResetView();
    private void Reset_Click(object sender, RoutedEventArgs e)
    {
        if (!EnsureVm()) return;
        LoadDemoProgram(clearConsole: true);
        SetStatus("Runtime reset to the most recently compiled bytecode. Breakpoints preserved.");
    }
    private void Step_Click(object sender, RoutedEventArgs e)
    {
        if (!EnsureVm()) return;
        var status = NovaNative.nova_vm_step(_vm);
        var mmioActivity = DrainEvents();
        RefreshSnapshot(mmioActivity);
        SetStatus(status switch
        {
            NovaNative.Halted => "Program halted.",
            < 0 => $"VM fault: {status}",
            _ => "Executed one instruction."
        });
    }
    private void StepSource_Click(object sender, RoutedEventArgs e) => ExecuteSourceStep(SourceStepMode.Into);
    private void StepOver_Click(object sender, RoutedEventArgs e) => ExecuteSourceStep(SourceStepMode.Over);
    private void StepOut_Click(object sender, RoutedEventArgs e) => ExecuteSourceStep(SourceStepMode.Out);
    private void ExecuteSourceStep(SourceStepMode mode)
    {
        if (!EnsureVm()) return;
        var startPc = NovaNative.nova_vm_get_pc(_vm);
        var startRow = FindSourceRow(startPc);
        var startLine = startRow?.Line ?? 0u;
        var startFile = startRow?.FileId ?? uint.MaxValue;
        var startFunction = startRow?.FunctionIndex ?? uint.MaxValue;
        var startDepth = NovaNative.nova_vm_get_call_frame_count(_vm);
        var status = NovaNative.Ok;
        if (mode == SourceStepMode.Out && startDepth == 0u)
        {
            SetStatus("Step Out is unavailable outside a function frame.");

            return;
        }
        for (var i = 0; i < SourceStepBudget; i++)
        {
            var beforePc = NovaNative.nova_vm_get_pc(_vm);
            if (i > 0 && NovaNative.nova_vm_has_breakpoint(_vm, beforePc) != 0)
            {
                status = NovaNative.nova_vm_run(_vm, 1u);
                break;
            }
            status = NovaNative.nova_vm_step(_vm);
            if (status == NovaNative.Halted || status < 0) break;
            var currentDepth = NovaNative.nova_vm_get_call_frame_count(_vm);
            var currentRow = FindSourceRow(NovaNative.nova_vm_get_pc(_vm));
            if (mode == SourceStepMode.Out)
            {
                if (currentDepth < startDepth) break;
                continue;
            }
            if (mode == SourceStepMode.Over && currentDepth > startDepth)
                continue;
            if (currentDepth < startDepth) break;
            if (currentRow is null) continue;
            if (startLine == 0u || currentRow.Line != startLine || currentRow.FileId != startFile || currentRow.FunctionIndex != startFunction)
                break;
        }
        var mmioActivity = DrainEvents();
        RefreshSnapshot(mmioActivity);
        var label = mode switch
        {
            SourceStepMode.Into => "Step Into",
            SourceStepMode.Over => "Step Over",
            _ => "Step Out"
        };
        SetStatus(status switch
        {
            NovaNative.Breakpoint => $"{label} paused at breakpoint 0x{NovaNative.nova_vm_get_pc(_vm):X8}.",
            NovaNative.Halted => $"{label} reached program halt.",
            < 0 => $"VM fault during {label}: {status}",
            _ => $"{label} completed from line {(startLine == 0u ? "?" : startLine.ToString(CultureInfo.InvariantCulture))}."
        });
    }
    private void Run_Click(object sender, RoutedEventArgs e)
    {
        if (!EnsureVm()) return;
        var status = NovaNative.nova_vm_run(_vm, 100_000);
        var mmioActivity = DrainEvents();
        RefreshSnapshot(mmioActivity);
        SetStatus(status switch
        {

            NovaNative.Breakpoint => $"Paused at breakpoint 0x{NovaNative.nova_vm_get_pc(_vm):X8}.",
            NovaNative.Halted => "Program completed.",
            < 0 => $"VM fault: {status}",
            _ => "Instruction budget exhausted."
        });
    }
    private void DisassemblyList_MouseDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (DisassemblyList.SelectedItem is not DisassemblyRow row)
            return;
        ToggleBreakpointAtAddress(row.Address);
    }
    private void SourceMapList_MouseDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (SourceMapList.SelectedItem is not SourceMapRow row || !EnsureVm())
            return;
        var sameLine = _sourceMapRows.Where(candidate => candidate.FileId == row.FileId && candidate.Line == row.Line && candidate.FunctionIndex == row.FunctionIndex).ToArray();
        var shouldAdd = sameLine.All(candidate => NovaNative.nova_vm_has_breakpoint(_vm, candidate.StartAddress) == 0);
        var changed = 0;
        foreach (var candidate in sameLine)
        {
            var result = shouldAdd
            ? NovaNative.nova_vm_add_breakpoint(_vm, candidate.StartAddress)
            : NovaNative.nova_vm_remove_breakpoint(_vm, candidate.StartAddress);
            if (result >= 0) changed++;
        }
        RefreshBreakpointVisuals();
        SetStatus(shouldAdd
        ? $"Source breakpoint set on {row.FileName}:{row.Line} ({changed} mapped address{(changed == 1 ? string.Empty : "es")})."
        : $"Source breakpoint removed from {row.FileName}:{row.Line}.");
    }
    private void ToggleBreakpointAtAddress(uint address)
    {
        if (!EnsureVm()) return;
        var exists = NovaNative.nova_vm_has_breakpoint(_vm, address) != 0;
        var result = exists
        ? NovaNative.nova_vm_remove_breakpoint(_vm, address)
        : NovaNative.nova_vm_add_breakpoint(_vm, address);
        RefreshBreakpointVisuals();
        SetStatus(result < 0
        ? $"Breakpoint update failed: {result}"
        : exists
        ? $"Breakpoint removed from 0x{address:X8}."
        : $"Breakpoint set at 0x{address:X8}.");
    }
    private void RefreshBreakpointVisuals()
    {
        if (_vm == 0) return;
        foreach (var row in _disassembly)
            row.IsBreakpoint = NovaNative.nova_vm_has_breakpoint(_vm, row.Address) != 0;

        foreach (var row in _sourceMapRows)
            row.IsBreakpoint = NovaNative.nova_vm_has_breakpoint(_vm, row.StartAddress) != 0;
        BreakpointCountText.Text = NovaNative.nova_vm_get_breakpoint_count(_vm).ToString(CultureInfo.InvariantCulture);
        RefreshGraphBreakpointState();
    }
    private bool EnsureVm()
    {
        if (_vm != 0) return true;
        SetStatus("NovaVM is not connected.");
        return false;
    }
    private void PopulateDisassembly()
    {
        _disassembly.Clear();
        foreach (var row in BytecodeDisassembler.Decode(_demoProgram))
            _disassembly.Add(row);
    }
    private void ClearSourceDebugViews()
    {
        _sourceMapEntries = Array.Empty<NovaCompilerNative.SourceMapEntry>();
        _debugSymbols = Array.Empty<NovaCompilerNative.DebugSymbol>();
        _debugFunctions = Array.Empty<NovaCompilerNative.DebugFunction>();
        _debugGlobals = Array.Empty<NovaCompilerNative.DebugGlobal>();
        _symbolReferences = Array.Empty<NovaCompilerNative.SymbolReference>();
        _includeDependencies = Array.Empty<NovaCompilerNative.IncludeDependency>();
        _debugStrings = Array.Empty<NovaCompilerNative.DebugString>();
        _declarations = Array.Empty<NovaCompilerNative.DeclarationMetadata>();
        _macros = Array.Empty<NovaCompilerNative.MacroDefinition>();
        _sourceMapRows.Clear();
        _watches.Clear();
        _callStack.Clear();
        CurrentSourceText.Text = "—";
    }
    private void PopulateSourceDebugViews()
    {
        _sourceMapRows.Clear();
        _watches.Clear();
        _callStack.Clear();
        foreach (var entry in _sourceMapEntries)
        {
            var file = ProjectFile(entry.FileId);
            var lines = (file?.Text ?? string.Empty).Replace("\r\n", "\n").Split('\n');
            var sourceText = entry.Line > 0u && entry.Line <= lines.Length
            ? lines[entry.Line - 1u].Trim()
            : string.Empty;
            _sourceMapRows.Add(new SourceMapRow
            {
                FileId = entry.FileId,
                FileName = file?.Name ?? $"file#{entry.FileId}",
                Line = entry.Line,
                Column = entry.Column,
                StartAddress = entry.BytecodeStart,
                EndAddress = entry.BytecodeEnd,
                StatementKind = entry.StatementKind,
                FunctionIndex = entry.FunctionIndex,
                SourceText = sourceText
            });

        }
        RefreshBreakpointVisuals();
    }
    private void LoadDemoProgram(bool clearConsole)
    {
        NovaNative.nova_vm_reset(_vm);
        var result = NovaNative.nova_vm_load(_vm, _demoProgram, (uint)_demoProgram.Length, 0);
        if (clearConsole)
            ConsoleText.Clear();
        RefreshSnapshot(mmioActivity: false);
        if (result < 0)
            SetStatus($"Bytecode load failed: {result}");
    }
    private bool DrainEvents()
    {
        var output = new StringBuilder();
        var mmioActivity = false;
        while (NovaNative.nova_vm_poll_event(_vm, out var eventData) > 0)
        {
            if (eventData.Type != NovaNative.EventConsoleChar) continue;
            output.Append((char)(eventData.Value & 0xFF));
            mmioActivity = true;
        }
        if (output.Length > 0)
        {
            ConsoleText.AppendText(output.ToString());
            ConsoleText.ScrollToEnd();
        }
        return mmioActivity;
    }
    private void RefreshSnapshot(bool mmioActivity)
    {
        if (_vm == 0) return;
        for (uint i = 0; i < 16; i++)
            _registers[(int)i].Value = $"0x{NovaNative.nova_vm_get_register(_vm, i):X8}";
        var pc = NovaNative.nova_vm_get_pc(_vm);
        var sp = NovaNative.nova_vm_get_sp(_vm);
        var flags = NovaNative.nova_vm_get_flags(_vm);
        var opcode = NovaNative.nova_vm_get_last_opcode(_vm);
        PcText.Text = $"0x{pc:X8}";
        SpText.Text = $"0x{sp:X8}";
        InstructionText.Text = NovaNative.nova_vm_get_instruction_count(_vm).ToString("N0");
        FlagsText.Text = FormatFlags(flags);
        BreakpointCountText.Text = NovaNative.nova_vm_get_breakpoint_count(_vm).ToString();
        RefreshHeapSnapshot();
        DisassemblyRow? current = null;
        foreach (var row in _disassembly)

        {
            row.IsCurrent = row.Address == pc;
            if (row.IsCurrent) current = row;
        }
        if (current is not null)
            DisassemblyList.ScrollIntoView(current);
        RefreshSourceDebugger(pc);
        RefreshCallStack(pc);
        RefreshWatchValues();
        RefreshGraphRuntimeState(pc, opcode, mmioActivity);
    }
    private void RefreshGraphRuntimeState(uint pc, byte opcode, bool mmioActivity)
    {
        var activeNodes = new HashSet<string>(StringComparer.Ordinal) { "vm" };
        var activeEdges = new List<(string FromId, string ToId)>();
        var activityNode = mmioActivity ? "mmio" : ActiveNodeForOpcode(opcode);
        activeNodes.Add(activityNode);
        activeEdges.Add(("vm", activityNode));
        var currentFunctionIndex = FindFunctionIndexByAddress(pc);
        if (currentFunctionIndex >= 0)
        {
            var currentFunctionNode = FunctionNodeId(currentFunctionIndex);
            activeNodes.Add(currentFunctionNode);
            var sourceFile = ProjectFile(_debugFunctions[currentFunctionIndex].FileId);
            if (sourceFile is not null) activeNodes.Add(sourceFile.NodeId);
        }
        var frameCount = NovaNative.nova_vm_get_call_frame_count(_vm);
        string? callerNode = null;
        for (uint i = 0; i < frameCount; i++)
        {
            if (NovaNative.nova_vm_get_call_frame(_vm, i, out var frame) != NovaNative.Ok) continue;
            var functionIndex = FindFunctionIndexByTarget(frame.TargetAddress);
            if (functionIndex < 0) continue;
            var functionNode = FunctionNodeId(functionIndex);
            activeNodes.Add(functionNode);
            if (callerNode is not null && !string.Equals(callerNode, functionNode, StringComparison.Ordinal))
                activeEdges.Add((callerNode, functionNode));
            callerNode = functionNode;
        }
        ArchitectureGraph.SetRuntimeState(activeNodes, activeEdges, activityNode);
        if (_graphRuntimeOverlayEnabled)
        {
            var functionLabel = currentFunctionIndex >= 0 ? _debugFunctions[currentFunctionIndex].Name : "entry/runtime";
            GraphRuntimeText.Text = $"RUNTIME · {functionLabel} · PC 0x{pc:X8} · {activityNode.ToUpperInvariant()}";
        }
    }
    private void RefreshGraphBreakpointState()
    {
        if (_vm == 0) return;
        var nodeIds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var row in _disassembly)
        {
            if (!row.IsBreakpoint) continue;
            var functionIndex = FindFunctionIndexByAddress(row.Address);
            if (functionIndex >= 0) nodeIds.Add(FunctionNodeId(functionIndex));
            var source = FindSourceRow(row.Address);
            var file = source is null ? null : ProjectFile(source.FileId);
            if (file is not null) nodeIds.Add(file.NodeId);
        }
        ArchitectureGraph.SetBreakpointNodes(nodeIds);
    }
    private SourceMapRow? FindSourceRow(uint pc)
    {
        SourceMapRow? current = null;
        uint currentSpan = uint.MaxValue;
        foreach (var row in _sourceMapRows)
        {
            if (pc < row.StartAddress || pc >= row.EndAddress) continue;
            var span = row.EndAddress - row.StartAddress;
            if (current is null || span < currentSpan)
            {
                current = row;
                currentSpan = span;
            }
        }
        return current;
    }
    private void RefreshSourceDebugger(uint pc)
    {
        foreach (var row in _sourceMapRows) row.IsCurrent = false;
        var current = FindSourceRow(pc);
        if (current is null)
        {
            CurrentSourceText.Text = "—";
            return;
        }
        current.IsCurrent = true;
        CurrentSourceText.Text = $"{current.FileText}:{current.Line} · {current.KindText} · {current.AddressText}";
        SourceMapList.ScrollIntoView(current);
        ShowSourceLocation(current.FileId, current.Line);
    }
    private void ShowSourceLocation(uint fileId, uint line)
    {
        var file = ProjectFile(fileId);
        if (file is null) return;
        OpenProjectFile(file);
        HighlightSourceLine(line);
        ArchitectureGraph.ActiveNodeId = file.NodeId;
    }
    private void HighlightSourceLine(uint line)
    {
        if (line == 0u) return;
        var text = SourceEditor.Text;

        var currentLine = 1u;
        var start = 0;
        while (currentLine < line && start < text.Length)
        {
            var newline = text.IndexOf('\n', start);
            if (newline < 0) return;
            start = newline + 1;
            currentLine++;
        }
        var end = text.IndexOf('\n', start);
        if (end < 0) end = text.Length;
        var length = end - start;
        if (length > 0 && text[end - 1] == '\r') length--;
        SourceEditor.Select(start, Math.Max(length, 0));
        SourceEditor.ScrollToLine(checked((int)line - 1));
    }
    private int FindFunctionIndexByAddress(uint address)
    {
        for (var i = 0; i < _debugFunctions.Length; i++)
        {
            var function = _debugFunctions[i];
            if (address >= function.BytecodeStart && address < function.BytecodeEnd)
                return i;
        }
        return -1;
    }
    private int FindFunctionIndexByTarget(uint target)
    {
        for (var i = 0; i < _debugFunctions.Length; i++)
        {
            if (_debugFunctions[i].BytecodeStart == target)
                return i;
        }
        return FindFunctionIndexByAddress(target);
    }
    private bool TryReadSelectedFrameRegister(uint nativeFrameIndex, uint registerIndex, out uint value)
    {
        value = 0u;
        if (_vm == 0 || registerIndex >= 16u) return false;
        var frameCount = NovaNative.nova_vm_get_call_frame_count(_vm);
        if (nativeFrameIndex >= frameCount) return false;
        if (nativeFrameIndex == frameCount - 1u)
        {
            value = NovaNative.nova_vm_get_register(_vm, registerIndex);
            return true;
        }
        if (registerIndex < NovaCFirstSavedRegister || registerIndex > NovaCLastSavedRegister)
            return false;
        if (NovaNative.nova_vm_get_call_frame(_vm, nativeFrameIndex + 1u, out var childFrame) != NovaNative.Ok)
            return false;

        var slotFromChildSp = 1u + (NovaCLastSavedRegister - registerIndex);
        var address = childFrame.StackPointer + slotFromChildSp * 4u;
        var bytes = new byte[4];
        if (NovaNative.nova_vm_read_memory(_vm, address, bytes, 4u) != NovaNative.Ok)
            return false;
        value = (uint)(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
        return true;
    }
    private uint ResolveSelectedFrameIndex(uint frameCount)
    {
        if (frameCount == 0u) return uint.MaxValue;
        if (_selectedNativeFrameIndex is uint selected && selected < frameCount) return selected;
        return frameCount - 1u;
    }
    private uint ResolveFrameSourceAddress(uint nativeFrameIndex, uint frameCount, uint currentPc)
    {
        if (nativeFrameIndex >= frameCount) return currentPc;
        if (nativeFrameIndex == frameCount - 1u) return currentPc;
        if (NovaNative.nova_vm_get_call_frame(_vm, nativeFrameIndex + 1u, out var childFrame) != NovaNative.Ok)
            return currentPc;
        return childFrame.ReturnAddress >= NovaCCallInstructionSize
        ? childFrame.ReturnAddress - NovaCCallInstructionSize
        : childFrame.ReturnAddress;
    }
    private static string DeclarationLinkageLabel(NovaCompilerNative.DeclarationMetadata? declaration)
    => declaration?.Linkage switch
    {
        NovaCompilerNative.LinkageInternal => "static",
        NovaCompilerNative.LinkageExternal => "external",
        _ => "none"
    };
    private static bool declarationHasFlag(NovaCompilerNative.DeclarationMetadata? declaration, uint flag)
    => declaration is { } value && (value.Flags & flag) != 0u;
    private static string DeclarationQualifierPrefix(NovaCompilerNative.DeclarationMetadata declaration)
    {
        var parts = new List<string>();
        if ((declaration.Qualifiers & NovaCompilerNative.DeclQualConst) != 0u) parts.Add("const");
        if ((declaration.Qualifiers & NovaCompilerNative.DeclQualVolatile) != 0u) parts.Add("volatile");
        if ((declaration.Qualifiers & NovaCompilerNative.DeclQualRestrict) != 0u) parts.Add("restrict");
        return parts.Count == 0 ? string.Empty : string.Join(" ", parts) + " ";
    }
    private static string DeclarationQualifierSuffix(NovaCompilerNative.DeclarationMetadata? declaration)
    {
        if (declaration is not { } value) return string.Empty;
        var prefix = DeclarationQualifierPrefix(value).Trim();
        return prefix.Length == 0 ? string.Empty : $" · {prefix}";
    }
    private static string DeclarationTypeName(uint type)
    => type switch
    {
        NovaCompilerNative.DebugIntPtr => "int*",
        NovaCompilerNative.DebugArray => "array",
        NovaCompilerNative.DebugStruct => "struct",
        NovaCompilerNative.DebugStructPtr => "struct*",
        NovaCompilerNative.DebugChar => "char",
        NovaCompilerNative.DebugCharPtr => "char*",
        NovaCompilerNative.DebugUnion => "union",
        NovaCompilerNative.DebugUnionPtr => "union*",
        _ => "int"
    };
    private static string DebugTypeName(NovaCompilerNative.DebugSymbol symbol)
    {
        return symbol.Type switch
        {
            NovaCompilerNative.DebugIntPtr => "int*",
            NovaCompilerNative.DebugArray => $"array[{symbol.ElementCount}]",
            NovaCompilerNative.DebugStruct => $"struct ({symbol.ElementCount} fields)",
            NovaCompilerNative.DebugStructPtr => "struct*",
            NovaCompilerNative.DebugUnion => $"union ({symbol.ElementCount} fields)",
            NovaCompilerNative.DebugUnionPtr => "union*",
            NovaCompilerNative.DebugChar => "char",
            NovaCompilerNative.DebugCharPtr => "char*",
            _ => "int"
        };
    }
    private static string DebugGlobalTypeName(NovaCompilerNative.DebugGlobal global)
    {
        return global.Type switch
        {
            NovaCompilerNative.DebugIntPtr => "int*",
            NovaCompilerNative.DebugArray => $"array[{global.ElementCount}]",
            NovaCompilerNative.DebugStruct => $"struct ({global.ElementCount} fields)",
            NovaCompilerNative.DebugStructPtr => "struct*",
            NovaCompilerNative.DebugUnion => $"union ({global.ElementCount} fields)",
            NovaCompilerNative.DebugUnionPtr => "union*",
            NovaCompilerNative.DebugChar => "char",
            NovaCompilerNative.DebugCharPtr => "char*",
            _ => "int"
        };
    }
    private static bool IsPointerWatchType(string type)
    => type.EndsWith("*", StringComparison.Ordinal);
    private bool TryReadWatchMemory(uint baseAddress, uint byteOffset, uint byteSize, out uint value)

    {
        value = 0u;
        var width = byteSize == 1u ? 1u : 4u;
        var bytes = new byte[checked((int)width)];
        if (baseAddress > uint.MaxValue - byteOffset) return false;
        if (NovaNative.nova_vm_read_memory(_vm, baseAddress + byteOffset, bytes, width) != NovaNative.Ok) return false;
        value = bytes[0];
        if (width == 4u) value |= (uint)(bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24);
        return true;
    }
    private void RefreshWatchValues()
    {
        if (_vm == 0) return;
        var frameCount = NovaNative.nova_vm_get_call_frame_count(_vm);
        var nativeFrameIndex = ResolveSelectedFrameIndex(frameCount);
        if (nativeFrameIndex == uint.MaxValue || NovaNative.nova_vm_get_call_frame(_vm, nativeFrameIndex, out var frame) != NovaNative.Ok)
        {
            _watches.Clear();
            return;
        }
        var functionIndex = FindFunctionIndexByTarget(frame.TargetAddress);
        if (functionIndex < 0)
        {
            _watches.Clear();
            return;
        }
        var frameAddress = ResolveFrameSourceAddress(nativeFrameIndex, frameCount, NovaNative.nova_vm_get_pc(_vm));
        var matching = _debugSymbols
        .Where(symbol => symbol.FunctionIndex == (uint)functionIndex &&
        frameAddress >= symbol.LifetimeStart &&
        (symbol.LifetimeEnd == uint.MaxValue || frameAddress < symbol.LifetimeEnd))
        .GroupBy(symbol => symbol.Name, StringComparer.Ordinal)
        .Select(group => group.OrderByDescending(symbol => symbol.ScopeDepth).First())
        .OrderBy(symbol => symbol.ScopeDepth)
        .ThenBy(symbol => symbol.RegisterIndex)
        .ThenBy(symbol => symbol.StorageKind)
        .ThenBy(symbol => symbol.ByteOffset)
        .ToArray();
        _watches.Clear();
        foreach (var symbol in matching)
        {
            _watches.Add(new WatchRow
            {
                Name = symbol.Name,
                Type = DebugTypeName(symbol),
                RegisterIndex = symbol.RegisterIndex,
                DeclarationLine = symbol.DeclarationLine,
                FileId = symbol.FileId,
                FunctionIndex = symbol.FunctionIndex,
                IsParameter = symbol.IsParameter != 0u,
                StorageKind = symbol.StorageKind,
                ByteOffset = symbol.ByteOffset,
                ByteSize = symbol.ByteSize,
                ElementCount = symbol.ElementCount,
                LifetimeStart = symbol.LifetimeStart,
                LifetimeEnd = symbol.LifetimeEnd,

                ScopeDepth = symbol.ScopeDepth
            });
        }
        foreach (var watch in _watches)
        {
            if (!TryReadSelectedFrameRegister(nativeFrameIndex, watch.RegisterIndex, out var baseOrValue))
            {
                watch.Value = "unavailable";
                continue;
            }
            if (watch.StorageKind == NovaCompilerNative.DebugStorageMemory)
            {
                if (!TryReadWatchMemory(baseOrValue, watch.ByteOffset, watch.ByteSize, out var memoryValue))
                {
                    watch.Value = "unavailable";
                    continue;
                }
                watch.Value = IsPointerWatchType(watch.Type)
                ? $"0x{memoryValue:X8}"
                : watch.Type == "char"
                ? $"'{(char)(memoryValue & 0xFFu)}' · {memoryValue & 0xFFu}"
                : $"{unchecked((int)memoryValue).ToString(CultureInfo.InvariantCulture)} (0x{memoryValue:X8})";
                continue;
            }
            if (watch.Type.StartsWith("array", StringComparison.Ordinal) || watch.Type.StartsWith("struct", StringComparison.Ordinal) || watch.Type.StartsWith("union", StringComparison.Ordinal))
            {
                watch.Value = $"0x{baseOrValue:X8} · {watch.ByteSize} B";
                continue;
            }
            watch.Value = IsPointerWatchType(watch.Type)
            ? $"0x{baseOrValue:X8}"
            : watch.Type == "char"
            ? $"'{(char)(baseOrValue & 0xFFu)}' · {baseOrValue & 0xFFu}"
            : $"{unchecked((int)baseOrValue).ToString(CultureInfo.InvariantCulture)} (0x{baseOrValue:X8})";
        }
    }
    private void WatchList_MouseDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (e.OriginalSource is FrameworkElement { DataContext: WatchRow row })
        {
            ShowSourceLocation(row.FileId, row.DeclarationLine);
            SetStatus($"WATCH {row.Name} · {row.Type} · source {row.FileId}:{row.DeclarationLine}");
        }
    }
    private void ShowSelectedFrameSource(StackFrameRow row, uint frameCount, uint currentPc)
    {
        var address = ResolveFrameSourceAddress(row.NativeFrameIndex, frameCount, currentPc);
        var source = FindSourceRow(address);
        if (source is null) return;
        CurrentSourceText.Text = row.NativeFrameIndex == frameCount - 1u
        ? $"{source.FileText}:{source.Line} · {source.KindText} · {source.AddressText}"
        : $"frame {row.Function} · {source.FileText}:{source.Line}";
        ShowSourceLocation(source.FileId, source.Line);
    }
    private void RefreshCallStack(uint pc)
    {
        if (_vm == 0) return;
        var frameCount = NovaNative.nova_vm_get_call_frame_count(_vm);
        var selectedIndex = ResolveSelectedFrameIndex(frameCount);
        _selectedNativeFrameIndex = selectedIndex == uint.MaxValue ? null : selectedIndex;

        _refreshingCallStackSelection = true;
        _callStack.Clear();
        StackFrameRow? selectedRow = null;
        for (var reverse = frameCount; reverse > 0u; reverse--)
        {
            var nativeIndex = reverse - 1u;
            if (NovaNative.nova_vm_get_call_frame(_vm, nativeIndex, out var frame) != NovaNative.Ok)
                continue;
            var functionIndex = FindFunctionIndexByTarget(frame.TargetAddress);
            var functionName = functionIndex >= 0 ? _debugFunctions[functionIndex].Name : $"0x{frame.TargetAddress:X8}";
            var functionFile = functionIndex >= 0 ? ProjectFile(_debugFunctions[functionIndex].FileId) : null;
            var functionFileName = functionIndex >= 0
            ? System.IO.Path.GetFileName(functionFile?.Name ?? ("file#" + _debugFunctions[functionIndex].FileId.ToString(CultureInfo.InvariantCulture)))
            : string.Empty;
            var range = functionIndex >= 0
            ? $"{functionFileName} · depth {frame.Depth} · 0x{_debugFunctions[functionIndex].BytecodeStart:X8}–0x{_debugFunctions[functionIndex].BytecodeEnd:X8}"
            : $"depth {frame.Depth} · target 0x{frame.TargetAddress:X8}";
            var state = nativeIndex == frameCount - 1u
            ? $"current · PC 0x{pc:X8}"
            : $"saved caller · ret 0x{frame.ReturnAddress:X8}";
            var row = new StackFrameRow
            {
                NativeFrameIndex = nativeIndex,
                FunctionIndex = functionIndex >= 0 ? (uint)functionIndex : uint.MaxValue,
                Function = functionName,
                Range = range,
                State = state,
                IsSelected = nativeIndex == selectedIndex
            };
            _callStack.Add(row);
            if (row.IsSelected) selectedRow = row;
        }
        CallStackList.SelectedItem = selectedRow;
        _refreshingCallStackSelection = false;
        if (selectedRow is not null && selectedRow.NativeFrameIndex != frameCount - 1u)
            ShowSelectedFrameSource(selectedRow, frameCount, pc);
    }
    private void CallStackList_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (_refreshingCallStackSelection || CallStackList.SelectedItem is not StackFrameRow row || _vm == 0)
            return;
        _selectedNativeFrameIndex = row.NativeFrameIndex;
        foreach (var frame in _callStack) frame.IsSelected = frame.NativeFrameIndex == row.NativeFrameIndex;
        RefreshWatchValues();
        var frameCount = NovaNative.nova_vm_get_call_frame_count(_vm);
        ShowSelectedFrameSource(row, frameCount, NovaNative.nova_vm_get_pc(_vm));
        SetStatus($"Selected frame {row.Function} at depth {row.NativeFrameIndex}; WATCH now shows reconstructed frame state.");
    }
    private static string ActiveNodeForOpcode(byte opcode) => opcode switch
    {
        >= 0x60 and <= 0x63 => "memory",
        0x70 or 0x71 => "memory",
        _ => "cpu"
    };

    private void RefreshHeapSnapshot()
    {
        if (NovaNative.nova_vm_get_heap_stats(_vm, out var stats) != NovaNative.Ok)
        {
            HeapUsageText.Text = "heap unavailable";
            HeapBlockCountText.Text = "-";
            HeapUsageBar.Value = 0;
            _heapBlocks.Clear();
            return;
        }
        var usedPercent = stats.TotalPayloadBytes == 0
        ? 0.0
        : stats.UsedPayloadBytes * 100.0 / stats.TotalPayloadBytes;
        HeapUsageText.Text = $"{FormatBytes(stats.UsedPayloadBytes)} / {FormatBytes(stats.TotalPayloadBytes)}";
        HeapBlockCountText.Text = $"{stats.AllocationCount} used / {stats.BlockCount} blocks";
        HeapLargestFreeText.Text = FormatBytes(stats.LargestFreeBlock);
        HeapUsageBar.Value = usedPercent;
        _heapBlocks.Clear();
        var visible = Math.Min(stats.BlockCount, 24u);
        for (uint i = 0; i < visible; i++)
        {
            if (NovaNative.nova_vm_get_heap_block(_vm, i, out var block) != NovaNative.Ok)
                break;
            _heapBlocks.Add(new HeapBlockRow(
            $"0x{block.PayloadAddress:X8}",
            FormatBytes(block.PayloadSize),
            block.IsUsed != 0 ? $"USED #{block.AllocationId}" : "FREE"));
        }
    }
    private static string FormatBytes(uint bytes)
    {
        if (bytes >= 1024u * 1024u) return $"{bytes / (1024.0 * 1024.0):0.00} MiB";
        if (bytes >= 1024u) return $"{bytes / 1024.0:0.0} KiB";
        return $"{bytes} B";
    }
    private void SetDiagnostic(string value, bool select = false)
    {
        DiagnosticsText.Text = value;
        if (select) DiagnosticsTab.IsSelected = true;
    }
    private void SetStatus(string value) => StatusText.Text = value;
    private static string FormatFlags(uint flags)
    {
        var z = (flags & 0x1) != 0 ? 'Z' : '-';
        var n = (flags & 0x2) != 0 ? 'N' : '-';
        var c = (flags & 0x4) != 0 ? 'C' : '-';
        var o = (flags & 0x8) != 0 ? 'O' : '-';
        return new string(new[] { z, n, c, o });
    }
    private static byte[] BuildConsoleProgram(string text)
    {
        var bytes = new List<byte>();
        EmitMovImmediate(bytes, 1, MmioConsole);
        foreach (var ch in Encoding.ASCII.GetBytes(text))
        {

            EmitMovImmediate(bytes, 0, ch);
            bytes.Add(OpStore8);
            bytes.Add(1);
            bytes.Add(0);
        }
        // Keep one allocation alive in the demo so the heap visualizer has a real block to display.
        EmitMovImmediate(bytes, 2, 96u);
        bytes.Add(0x70); // ALLOC R3, R2
        bytes.Add(3);
        bytes.Add(2);
        bytes.Add(OpHalt);
        return bytes.ToArray();
    }
    private static void EmitMovImmediate(List<byte> output, byte register, uint value)
    {
        output.Add(OpMovRi);
        output.Add(register);
        output.Add((byte)(value & 0xFF));
        output.Add((byte)((value >> 8) & 0xFF));
        output.Add((byte)((value >> 16) & 0xFF));
        output.Add((byte)((value >> 24) & 0xFF));
    }
    private sealed record HeapBlockRow(string Address, string Size, string State);
    private sealed class RegisterRow : System.ComponentModel.INotifyPropertyChanged
    {
        private string _value;
        public RegisterRow(string name, string value)
        {
            Name = name;
            _value = value;
        }
        public string Name { get; }
        public string Value
        {
            get => _value;
            set
            {
                if (_value == value) return;
                _value = value;
                PropertyChanged?.Invoke(this, new System.ComponentModel.PropertyChangedEventArgs(nameof(Value)));
            }
        }
        public event System.ComponentModel.PropertyChangedEventHandler? PropertyChanged;
    }
}
