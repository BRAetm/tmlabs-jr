/**
 * Frida instrumentation script for Helios II
 * Hooks: BCrypt ops, WinInet HTTP, Session token handling, InferenceCore API
 * Usage: frida -l frida_helios_trace.js -f "lib/Helios2.exe" --no-pause
 *        OR attach: frida -l frida_helios_trace.js -n Helios2.exe
 */

'use strict';

// ── Helpers ──────────────────────────────────────────────────────────────────
function hexdump_short(ptr, len) {
    try { return hexdump(ptr, { length: Math.min(len, 64), header: false, ansi: false }); }
    catch (_) { return '<unreadable>'; }
}
function readStr(ptr) {
    if (ptr.isNull()) return '<null>';
    try { return ptr.readUtf8String() || ptr.readAnsiString(); }
    catch (_) { return '<unreadable>'; }
}

// ── 1. BCrypt hooks (InferenceCore.dll + HeliosCore.dll) ─────────────────────
const BCrypt = {
    BCryptOpenAlgorithmProvider: null,
    BCryptGenerateSymmetricKey: null,
    BCryptEncrypt: null,
    BCryptDecrypt: null,
    BCryptHash: null,
};

// Resolve from bcrypt.dll
for (const fn of Object.keys(BCrypt)) {
    BCrypt[fn] = Module.findExportByName('bcrypt.dll', fn);
}

if (BCrypt.BCryptDecrypt) {
    Interceptor.attach(BCrypt.BCryptDecrypt, {
        onEnter(args) {
            this.hKey    = args[0];
            this.pbInput = args[1];
            this.cbInput = args[2].toUInt32();
            this.pbOutput= args[5];
            console.log(`\n[BCryptDecrypt] hKey=${this.hKey} cbInput=${this.cbInput}`);
            if (this.cbInput > 0 && !this.pbInput.isNull())
                console.log('  Input:\n' + hexdump_short(this.pbInput, this.cbInput));
        },
        onLeave(retval) {
            if (retval.toUInt32() === 0 && !this.pbOutput.isNull()) {
                const cbOut = this.cbInput; // rough estimate
                console.log(`  [BCryptDecrypt] SUCCESS - Output:\n` + hexdump_short(this.pbOutput, cbOut));
            } else {
                console.log(`  [BCryptDecrypt] FAILED status=0x${retval.toUInt32().toString(16)}`);
            }
        }
    });
}

if (BCrypt.BCryptEncrypt) {
    Interceptor.attach(BCrypt.BCryptEncrypt, {
        onEnter(args) {
            this.cbInput = args[2].toUInt32();
            this.pbInput = args[1];
            console.log(`\n[BCryptEncrypt] cbInput=${this.cbInput}`);
            if (this.cbInput > 0) console.log('  Input:\n' + hexdump_short(this.pbInput, this.cbInput));
        }
    });
}

if (BCrypt.BCryptHash) {
    Interceptor.attach(BCrypt.BCryptHash, {
        onEnter(args) {
            const cbSecret = args[2].toUInt32();
            const pbSecret = args[1];
            const cbInput  = args[4].toUInt32();
            const pbInput  = args[3];
            if (cbSecret > 0 && !pbSecret.isNull())
                console.log(`\n[BCryptHash] secret(${cbSecret}):\n` + hexdump_short(pbSecret, cbSecret));
            if (cbInput > 0 && !pbInput.isNull())
                console.log(`  input(${cbInput}):\n` + hexdump_short(pbInput, cbInput));
        }
    });
}

// ── 2. WinInet HTTP hooks ─────────────────────────────────────────────────────
const wininet = 'WININET.dll';

const HttpSendRequestA = Module.findExportByName(wininet, 'HttpSendRequestA');
if (HttpSendRequestA) {
    Interceptor.attach(HttpSendRequestA, {
        onEnter(args) {
            // args: hRequest, lpszHeaders, dwHeadersLength, lpOptional, dwOptionalLength
            const headers = args[1].isNull() ? '' : args[1].readAnsiString(args[2].toUInt32()) || '';
            const bodyLen = args[4].toUInt32();
            const body    = bodyLen > 0 ? hexdump_short(args[3], bodyLen) : '';
            console.log(`\n[HttpSendRequest] hReq=${args[0]}`);
            if (headers) console.log('  Headers:\n  ' + headers.replace(/\n/g, '\n  '));
            if (body)    console.log('  Body:\n' + body);
        },
        onLeave(retval) {
            console.log(`  [HttpSendRequest] => ${retval}`);
        }
    });
}

const HttpOpenRequestA = Module.findExportByName(wininet, 'HttpOpenRequestA');
if (HttpOpenRequestA) {
    Interceptor.attach(HttpOpenRequestA, {
        onEnter(args) {
            // args: hConnect, lpszVerb, lpszObjectName, ...
            const verb = readStr(args[1]);
            const path = readStr(args[2]);
            console.log(`\n[HttpOpenRequest] ${verb} ${path}`);
        }
    });
}

