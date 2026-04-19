/**
 * Frida script — nba2k_helper.pyd (Cython CV bot)
 * Intercepts Helper methods, webhook POSTs, and image send paths
 * Usage: frida -l frida_nba2k_helper.js -n Helios2.exe
 *        (attach after the script loads the .pyd)
 */

'use strict';

function readStr(ptr) {
    if (!ptr || ptr.isNull()) return '<null>';
    try { return ptr.readUtf8String() || ptr.readAnsiString() || '<binary>'; }
    catch (_) { return '<unreadable>'; }
}

// ── Python C-API hooks ────────────────────────────────────────────────────────
// We hook PyObject_CallMethod to catch Helper.run / meter_run / skele_run / etc.

const python_dll = Process.findModuleByName('python311.dll')
                || Process.findModuleByName('python310.dll')
                || Process.findModuleByName('python39.dll')
                || Process.findModuleByName('python38.dll');

if (python_dll) {
    console.log(`[*] Found Python: ${python_dll.name}`);

    // Hook PyRun_StringFlags to catch eval'd code
    const PyRun_StringFlags = Module.findExportByName(python_dll.name, 'PyRun_StringFlags');
    if (PyRun_StringFlags) {
        Interceptor.attach(PyRun_StringFlags, {
            onEnter(args) {
                const code = readStr(args[0]);
                if (code && code.length < 512)
                    console.log(`\n[PyRun_StringFlags] code="${code}"`);
            }
        });
    }

    // Hook requests.post / requests.get via urllib3 at the C layer is complex;
    // instead hook WinInet directly (done in main script).
    // Here we hook the nba2k_helper exported PyInit to know when it loads.
    const PyInit_nba2k = Module.findExportByName('nba2k_helper.cp311-win_amd64.pyd', 'PyInit_nba2k_helper');
    if (PyInit_nba2k) {
        Interceptor.attach(PyInit_nba2k, {
            onLeave(retval) {
                console.log(`\n[*] nba2k_helper module initialized → module obj @ ${retval}`);
            }
        });
    }
}

// ── requests library: hook libcurl send (used in some .pyd builds) ───────────
const libcurl = Process.findModuleByName('libcurl.dll');
if (libcurl) {
    const curl_easy_setopt = Module.findExportByName('libcurl.dll', 'curl_easy_setopt');
    const curl_easy_perform = Module.findExportByName('libcurl.dll', 'curl_easy_perform');

    const CURLOPT_URL = 10002;
    const CURLOPT_POSTFIELDS = 10015;
    const CURLOPT_HTTPHEADER = 10023;

    if (curl_easy_setopt) {
        Interceptor.attach(curl_easy_setopt, {
            onEnter(args) {
                const opt = args[1].toUInt32();
                const val = args[2];
                if (opt === CURLOPT_URL)
                    console.log(`\n[curl] URL = ${readStr(val)}`);
                else if (opt === CURLOPT_POSTFIELDS)
                    console.log(`[curl] POST body = ${readStr(val)}`);
            }
        });
    }

    if (curl_easy_perform) {
        Interceptor.attach(curl_easy_perform, {
            onEnter(args) { console.log(`\n[curl_easy_perform] handle=${args[0]}`); },
            onLeave(retval) { console.log(`  => CURLcode ${retval.toUInt32()}`); }
        });
    }
}

// ── Discord webhook POST detection ────────────────────────────────────────────
// The webhook URL is in the pyd. We scan memory for it and log surrounding context.
const WEBHOOK_SIGNATURE = 'discord.com/api/webhooks/';

Process.enumerateModules().forEach(mod => {
    if (!mod.name.toLowerCase().includes('nba2k')) return;
    try {
        Memory.scan(mod.base, mod.size, WEBHOOK_SIGNATURE, {
            onMatch(address, size) {
                const fullUrl = address.readAnsiString(200) || '';
                console.log(`\n[WEBHOOK] Found in ${mod.name} @ ${address}: ${fullUrl.split('\0')[0]}`);
            },
            onComplete() {}
        });
    } catch (_) {}
});

// ── OpenCV frame capture: hook VideoRingBuffer reads ─────────────────────────
// When inference sees frames, GCVData flows through shared memory.
// We log ring buffer read events from HeliosCore.
const hcore = Process.findModuleByName('HeliosCore.dll');
if (hcore) {
    // Find VideoRingBufferReader::read (mangled name pattern)
    const exports = Module.enumerateExports('HeliosCore.dll');
    exports.forEach(exp => {
        if (exp.name && exp.name.includes('VideoRingBuffer') && exp.name.includes('read')) {
            console.log(`[*] Hooking VideoRingBuffer: ${exp.name}`);
            Interceptor.attach(exp.address, {
                onLeave(retval) {
                    console.log(`[VideoRingBuffer::read] => ${retval}`);
                }
            });
        }
    });
}

console.log('\n[*] nba2k_helper Frida script loaded');
