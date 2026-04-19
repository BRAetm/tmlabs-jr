using System;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;

namespace Labs.Engine.Core;

/// <summary>Persisted configuration for a single session (saved to /Config).</summary>
public class SessionConfig
{
    public string Name { get; set; } = string.Empty;
    public string Platform { get; set; } = string.Empty;
    public string Url { get; set; } = string.Empty;
    public string? LastScript { get; set; }
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
    public DateTime LastUsed { get; set; } = DateTime.UtcNow;
}

/// <summary>Manages per-session config persistence in the /Config folder (Helios settings.ini equivalent).</summary>
public static class SessionConfigStore
{
    private static readonly string ConfigFolder = Path.Combine(AppContext.BaseDirectory, "Config");

    /// <summary>Saves a session config to disk.</summary>
    public static void Save(int sessionId, SessionConfig config)
    {
        try
        {
            Directory.CreateDirectory(ConfigFolder);
            config.LastUsed = DateTime.UtcNow;
            var json = JsonConvert.SerializeObject(config, Formatting.Indented);
            File.WriteAllText(GetPath(sessionId), json);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[SessionConfig] Save failed for session {sessionId}: {ex.Message}");
        }
    }

    /// <summary>Loads a session config from disk, or null if not found.</summary>
    public static SessionConfig? Load(int sessionId)
    {
        try
        {
            var path = GetPath(sessionId);
            if (!File.Exists(path)) return null;
            var json = File.ReadAllText(path);
            return JsonConvert.DeserializeObject<SessionConfig>(json);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[SessionConfig] Load failed for session {sessionId}: {ex.Message}");
            return null;
        }
    }

    /// <summary>Deletes the config for a session.</summary>
    public static void Delete(int sessionId)
    {
        try
        {
            var path = GetPath(sessionId);
            if (File.Exists(path)) File.Delete(path);
        }
        catch { }
    }

    /// <summary>Loads all saved session configs.</summary>
    public static Dictionary<int, SessionConfig> LoadAll()
    {
        var result = new Dictionary<int, SessionConfig>();
        if (!Directory.Exists(ConfigFolder)) return result;

        foreach (var file in Directory.EnumerateFiles(ConfigFolder, "session_*.json"))
        {
            var name = Path.GetFileNameWithoutExtension(file);
            if (name.StartsWith("session_") && int.TryParse(name["session_".Length..], out var id))
            {
                var config = Load(id);
                if (config is not null)
                    result[id] = config;
            }
        }
        return result;
    }

    private static string GetPath(int sessionId) =>
        Path.Combine(ConfigFolder, $"session_{sessionId}.json");
}
