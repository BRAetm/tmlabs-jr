"""
inject_vx360_mirror.py
Run AFTER launching ZP and starting the engine.
Patches DS4.right_trigger_float to also fire VX360 RT so Xbox Remote Play receives inputs.

Usage:
    python inject_vx360_mirror.py <child_pid>
    python inject_vx360_mirror.py          (auto-detects largest ZP process)
"""
import frida, sys, time, psutil

def get_zp_child_pid():
    procs = [(p.pid, p.memory_info().rss)
             for p in psutil.process_iter(['pid','name','memory_info'])
             if 'ZP' in (p.info['name'] or '')]
    if not procs:
        raise RuntimeError("ZP process not found")
    return max(procs, key=lambda x: x[1])[0]

pid = int(sys.argv[1]) if len(sys.argv) > 1 else get_zp_child_pid()
print(f"[*] Attaching to ZP PID {pid}...")

code = '''
import gc

vx = None; ds4 = None
for obj in gc.get_objects():
    n = type(obj).__name__
    if n == "VX360Gamepad": vx = obj
    if n == "VDS4Gamepad":  ds4 = obj

if not vx:
    print("[VX360-MIRROR] VX360Gamepad not found — is engine started?")
elif not ds4:
    print("[VX360-MIRROR] No DS4 found — ZP may already fire VX360 directly")
else:
    orig = type(ds4).right_trigger_float
    def _patched(self, val, *a, **kw):
        orig(self, val, *a, **kw)
        if self is ds4:
            try: vx.right_trigger_float(val); vx.update()
            except: pass
    type(ds4).right_trigger_float = _patched
    print("[VX360-MIRROR] Patched — DS4 RT now mirrors to VX360 (Xbox Remote Play will see it)")
'''

dev = frida.get_local_device()
sess = dev.attach(pid)
scr = sess.create_script(f'''
var py      = Process.getModuleByName('python311.dll');
var ensure  = new NativeFunction(py.getExportByName('PyGILState_Ensure'),  'int',  []);
var release = new NativeFunction(py.getExportByName('PyGILState_Release'), 'void', ['int']);
var run     = new NativeFunction(py.getExportByName('PyRun_SimpleString'), 'int',  ['pointer']);
var buf     = Memory.allocUtf8String({repr(code)});
var g = ensure(); run(buf); release(g);
send('done');
''')
scr.on('message', lambda m, d: print(m.get('payload', m)))
scr.load()
time.sleep(3)
scr.unload()
sess.detach()
print("[*] Done.")
