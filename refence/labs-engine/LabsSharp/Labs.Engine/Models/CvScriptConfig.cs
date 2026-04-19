using System;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;

namespace Labs.Engine.Models;

/// <summary>Serializable configuration for a CV script — a named list of CvRules.</summary>
public class CvScriptConfig
{
    /// <summary>Folder where script configs are saved, next to the exe.</summary>
    public static readonly string ConfigFolder = Path.Combine(AppContext.BaseDirectory, "ScriptConfigs");

    /// <summary>Name of this script configuration.</summary>
    public string ScriptName { get; set; } = string.Empty;

    /// <summary>Ordered list of CV detection rules.</summary>
    public List<CvRule> Rules { get; set; } = new();

    /// <summary>Saves this config to /ScriptConfigs/{ScriptName}.json.</summary>
    public void Save()
    {
        Directory.CreateDirectory(ConfigFolder);
        var path = Path.Combine(ConfigFolder, $"{ScriptName}.json");
        var json = JsonConvert.SerializeObject(this, Formatting.Indented);
        File.WriteAllText(path, json);
    }

    /// <summary>Loads a CvScriptConfig from the given JSON file path.</summary>
    public static CvScriptConfig Load(string path)
    {
        var json = File.ReadAllText(path);
        return JsonConvert.DeserializeObject<CvScriptConfig>(json)
            ?? throw new InvalidOperationException($"Failed to deserialize config from: {path}");
    }
}
