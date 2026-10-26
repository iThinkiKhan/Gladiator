#!/usr/bin/env python3
"""Measure and fit the straight-line track balance on the S3 recovery AP.

Reproduces the 2026-09-08 method: raw, unequal-track runs (which bypass
applyStraightTrim in src/main.cpp), BNO085 game-rotation-vector yaw rate, a
linear fit of veer rate against d = left - right, and the balance point.

  d       signed left - right percent (reverse uses negative track values)
  veer    yaw rate in deg/s from the game rotation vector; positive = yaw
          increasing, which the 2026-09-08 logs called "drifts LEFT"

Two stages, and the directions alternate on every step (forward, then back over
the same ground) so the robot stays in one area: a coarse three-point bracket
around the currently shipped trim, a fit per direction, then a three-point
refinement around each predicted balance point. Every run is
regressed over the settled part of the run, not just endpoint to endpoint, so
the 5%/20 ms soft start does not bias the rate.

Without --go this only prints the plan and reads sensors; it never moves the
robot. With --go, every exit path (error, Ctrl+C, runaway yaw) sends `stop`.

Join the Wi-Fi network `Gladiator-Setup` first (the S3 recovery AP,
192.168.4.1; it is up at boot and for a short time before it sheds itself).
"""
import argparse
import http.client
import json
import statistics
import sys
import threading
import time
import urllib.parse
from datetime import datetime
from pathlib import Path

HOST = "192.168.4.1"
RESEND_S = 0.15          # firmware watchdog is 350 ms; stay well inside it
RUN_S = 2.8              # same duration as the 2026-09-08 sweeps
SETTLE_S = 0.8           # ignore ramp-up and track-grip transients
REST_S = 3.0
RUNAWAY_DEG = 150.0      # abort a run that has turned this far
MIN_SAMPLES = 10

# Currently shipped constants (src/main.cpp), expressed as d at power p.
SHIPPED_FORWARD_TRIM = 12
SHIPPED_REVERSE_TRIM = 14


class Robot:
    def __init__(self, host):
        self.host = host
        self.cmd_conn = http.client.HTTPConnection(host, 80, timeout=3)
        self.sen_conn = http.client.HTTPConnection(host, 80, timeout=3)

    def _post(self, conn, path, fields):
        body = urllib.parse.urlencode(fields)
        conn.request("POST", path, body,
                     {"Content-Type": "application/x-www-form-urlencoded"})
        r = conn.getresponse()
        return r.status, r.read().decode("utf-8", "replace")

    def command(self, text):
        return self._post(self.cmd_conn, "/api/command", {"command": text})

    def stop(self):
        for _ in range(3):
            try:
                self.command("stop")
                return
            except Exception:
                self.cmd_conn.close()
                self.cmd_conn = http.client.HTTPConnection(self.host, 80, timeout=3)
                time.sleep(0.1)

    def sensors(self):
        self.sen_conn.request("GET", "/api/sensors")
        r = self.sen_conn.getresponse()
        return json.loads(r.read().decode("utf-8", "replace"))


def find_key(node, key):
    if isinstance(node, dict):
        if key in node:
            return node[key]
        for v in node.values():
            found = find_key(v, key)
            if found is not None:
                return found
    elif isinstance(node, list):
        for v in node:
            found = find_key(v, key)
            if found is not None:
                return found
    return None


def read_yaw_volts(snapshot):
    game = find_key(snapshot, "gameDerived")
    yaw = game.get("yawDeg") if isinstance(game, dict) and game.get("valid") else None
    return yaw, find_key(snapshot, "busVolts")


def tracks(direction, d, power):
    """Track commands for a signed left-right difference d at mean `power`."""
    left_mag = power + d / 2 if direction == "forward" else power - d / 2
    right_mag = power - d / 2 if direction == "forward" else power + d / 2
    left, right = round(left_mag), round(right_mag)
    if left == right:           # equal tracks would engage the trim; avoid it
        left += 1
    if direction == "reverse":
        left, right = -left, -right
    return int(left), int(right)


def fit_line(points):
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    return slope, my - slope * mx


def unwrap(yaws):
    out, offset = [yaws[0]], 0.0
    for prev, cur in zip(yaws, yaws[1:]):
        step = cur - prev
        if step > 180:
            offset -= 360
        elif step < -180:
            offset += 360
        out.append(cur + offset)
    return out


class Run:
    def __init__(self, robot):
        self.robot = robot

    def execute(self, direction, d, power, log):
        left, right = tracks(direction, d, power)
        actual_d = left - right
        samples, stop_flag = [], threading.Event()

        def poll():
            while not stop_flag.is_set():
                try:
                    yaw, volts = read_yaw_volts(self.robot.sensors())
                    if yaw is not None:
                        samples.append((time.monotonic(), yaw, volts))
                except Exception:
                    pass

        t = threading.Thread(target=poll, daemon=True)
        t.start()
        time.sleep(0.4)                          # baseline samples while safe
        begin = time.monotonic()
        aborted = None
        try:
            while time.monotonic() - begin < RUN_S:
                status, text = self.robot.command(f"drive {left} {right}")
                if status != 200:
                    aborted = f"drive refused: {status} {text.strip()}"
                    break
                ys = [s[1] for s in samples if s[0] >= begin - 0.4]
                if len(ys) > 3 and abs(unwrap(ys)[-1] - unwrap(ys)[0]) > RUNAWAY_DEG:
                    aborted = "runaway yaw"
                    break
                time.sleep(RESEND_S)
        finally:
            self.robot.stop()
            time.sleep(0.4)
            stop_flag.set()
            t.join(timeout=3)

        if aborted:
            raise RuntimeError(aborted)
        window = [s for s in samples if begin + SETTLE_S <= s[0] <= begin + RUN_S]
        if len(window) < MIN_SAMPLES:
            raise RuntimeError(f"only {len(window)} IMU samples; link too slow")
        yaws = unwrap([s[1] for s in window])
        times = [s[0] - begin for s in window]
        slope, _ = fit_line(list(zip(times, yaws)))
        volts = [s[2] for s in samples if s[2] is not None]
        row = dict(direction=direction, power=power, left=left, right=right,
                   d=actual_d, rate=slope, samples=len(window),
                   volts_min=min(volts) if volts else None)
        log(f"  {direction:7s} ({left:4d},{right:4d})  d={actual_d:+4d}  "
            f"veer {slope:+7.2f} deg/s [{len(window)} samples, "
            f"min {row['volts_min']} V]")
        time.sleep(REST_S)
        return row