const InternetConnectA = Module.findExportByName(wininet, 'InternetConnectA');
if (InternetConnectA) {
    Interceptor.attach(InternetConnectA, {
        onEnter(args) {
            const host = readStr(args[1]);
            const port = args[2].toUInt32();
            console.log(`\n[InternetConnect] host=${host} port=${port}`);
        }
    });
}

const InternetReadFile = Module.findExportByName(wininet, 'InternetReadFile');
if (InternetReadFile) {
    Interceptor.attach(InternetReadFile, {
        onEnter(args) {
            this.buf = args[1];
            this.dwNumberOfBytesRead = args[3];
        },
        onLeave(retval) {
            if (retval.toUInt32() !== 0) {
                const n = this.dwNumberOfBytesRead.readU32();
                if (n > 0) {
                    console.log(`[InternetReadFile] ${n} bytes received:`);
                    console.log(hexdump_short(this.buf, n));
                }
            }
        }
    });
}

// ── 3. InferenceCore exported API ─────────────────────────────────────────────
const infcore_fns = [
    'infcore_init',
    'infcore_set_session',
    'infcore_load_model',
    'infcore_list_models',
    'infcore_build_engine',
    'infcore_start_inference',
    'infcore_stop_inference',
    'infcore_get_last_error',
    'infcore_get_debug_log',
];

for (const fn of infcore_fns) {
    // Try both DLL locations
    for (const dll of ['InferenceCore.dll', null]) {
        const addr = dll ? Module.findExportByName(dll, fn) : null;
        if (addr) {
            Interceptor.attach(addr, {
                onEnter(args) {
                    console.log(`\n[${fn}] arg0=${args[0]} arg1=${args[1]}`);
                    if (fn === 'infcore_set_session') {
                        // arg0 is likely session token string
                        console.log('  session_token: ' + readStr(args[0]));
                    }
                    if (fn === 'infcore_load_model') {
                        console.log('  model_path/uuid: ' + readStr(args[0]));
                    }
                },
                onLeave(retval) {
                    console.log(`  [${fn}] => ${retval}`);
                    if (fn === 'infcore_get_last_error' || fn === 'infcore_get_debug_log') {
                        console.log('  msg: ' + readStr(retval));
                    }
                }
            });
            break;
        }
    }
}

// ── 4. Session file / Registry access ────────────────────────────────────────
// Hook RegOpenKeyExA to catch HWID / session registry reads
const RegOpenKeyExA = Module.findExportByName('ADVAPI32.dll', 'RegOpenKeyExA');
if (RegOpenKeyExA) {
    Interceptor.attach(RegOpenKeyExA, {
        onEnter(args) {
            const subKey = readStr(args[1]);
            if (subKey && subKey.toLowerCase().includes('helios'))
                console.log(`\n[RegOpenKeyExA] key="${subKey}"`);
        }
    });
}

const RegQueryValueExA = Module.findExportByName('ADVAPI32.dll', 'RegQueryValueExA');
if (RegQueryValueExA) {
    Interceptor.attach(RegQueryValueExA, {
        onEnter(args) {
            this.valueName = readStr(args[1]);
            this.dataPtr   = args[4];
            this.dataLen   = args[5];
        },
        onLeave(retval) {
            if (retval.toUInt32() === 0 && !this.dataPtr.isNull()) {
                const len = this.dataLen.isNull() ? 64 : this.dataLen.readU32();
                console.log(`[RegQueryValueExA] "${this.valueName}" => ${hexdump_short(this.dataPtr, len)}`);
            }
        }
    });
}

// ── 5. IsDebuggerPresent (anti-debug bypass) ──────────────────────────────────
const IsDebuggerPresent = Module.findExportByName('KERNEL32.dll', 'IsDebuggerPresent');
if (IsDebuggerPresent) {
    Interceptor.replace(IsDebuggerPresent, new NativeCallback(function () {
        return 0; // always report: no debugger
    }, 'int', []));
    console.log('[*] IsDebuggerPresent patched → always returns 0');
}

// ── 6. CreateFileMappingW / OpenFileMappingW — shared memory segments ─────────
const CreateFileMappingW = Module.findExportByName('KERNEL32.dll', 'CreateFileMappingW');
if (CreateFileMappingW) {
    Interceptor.attach(CreateFileMappingW, {
        onEnter(args) {
            const name = args[5].isNull() ? '<anonymous>' : args[5].readUtf16String();
            if (name && name.includes('Helios'))
                console.log(`\n[CreateFileMapping] name="${name}" size=${args[4]}:${args[3]}`);
        }
    });
}

const OpenFileMappingW = Module.findExportByName('KERNEL32.dll', 'OpenFileMappingW');
if (OpenFileMappingW) {
    Interceptor.attach(OpenFileMappingW, {
        onEnter(args) {
            const name = args[2].isNull() ? '' : args[2].readUtf16String();
            if (name) console.log(`\n[OpenFileMapping] name="${name}"`);
        }
    });
}

console.log('\n[*] Helios II Frida instrumentation loaded');
console.log('[*] Hooks active: BCrypt, WinInet, InferenceCore, Registry, SharedMem, AntiDebug');
