using System;
using System.Diagnostics;
using System.Threading;
using Labs.Engine.Core;
using Labs.Engine.Core.Shm;

namespace Labs.Engine.Tests;

public class LabsGamepadShmTests
{
    private sealed class CapturingSink : IGamepadSink
    {
        public int SessionId;
        public GamepadState? Last;
        public readonly ManualResetEventSlim Received = new(false);
        public bool IsConnected(int sessionId) => true;
        public void SendInput(int sessionId, GamepadState state)
        {
            SessionId = sessionId;
            Last = state;
            Received.Set();
        }
    }

    [Fact]
    public void Writer_Publish_Reaches_Reader_Sink()
    {
        var pid = Process.GetCurrentProcess().Id + 7777; // unique block name
        using var writer = new LabsGamepadShmWriter(pid);
        using var reader = new LabsGamepadShmReader(pid);
        var sink = new CapturingSink();
        reader.SetActiveSink(sink, sessionId: 42);
        reader.Start();

        var state = new GamepadState();
        state.Axes[0] = 0.5f; state.Axes[1] = -0.25f;
        state.Axes[2] = 1.0f; state.Axes[3] = -1.0f;
        state.Buttons[0] = true; state.Buttons[4] = true; state.Buttons[16] = true;

        // Reader may need a moment to open the block; retry publish a few times.
        for (int i = 0; i < 20 && !sink.Received.IsSet; i++)
        {
            writer.Publish(42, state);
            Thread.Sleep(25);
        }

        Assert.True(sink.Received.Wait(TimeSpan.FromSeconds(2)), "sink never received a frame");
        Assert.Equal(42, sink.SessionId);
        Assert.NotNull(sink.Last);
        Assert.Equal(0.5f, sink.Last!.Axes[0]);
        Assert.Equal(-0.25f, sink.Last.Axes[1]);
        Assert.Equal(1.0f, sink.Last.Axes[2]);
        Assert.Equal(-1.0f, sink.Last.Axes[3]);
        Assert.True(sink.Last.Buttons[0]);
        Assert.True(sink.Last.Buttons[4]);
        Assert.True(sink.Last.Buttons[16]);
        Assert.False(sink.Last.Buttons[1]);
    }
}
