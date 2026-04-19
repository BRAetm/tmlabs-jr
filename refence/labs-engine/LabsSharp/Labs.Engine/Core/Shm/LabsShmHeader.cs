using System;
using System.IO.MemoryMappedFiles;

namespace Labs.Engine.Core.Shm;

/// <summary>
/// Fixed 32-byte header prefixed on every Labs shared-memory block. Mirrors the
/// pattern used by high-throughput game overlay tools — single-writer / many-readers,
/// sequence counter incremented last so readers can check consistency without a lock.
///
/// Layout (little-endian):
///   [0..3]   magic  "LABS" (0x5342414C)
///   [4..7]   version (uint32, currently 1)
///   [8..11]  writer_pid (uint32)
///   [12..15] sequence (uint32)  — bumped after every payload write
///   [16..19] payload_size (uint32)
///   [20..31] reserved (12 bytes)
/// </summary>
public static class LabsShmHeader
{
    public const uint Magic = 0x5342414Cu;        // 'L''A''B''S' in little-endian
    public const uint Version = 1;
    public const int Size = 32;

    public const int OffsetMagic       = 0;
    public const int OffsetVersion     = 4;
    public const int OffsetWriterPid   = 8;
    public const int OffsetSequence    = 12;
    public const int OffsetPayloadSize = 16;

    /// <summary>Block name format: Labs_&lt;pid&gt;_&lt;suffix&gt;.</summary>
    public static string BlockName(string suffix, int? pid = null)
        => $"Labs_{pid ?? Environment.ProcessId}_{suffix}";

    /// <summary>Named-event (cross-process) format: Global\Labs_&lt;pid&gt;_&lt;suffix&gt;_Written.</summary>
    public static string WrittenEventName(string suffix, int? pid = null)
        => $@"Global\Labs_{pid ?? Environment.ProcessId}_{suffix}_Written";

    /// <summary>Writes the fixed header bytes. Call once after creating the block.</summary>
    public static void Initialize(MemoryMappedViewAccessor a, uint payloadSize)
    {
        a.Write(OffsetMagic, Magic);
        a.Write(OffsetVersion, Version);
        a.Write(OffsetWriterPid, (uint)Environment.ProcessId);
        a.Write(OffsetSequence, 0u);
        a.Write(OffsetPayloadSize, payloadSize);
    }

    /// <summary>True when the block has been initialized by a writer.</summary>
    public static bool IsValid(MemoryMappedViewAccessor a)
        => a.ReadUInt32(OffsetMagic) == Magic && a.ReadUInt32(OffsetVersion) == Version;

    public static uint ReadSequence(MemoryMappedViewAccessor a) => a.ReadUInt32(OffsetSequence);

    /// <summary>Publish the new payload by bumping the sequence counter. Must be called AFTER writing the payload.</summary>
    public static void PublishSequence(MemoryMappedViewAccessor a, uint next) => a.Write(OffsetSequence, next);
}
