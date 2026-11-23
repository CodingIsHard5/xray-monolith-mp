import sys, time, os
shm, rid, times, crash_at, tag = sys.argv[2], sys.argv[3], float(sys.argv[4]), float(sys.argv[5]), sys.argv[6]
SP = 20.0; t0 = time.time(); seq = 0
while True:
    t = (time.time() - t0 - 2) * SP
    if t > times: open(f"out/tb_{tag}/done", "w").close(); sys.exit(0)   # like the real dump files: tb_run sees them
    if t >= crash_at:
        if os.environ.get("FAKE_HANG"): time.sleep(10**6)
        os._exit(9)
    seq += 1
    with open(shm + "/snap.txt", "wb") as f: f.write(f"seq,{seq},{int(t*1000)},{t:.2f},l01_escape,{rid}\na,1,2,3,0,1\nend,{seq}\n".encode())
    time.sleep(0.1)
