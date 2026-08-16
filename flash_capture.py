#!/usr/bin/env python3
"""
Flash the device AND capture boot serial output in one go.
Key: open serial FIRST, THEN flash (so bootloader logs aren't lost).
"""
import subprocess, serial, time, sys, os, threading

TTY = "/dev/ttyACM0"
BAUD = 115200

def read_serial(ser, result, stop_event, duration=15):
    """Background reader — collects lines until stop_event set or duration elapsed."""
    deadline = time.time() + duration
    while not stop_event.is_set() and time.time() < deadline:
        try:
            line = ser.readline().decode('utf-8', errors='replace').rstrip()
            if line:
                result.append(line)
        except Exception:
            pass

# ── 1. Open serial FIRST ────────────────────────────────────────────────────
print(f"[1/3] Opening {TTY} @ {BAUD}…")
try:
    ser = serial.Serial(TTY, BAUD, timeout=0.3)
    time.sleep(0.2)
    ser.reset_input_buffer()
    print("  Serial open, buffering…")
except Exception as e:
    print(f"  FAILED to open serial: {e}")
    sys.exit(1)

# ── 2. Start background reader ───────────────────────────────────────────────
result = []
stop_event = threading.Event()
reader = threading.Thread(target=read_serial, args=(ser, result, stop_event, 15), daemon=True)
reader.start()
print("[2/3] Background reader started…")

# ── 3. Flash ─────────────────────────────────────────────────────────────────
print("[3/3] Flashing…")
r = subprocess.run(
    ["bash", "-c",
     f". ~/esp/setup_env.sh && sg dialout -c 'idf.py -p {TTY} -b 460800 flash'"],
    capture_output=True, text=True, timeout=180)
print(r.stdout[-1000:] if r.stdout else "")
if r.stderr:
    print("STDERR:", r.stderr[-500:])
print(f"  Flash return code: {r.returncode}")
if r.returncode != 0:
    stop_event.set()
    ser.close()
    sys.exit(1)

# ── 4. Wait for more boot output ─────────────────────────────────────────────
print("  Waiting 10s for boot output…")
time.sleep(10)
stop_event.set()
reader.join(timeout=2)
ser.close()

# ── 5. Print relevant lines ──────────────────────────────────────────────────
keywords = ["DBG","draw_loading","panel_push","draw_bitmap","Starting",
            "SD card","file_browser","LCD fmt","FROZEN","F{","载入",
            "panel=","draw_bitmap=","DONE","SKIP","Error","WARN","NC2000",
            "nc2000_run","s_panel","fb=","NVS","ppa","Hardware keyboard",
            "Configuring","Loading","wqx loaded","main loop","No file selected",
            "Selected","Launching","boot_count","init","ili9341","brightness",
            "app_clear","free","alloc","heap"]
matched = [(i,l) for i,l in enumerate(result) if any(k in l for k in keywords)]
print(f"\n=== CAPTURED {len(result)} lines, {len(matched)} matched ===")
for i,l in matched:
    print(f"[{i:4d}] {l}")
print(f"\n--- ALL {len(result)} lines ---")
for i,l in enumerate(result):
    print(f"[{i:4d}] {l}")
