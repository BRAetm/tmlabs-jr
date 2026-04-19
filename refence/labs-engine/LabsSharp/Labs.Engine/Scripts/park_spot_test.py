import zmq, json, time, argparse
import cv2
import numpy as np
import mss, win32gui

parser = argparse.ArgumentParser()
parser.add_argument('--session', type=int, default=0)
parser.add_argument('--port', type=int, default=5556)
args = parser.parse_args()

PORT = args.port + args.session
SESSION = args.session

# ---- ZMQ OUTPUT (to your app) ----
ctx = zmq.Context()
sock = ctx.socket(zmq.PUB)
sock.bind(f"tcp://127.0.0.1:{PORT}")
time.sleep(0.5)

def send_state(axes=None, buttons=None):
    if axes is None: axes = [0.0, 0.0, 0.0, 0.0]
    if buttons is None: buttons = [False]*17
    sock.send_string(json.dumps({"axes": axes, "buttons": buttons}))

def tap_button(idx, hold=0.12):
    b = [False]*17
    b[idx] = True
    send_state(buttons=b)
    time.sleep(hold)
    send_state()

# ---- WINDOW CAPTURE ----
def get_hwnd():
    res = []
    def cb(hwnd, _):
        t = win32gui.GetWindowText(hwnd).lower()
        if "xbox" in t and win32gui.IsWindowVisible(hwnd):
            res.append(hwnd)
    win32gui.EnumWindows(cb, None)
    return res[SESSION] if len(res) > SESSION else None

def get_frame(hwnd):
    x, y, x2, y2 = win32gui.GetWindowRect(hwnd)
    with mss.mss() as sct:
        # crop out browser chrome
        m = {"top": y+80, "left": x, "width": x2-x, "height": y2-y-80}
        img = np.array(sct.grab(m))
        return cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)

# ---- SIMPLE DETECTORS ----
def is_in_game(frame):
    h, w, _ = frame.shape
    # look at thin strip at top center for scoreboard-ish brightness
    roi = frame[0:int(h*0.12), int(w*0.25):int(w*0.75)]
    gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
    bright = cv2.threshold(gray, 200, 255, cv2.THRESH_BINARY)[1]
    pct = cv2.countNonZero(bright)/(bright.size+1e-6)
    return pct > 0.03  # tweak

def is_on_court_spot(frame):
    h, w, _ = frame.shape
    # look near bottom center for light circular "Got Next" spot
    roi = frame[int(h*0.7):int(h*0.95), int(w*0.35):int(w*0.65)]
    hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
    # generic light floor/spot colors
    mask = cv2.inRange(hsv, np.array([0,0,160]), np.array([180,60,255]))
    pct = cv2.countNonZero(mask)/(mask.size+1e-6)
    return pct > 0.10  # tweak

# ---- MAIN LOOP ----
STATE_PARK_IDLE = 0
STATE_MOVING_TO_SPOT = 1
STATE_ON_SPOT_WAITING = 2
STATE_IN_GAME = 3

state = STATE_PARK_IDLE
last_move_time = 0

print(f"[Session {SESSION}] park spot test starting")
hwnd = None
while hwnd is None:
    hwnd = get_hwnd()
    print("  Waiting for Xbox window...")
    time.sleep(1)

print("  Found window. Starting loop...")

while True:
    frame = get_frame(hwnd)

    ingame = is_in_game(frame)
    onspot = is_on_court_spot(frame)

    # STATE UPDATES
    if ingame:
        state = STATE_IN_GAME
    else:
        if onspot:
            state = STATE_ON_SPOT_WAITING
        elif state == STATE_IN_GAME:
            # game ended back to park
            state = STATE_PARK_IDLE

    # BEHAVIOR
    now = time.time()

    if state == STATE_PARK_IDLE:
        # every few seconds, walk right/up toward a spot & tap A
        if now - last_move_time > 2.0:
            print("  [PARK_IDLE] Moving toward spot...")
            # left stick right-up
            send_state(axes=[0.7, -0.7, 0, 0])
            time.sleep(0.6)
            send_state()  # stop
            tap_button(0) # A
            last_move_time = now
            state = STATE_MOVING_TO_SPOT

    elif state == STATE_MOVING_TO_SPOT:
        if onspot:
            print("  [MOVING_TO_SPOT] Detected on spot")
            state = STATE_ON_SPOT_WAITING
        # else just chill and wait for next frame

    elif state == STATE_ON_SPOT_WAITING:
        # do nothing; just heartbeat so you don't time out
        send_state()
        # optional: if not onspot for a while, go back to idle
        if not onspot and now - last_move_time > 8:
            print("  [ON_SPOT] Lost spot, back to idle")
            state = STATE_PARK_IDLE

    elif state == STATE_IN_GAME:
        # For now just neutral; you can add anti-idle movement here
        send_state()

    # Debug window if you want
    cv2.imshow(f"Session {SESSION}", cv2.resize(frame, (480, 270)))
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

    time.sleep(0.05)  # ~20 fps

cv2.destroyAllWindows()