def shipped_trim(direction):
    return SHIPPED_FORWARD_TRIM if direction == "forward" else SHIPPED_REVERSE_TRIM


def shipped_d(direction, power):
    sign = -1 if direction == "forward" else 1
    return sign * 2 * power * shipped_trim(direction) / 100


def fit_direction(direction, rows, power, log):
    pts = [(r["d"], r["rate"]) for r in rows if r["direction"] == direction]
    slope, intercept = fit_line(pts)
    # The balance point is a zero crossing, so it does not depend on whether
    # yaw reads positive-left or positive-right; only a flat fit is untrustworthy.
    if abs(slope) < 0.3:
        raise RuntimeError(f"{direction}: fitted slope {slope:+.3f} is too flat; "
                           "data too noisy to trust. Re-run on a level, clear surface.")
    return slope, intercept, -intercept / slope


def sweep(runner, directions, power, log):
    """Alternate directions on every step so the robot stays in one area."""
    rows = []

    def stage(name, offsets, centers):
        log(f"-- {name} --")
        for off in offsets:
            for direction in directions:       # forward, then back over the same ground
                rows.append(runner.execute(direction, centers[direction] + off, power, log))

    stage("coarse", (-12, 0, 12), {d: shipped_d(d, power) for d in directions})
    predicted = {}
    for direction in directions:
        slope, intercept, predicted[direction] = fit_direction(direction, rows, power, log)
        log(f"  {direction} coarse fit: rate = {slope:+.4f}*d {intercept:+.3f}  "
            f"-> balance d={predicted[direction]:+.1f}")
    stage("refine", (-4, 0, 4), predicted)

    results = []
    for direction in directions:
        slope, intercept, balance = fit_direction(direction, rows, power, log)
        trim = abs(balance) * 50 / power
        residual = slope * shipped_d(direction, power) + intercept
        log(f"== {direction.upper()}: rate = {slope:+.4f}*d {intercept:+.3f}")
        log(f"   BALANCE d={balance:+.2f}  ->  trim constant {trim:.1f}% "
            f"(shipped {shipped_trim(direction)}%)")
        log(f"   with the shipped trim it would veer {residual:+.2f} deg/s "
            f"({'LEFT' if residual > 0 else 'RIGHT'} if positive yaw = left)")
        results.append(dict(direction=direction, power=power, slope=slope,
                            intercept=intercept, balance_d=balance, trim_percent=trim,
                            shipped=shipped_trim(direction),
                            shipped_residual_deg_s=residual,
                            rows=[r for r in rows if r["direction"] == direction]))
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--power", type=int, default=50,
                    help="mean track power for the sweep (default 50)")
    ap.add_argument("--directions", default="forward,reverse")
    ap.add_argument("--go", action="store_true",
                    help="actually drive; without it nothing moves")
    args = ap.parse_args()

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = Path(__file__).resolve().parent.parent / "diagnostics"
    log_path = out_dir / f"tune-straight-{stamp}.log"
    lines = []

    def log(msg):
        print(msg, flush=True)
        lines.append(msg)

    robot = Robot(args.host)
    try:
        yaw, volts = read_yaw_volts(robot.sensors())
    except Exception as exc:
        sys.exit(f"Cannot reach {args.host} ({exc}). Join Gladiator-Setup first.")
    log(f"start {stamp}: yaw={yaw} volts={volts} power={args.power}")
    if yaw is None:
        sys.exit("IMU game rotation vector not valid; not driving.")
    if volts is not None and volts < 13.5:
        sys.exit(f"Battery at {volts} V is too low for a meaningful sweep.")

    if not args.go:
        log("DRY RUN (no --go): sensors read OK, nothing was driven.")
        for direction in args.directions.split(","):
            log(f"  would run 6 x {RUN_S}s {direction} at {args.power}% around "
                f"shipped trim {shipped_trim(direction)}%, alternating directions")
        return

    log("positive veer = yaw increasing (the 2026-09-08 logs called this LEFT)")
    runner, results = Run(robot), []
    try:
        log("Place the robot with ~3 m clear ahead AND behind; starting in 8 s")
        time.sleep(8)
        results = sweep(runner, args.directions.split(","), args.power, log)
    except KeyboardInterrupt:
        log("interrupted")
    except Exception as exc:
        log(f"ABORTED: {exc}")
    finally:
        robot.stop()
        log_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        (out_dir / f"tune-straight-{stamp}.json").write_text(
            json.dumps(results, indent=2), encoding="utf-8")
        print(f"log: {log_path}")


if __name__ == "__main__":
    main()
