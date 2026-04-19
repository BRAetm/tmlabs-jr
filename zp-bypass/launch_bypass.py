import frida, time, sys

EXE = r"C:\Users\TM\Downloads\hackme.101(master level) by rashee\ZP HIGHER Lite(1).exe"
SCRIPT = open(r"C:\ghidra_work\ssl_ex_hook.js").read()
LOG = open(r"C:\ghidra_work\bypass_out.txt", "w", buffering=1)

def log(s):
    LOG.write(s + "\n")
    LOG.flush()

sessions = {}

def on_message(pid, msg, data):
    if msg.get("type") == "send":
        log(f"[{pid}] {msg['payload']}")
    elif msg.get("type") == "error":
        log(f"[ERR {pid}] {msg.get('description','')} {msg.get('stack','')[:300]}")

def inject(session, pid):
    try:
        scr = session.create_script(SCRIPT)
        scr.on("message", lambda m, d: on_message(pid, m, d))
        scr.load()
        sessions[pid] = (session, scr)
        log(f"Injected into PID {pid}")
    except Exception as e:
        log(f"Inject failed PID {pid}: {e}")

def on_child(child):
    log(f"Child spawned PID={child.pid}")
    try:
        sess = device.attach(child.pid)
        inject(sess, child.pid)
    except Exception as e:
        log(f"Attach child failed: {e}")
    device.resume(child.pid)

device = frida.get_local_device()
device.on("child-added", on_child)

pid = device.spawn(EXE)
log(f"Bootstrap PID={pid}")
sess = device.attach(pid)
sess.enable_child_gating()
inject(sess, pid)
device.resume(pid)

log("App launched. Enter any key in the login screen.")
try:
    while True: time.sleep(1)
except KeyboardInterrupt:
    for p, (s, sc) in sessions.items():
        try: sc.unload()
        except: pass
        try: s.detach()
        except: pass
    LOG.close()
