"""
Runtime source dumper via Frida - attaches to Helios2.exe and
dumps nba2k_helper method bodies by hooking PyObject_Call
"""
import frida, sys, json

SCRIPT = r"""
'use strict';

const pyDll = Process.findModuleByName('python311.dll');
if (!pyDll) { console.error('Python DLL not found'); }

// Hook PyObject_Call to intercept all Python-level calls
const PyObject_Call = Module.findExportByName(pyDll.name, 'PyObject_Call');

// We'll use PyRun_StringFlags to eval code in the Python interpreter
const PyRun_StringFlags = Module.findExportByName(pyDll.name, 'PyRun_StringFlags');
const PyGILState_Ensure = Module.findExportByName(pyDll.name, 'PyGILState_Ensure');
const PyGILState_Release = Module.findExportByName(pyDll.name, 'PyGILState_Release');
const PyBytes_AsString = Module.findExportByName(pyDll.name, 'PyBytes_AsString');
const PyUnicode_AsUTF8String = Module.findExportByName(pyDll.name, 'PyUnicode_AsUTF8String');
const Py_CompileString = Module.findExportByName(pyDll.name, 'Py_CompileString');

// Once nba2k_helper loads, inspect it
let dumped = false;
const PyInit = Module.findExportByName('nba2k_helper.cp311-win_amd64.pyd', 'PyInit_nba2k_helper');

if (PyInit) {
  Interceptor.attach(PyInit, {
    onLeave(retval) {
      if (dumped) return;
      dumped = true;
      
      const gstate = new NativeFunction(PyGILState_Ensure, 'int', [])();
      
      // Use PyRun_StringFlags to inspect the module
      const code = Memory.allocUtf8String(`
import nba2k_helper, inspect, dis, types, io

results = {}
h = nba2k_helper.Helper

for name in dir(h):
    if name.startswith('_'): continue
    obj = getattr(h, name, None)
    if obj is None: continue
    entry = {'name': name, 'type': str(type(obj))}
    try:
        entry['doc'] = obj.__doc__ or ''
    except: pass
    try:
        # Get bytecode via dis
        buf = io.StringIO()
        import sys
        old = sys.stdout
        sys.stdout = buf
        dis.dis(obj)
        sys.stdout = old
        entry['bytecode'] = buf.getvalue()
    except Exception as e:
        entry['bytecode_err'] = str(e)
    try:
        entry['code_consts'] = list(obj.__func__.__code__.co_consts) if hasattr(obj, '__func__') else []
        entry['code_names'] = list(obj.__func__.__code__.co_names) if hasattr(obj, '__func__') else []
        entry['code_varnames'] = list(obj.__func__.__code__.co_varnames) if hasattr(obj, '__func__') else []
    except: pass
    results[name] = entry

import json
print('DUMP_START')
print(json.dumps(results, default=str))
print('DUMP_END')
`);

      // Py_file_input = 257
      const PyRun_fn = new NativeFunction(PyRun_StringFlags, 'pointer', ['pointer','int','pointer','pointer','pointer']);
      // We need globals/locals - use Py_None for now, will use main module dict
      // This is complex; instead hook the simpler path
      
      const release = new NativeFunction(PyGILState_Release, 'void', ['int']);
      release(gstate);
      
      console.log('[*] nba2k_helper initialized - use frida REPL to inspect');
      send({type: 'module_init', addr: retval.toString()});
    }
  });
}

// Simpler: hook stdout writes from Python to capture print output
const WriteFile = Module.findExportByName('KERNEL32.dll', 'WriteFile');
if (WriteFile) {
  Interceptor.attach(WriteFile, {
    onEnter(args) {
      // fd=1 is stdout
      const handle = args[0];
      const buf = args[1];
      const len = args[2].toUInt32();
      if (len > 0 && len < 4096 && !buf.isNull()) {
        try {
          const s = buf.readAnsiString(len);
          if (s && (s.includes('Helper') || s.includes('nba2k') || s.includes('meter') || s.includes('skele')))
            console.log('[stdout] ' + s);
        } catch(_) {}
      }
    }
  });
}
""";

def on_message(msg, data):
    print(f"[frida] {msg}")

device = frida.get_local_device()
try:
    session = device.attach("Helios2.exe")
    script = session.create_script(SCRIPT)
    script.on('message', on_message)
    script.load()
    print("[*] Attached to Helios2.exe. Press Ctrl+C to stop.")
    sys.stdin.read()
except frida.ProcessNotFoundError:
    print("[-] Helios2.exe not running. Start it first.")
except KeyboardInterrupt:
    print("\n[*] Done.")
