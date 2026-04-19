using System;
using System.Globalization;
using System.Text;
using System.Threading;
using Labs.Engine.Core;
using Microsoft.Web.WebView2.Core;

namespace Labs.Engine.Scripting;

/// <summary>
/// IGamepadSink for WebView2-hosted xCloud / Xbox Remote Play tabs.
///
/// Uses WebView2's built-in ExecuteScriptAsync rather than spinning up
/// a separate Chrome with --remote-debugging-port. Same JS shim as
/// CdpGamepadInjector, just delivered through the WebView2 JS bridge.
///
/// Caller wires this in once the page has navigated to xbox.com/play
/// (or whichever target). Call <see cref="InjectShim"/> on Navigation
/// Completed to (re)install window.__cvGamepad.
/// </summary>
public sealed class WebView2GamepadSink : IGamepadSink
{
    private const string GamepadShim = @"(function(){
  if (window.__cvGamepad) return;
  // Synthetic pad — exposed only when no real controller is plugged in.
  const gp = {
    axes: [0,0,0,0],
    buttons: Array(17).fill(null).map(()=>({pressed:false,value:0,touched:false})),
    connected: true,
    id: 'Xbox Wireless Controller (STANDARD GAMEPAD Vendor: 045e Product: 02fd)',
    index: 0,
    mapping: 'standard',
    timestamp: performance.now(),
    vibrationActuator: { type: 'dual-rumble', playEffect: function(){return Promise.resolve('complete');}, reset: function(){return Promise.resolve('complete');} }
  };
  const orig = navigator.getGamepads.bind(navigator);
  const AXIS_OVR_THRESH = 0.15;
  Object.defineProperty(navigator, 'getGamepads', {
    value: function(){
      let real = [];
      try { real = Array.from(orig()); } catch(e) {}
      const realPad = real.find(p => p && p.connected) || null;
      gp.timestamp = performance.now();
      if (!realPad) {
        // No real controller — expose the script-only synthetic in slot 0.
        return [gp, null, null, null];
      }
      // Per-field merge: script asserts override real, otherwise real passes through.
      // Lets the user dribble (left stick / face buttons / idle right stick) while
      // the script is simultaneously holding the shot meter.
      const mergedButtons = new Array(17);
      for (let i = 0; i < 17; i++) {
        const sb = gp.buttons[i] || {pressed:false,value:0};
        const rb = (realPad.buttons && realPad.buttons[i]) || {pressed:false,value:0};
        if (sb.pressed || sb.value > 0.15) {
          mergedButtons[i] = {pressed:true, value: Math.max(sb.value||0, 1), touched:true};
        } else {
          mergedButtons[i] = {pressed: !!rb.pressed, value: rb.value||0, touched: !!rb.touched};
        }
      }
      const mergedAxes = [0,0,0,0];
      for (let i = 0; i < 4; i++) {
        const sv = gp.axes[i] || 0;
        const rv = (realPad.axes && realPad.axes[i]) || 0;
        mergedAxes[i] = (Math.abs(sv) > AXIS_OVR_THRESH) ? sv : rv;
      }
      const merged = {
        axes: mergedAxes,
        buttons: mergedButtons,
        connected: true,
        id: realPad.id || gp.id,
        index: 0,
        mapping: 'standard',
        timestamp: performance.now(),
        vibrationActuator: realPad.vibrationActuator || gp.vibrationActuator
      };
      return [merged, null, null, null];
    }, writable:true, configurable:true
  });
  window.__cvGamepad = gp;
  // Fire gamepadconnected ONCE when the page loads. Firing it every 500ms
  // (what the old shim did) caused xCloud to treat the pad as reconnecting
  // and drop input. If a real controller connects later, the browser will
  // dispatch its own gamepadconnected — we don't need to fake it.
  try {
    const ev = new GamepadEvent('gamepadconnected', { gamepad: gp });
    window.dispatchEvent(ev);
  } catch(e) {
    window.dispatchEvent(new Event('gamepadconnected', {bubbles:true}));
  }
  // ---- Keep-alive: make xCloud think the tab is always focused & visible ----
  // xCloud pauses with 'please click here to start playing' when focus/visibility
  // is lost. Lie to every API it could be checking.
  Object.defineProperty(document, 'hidden', { get: () => false, configurable: true });
  Object.defineProperty(document, 'visibilityState', { get: () => 'visible', configurable: true });
  try { document.hasFocus = function(){ return true; }; } catch(e) {}
  try { Object.defineProperty(document, 'hasFocus', { value: function(){ return true; }, configurable: true, writable: true }); } catch(e) {}
  // Swallow blur/visibilitychange/pagehide events before listeners see them.
  const swallow = function(e){ e.stopImmediatePropagation(); e.preventDefault && e.preventDefault(); };
  ['blur','visibilitychange','pagehide','freeze','webkitvisibilitychange'].forEach(function(t){
    window.addEventListener(t, swallow, true);
    document.addEventListener(t, swallow, true);
  });
  // If something assigns window.onblur directly, neuter it.
  try { Object.defineProperty(window, 'onblur', { set: function(){}, get: function(){ return null; }, configurable: true }); } catch(e) {}
  try { Object.defineProperty(document, 'onvisibilitychange', { set: function(){}, get: function(){ return null; }, configurable: true }); } catch(e) {}
  // Re-fire focus on an interval so xCloud pollers keep seeing the tab as active.
  setInterval(function(){
    try { window.dispatchEvent(new Event('focus')); } catch(e) {}
  }, 1000);
})();";

    private readonly int _sessionId;
    private CoreWebView2? _web;
    private int _sendInFlight; // 0/1 — drop frame if a send is busy

    public WebView2GamepadSink(int sessionId) { _sessionId = sessionId; }

    /// <summary>Bind the live CoreWebView2. Must be called from the UI thread.</summary>
    public void Attach(CoreWebView2 web)
    {
        _web = web;
        // Re-inject the shim on every page navigation so SPA reloads can't strip it.
        web.AddScriptToExecuteOnDocumentCreatedAsync(GamepadShim);
        InjectShim();
    }

    /// <summary>Manually re-inject the shim into the current page.</summary>
    public void InjectShim()
    {
        var w = _web;
        if (w == null) return;
        try { _ = w.ExecuteScriptAsync(GamepadShim); } catch { }
    }

    public bool IsConnected(int sessionId) => _web != null && sessionId == _sessionId;

    public void SendInput(int sessionId, GamepadState state)
    {
        if (_web == null || sessionId != _sessionId) return;
        if (Interlocked.Exchange(ref _sendInFlight, 1) == 1) return; // drop frame
        try
        {
            var js = BuildUpdateJs(state);
            // ExecuteScriptAsync must be called on the UI thread that owns the WebView2.
            // The CvWorkerHost dispatches on a thread pool — marshal back to UI dispatcher.
            var w = _web;
            System.Windows.Application.Current?.Dispatcher.BeginInvoke(new Action(() =>
            {
                try { _ = w.ExecuteScriptAsync(js); } catch { }
                finally { Interlocked.Exchange(ref _sendInFlight, 0); }
            }));
        }
        catch { Interlocked.Exchange(ref _sendInFlight, 0); }
    }

    private static string BuildUpdateJs(GamepadState s)
    {
        static string F(float v) => v.ToString("F4", CultureInfo.InvariantCulture);
        static string B(bool v)  => v ? "true" : "false";
        var sb = new StringBuilder(512);
        sb.Append("(function(){var g=window.__cvGamepad;if(!g)return;");
        for (int i = 0; i < 4 && i < s.Axes.Length; i++)
            sb.Append("g.axes[").Append(i).Append("]=").Append(F(s.Axes[i])).Append(';');
        for (int i = 0; i < 17 && i < s.Buttons.Length; i++)
        {
            sb.Append("g.buttons[").Append(i).Append("].pressed=").Append(B(s.Buttons[i])).Append(';');
            sb.Append("g.buttons[").Append(i).Append("].value=").Append(s.Buttons[i] ? "1" : "0").Append(';');
        }
        sb.Append("g.timestamp=performance.now();})();");
        return sb.ToString();
    }
}
