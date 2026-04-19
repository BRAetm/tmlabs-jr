using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace Labs.Engine.Registration;

/// <summary>On-disk store of registered hosts. One JSON file under %AppData%/PSRemotePlay/hosts.json.</summary>
public static class RegistrationStore
{
    private static readonly string Path = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "PSRemotePlay", "hosts.json");

    private static Dictionary<string, RegisteredCredentials> _cache = Load();

    public static bool IsRegistered(string hostId) => _cache.ContainsKey(hostId);

    public static RegisteredCredentials? Load(string hostId)
        => _cache.TryGetValue(hostId, out var c) ? c : null;

    public static void Save(string hostId, RegisteredCredentials creds)
    {
        _cache[hostId] = creds;
        Persist();
    }

    private static Dictionary<string, RegisteredCredentials> Load()
    {
        if (!File.Exists(Path)) return new();
        try
        {
            var json = File.ReadAllText(Path);
            return JsonSerializer.Deserialize<Dictionary<string, RegisteredCredentials>>(json) ?? new();
        }
        catch { return new(); }
    }

    private static void Persist()
    {
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(Path)!);
        File.WriteAllText(Path, JsonSerializer.Serialize(_cache, new JsonSerializerOptions { WriteIndented = true }));
    }
}
