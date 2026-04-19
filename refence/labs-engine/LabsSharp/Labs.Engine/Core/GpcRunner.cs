using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace Labs.Engine.Core;

/// <summary>
/// Compiles and runs GPC3 scripts using the Helios GPC3 toolchain.
/// GPC (GamePack Compiler) is the scripting language used by Titan Two and Cronus devices.
/// Pipeline: .gpc3 source → gpc3vmc.exe → .gbc3 bytecode → gpc3vm-run.exe → controller output
/// </summary>
public class GpcRunner : IAsyncDisposable
{
    private static readonly string ToolsDir = Path.Combine(AppContext.BaseDirectory, "Tools", "gpc3");
    private static readonly string CompilerPath = Path.Combine(ToolsDir, "gpc3vmc.exe");
    private static readonly string VmPath = Path.Combine(ToolsDir, "gpc3vm-run.exe");
    private static readonly string CompiledDir = Path.Combine(AppContext.BaseDirectory, "GpcCompiled");

    private Process? _vmProcess;
    private CancellationTokenSource? _cts;
    private readonly object _lock = new();

    /// <summary>Raised when the GPC VM outputs a line (stdout/stderr).</summary>
    public event Action<string>? OutputReceived;

    /// <summary>Raised when the GPC VM process exits.</summary>
    public event Action<int>? VmExited;

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    /// <summary>Returns true if the GPC3 tools are available.</summary>
    public static bool IsAvailable() =>
        File.Exists(CompilerPath) && File.Exists(VmPath);

    /// <summary>Compiles a .gpc3 script to .gbc3 bytecode. Returns the output path on success.</summary>
    public async Task<string?> CompileAsync(string scriptPath)
    {
        if (!File.Exists(CompilerPath))
        {
            Log("[GPC] Compiler not found. Place gpc3vmc.exe in Tools/gpc3/");
            return null;
        }

        if (!File.Exists(scriptPath))
        {
            Log($"[GPC] Script not found: {scriptPath}");
            return null;
        }

        Directory.CreateDirectory(CompiledDir);
        var outputName = Path.GetFileNameWithoutExtension(scriptPath) + ".gbc3";
        var outputPath = Path.Combine(CompiledDir, outputName);

        var psi = new ProcessStartInfo
        {
            FileName = CompilerPath,
            Arguments = $"\"{scriptPath}\" -o \"{outputPath}\"",
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };

        Log($"[GPC] Compiling: {Path.GetFileName(scriptPath)}...");

        try
        {
            var proc = Process.Start(psi);
            if (proc is null) return null;

            var stdout = await proc.StandardOutput.ReadToEndAsync();
            var stderr = await proc.StandardError.ReadToEndAsync();
            await proc.WaitForExitAsync();

            if (!string.IsNullOrWhiteSpace(stdout)) Log($"[GPC] {stdout.Trim()}");
            if (!string.IsNullOrWhiteSpace(stderr)) Log($"[GPC] ERROR: {stderr.Trim()}");

            if (proc.ExitCode == 0 && File.Exists(outputPath))
            {
                Log($"[GPC] Compiled successfully: {outputName}");
                return outputPath;
            }

            Log($"[GPC] Compilation failed (exit code {proc.ExitCode})");
            return null;
        }
        catch (Exception ex)
        {
            Log($"[GPC] Compile error: {ex.Message}");
            return null;
        }
    }

    /// <summary>Runs a compiled .gbc3 bytecode file in the GPC3 virtual machine.</summary>
    public async Task<bool> RunAsync(string bytecodePath, int? frequency = null)
    {
        if (!File.Exists(VmPath))
        {
            Log("[GPC] VM not found. Place gpc3vm-run.exe in Tools/gpc3/");
            return false;
        }

        if (!File.Exists(bytecodePath))
        {
            Log($"[GPC] Bytecode not found: {bytecodePath}");
            return false;
        }

        await StopAsync();

        _cts = new CancellationTokenSource();
        var hostPid = Environment.ProcessId;

        var args = $"\"{bytecodePath}\" --helios-pid {hostPid}";
        if (frequency.HasValue)
            args += $" --frequency {frequency.Value}";

        var psi = new ProcessStartInfo
        {
            FileName = VmPath,
            Arguments = args,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };

        Log($"[GPC] Starting VM: {Path.GetFileName(bytecodePath)} (host PID {hostPid})");

        try
        {
            var proc = new Process { StartInfo = psi, EnableRaisingEvents = true };
            proc.OutputDataReceived += (_, e) => { if (e.Data is not null) Log($"[GPC:VM] {e.Data}"); };
            proc.ErrorDataReceived += (_, e) => { if (e.Data is not null) Log($"[GPC:VM:ERR] {e.Data}"); };

            proc.Start();
            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();

            lock (_lock)
                _vmProcess = proc;

            Log($"[GPC] VM running (PID {proc.Id})");

            // Monitor for exit
            _ = Task.Run(async () =>
            {
                await proc.WaitForExitAsync();
                var code = proc.ExitCode;
                Log($"[GPC] VM exited (code {code})");
                VmExited?.Invoke(code);
            });

            return true;
        }
        catch (Exception ex)
        {
            Log($"[GPC] VM start error: {ex.Message}");
            return false;
        }
    }

    /// <summary>Compiles and runs a .gpc3 script in one step.</summary>
    public async Task<bool> CompileAndRunAsync(string scriptPath, int? frequency = null)
    {
        var bytecode = await CompileAsync(scriptPath);
        if (bytecode is null) return false;
        return await RunAsync(bytecode, frequency);
    }

    /// <summary>Stops the running GPC VM process.</summary>
    public async Task StopAsync()
    {
        Process? proc;
        lock (_lock)
        {
            proc = _vmProcess;
            _vmProcess = null;
        }

        _cts?.Cancel();

        if (proc is not null)
        {
            try
            {
                if (!proc.HasExited)
                {
                    proc.Kill();
                    await proc.WaitForExitAsync();
                }
            }
            catch { }
            finally
            {
                proc.Dispose();
            }
            Log("[GPC] VM stopped.");
        }

        _cts?.Dispose();
        _cts = null;
    }

    /// <summary>True if the GPC VM is currently running.</summary>
    public bool IsRunning
    {
        get
        {
            lock (_lock)
                return _vmProcess is not null && !_vmProcess.HasExited;
        }
    }

    /// <summary>Lists available .gpc3 script files from the Scripts folder.</summary>
    public static List<string> ListScripts()
    {
        var scriptsDir = Path.Combine(AppContext.BaseDirectory, "Scripts");
        var results = new List<string>();

        if (Directory.Exists(scriptsDir))
        {
            foreach (var f in Directory.EnumerateFiles(scriptsDir, "*.gpc3"))
                results.Add(Path.GetFileName(f));
            foreach (var f in Directory.EnumerateFiles(scriptsDir, "*.gpc"))
                results.Add(Path.GetFileName(f));
        }

        return results;
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync();
    }

    private void Log(string msg)
    {
        Console.WriteLine(msg);
        OutputReceived?.Invoke(msg);
    }
}
