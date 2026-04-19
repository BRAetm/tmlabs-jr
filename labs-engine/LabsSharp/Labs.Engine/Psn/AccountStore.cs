using System;
using System.IO;

namespace Labs.Engine.Psn;

/// <summary>Persists the user's PSN account id (base64 of 8 bytes) between launches.</summary>
public static class AccountStore
{
    private static readonly string Path = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "PSRemotePlay", "account.txt");

    public static string? Load()
    {
        try { return File.Exists(Path) ? File.ReadAllText(Path).Trim() : null; }
        catch { return null; }
    }

    public static void Save(string accountIdBase64)
    {
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(Path)!);
        File.WriteAllText(Path, accountIdBase64);
    }

    public static bool HasAccount => !string.IsNullOrEmpty(Load());
}
