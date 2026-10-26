#!/usr/bin/env python3
"""Measure the straight-line trim at each power level, in the units the firmware table uses.

Reuses the robot access and run logic of tune_straight.py (S3 recovery AP, raw unequal-track
commands that bypass the firmware trim, BNO085 yaw rate). The x axis here is the trim itself:

  T   signed percent. T > 0 holds the LEFT track back, T < 0 the RIGHT. For power p and
      a = |T|, strong = p*(100+a)/100 (pinned at 100) and weak = strong*(100-a)/(100+a),
      exactly the ratio src/drive/StraightTrim.h applies, so the answer drops straight
      into FORWARD_TRIM / REVERSE_TRIM and works at 100% where a track is already pinned.

For each power the two directions alternate on every run so the robot stays in one area:
three coarse points around the current table value, a linear fit, then three refinement
points around the predicted zero crossing. The zero crossing does not depend on whether
the IMU yaw reads positive-left or positive-right.

  --join   wait for the Gladiator-Setup network, connect to it with the saved Windows
           profile, and run the preflight before anything moves
  --go     actually drive (otherwise: preflight only, nothing moves)
"""
import argparse
import importlib.util
import json
import statistics
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("ts", HERE / "tune_straight.py")
ts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ts)

SSID = "Gladiator-Setup"
POWERS = [25, 50, 75, 100]
RUN_SECONDS = {25: 3.0, 50: 2.8, 75: 2.4, 100: 2.2}
# Current table (interim) values, used only to centre the first three points.
CENTER = {"forward": {25: 12, 50: 12, 75: 6, 100: 0}, "reverse": {25: 20, 50: 20, 75: 10, 100: 0}}
MAX_T = 40
MIN_VOLTS = 12.8   # well above the firmware's 13.2 V lockout margin for a clean test


def ratio_tracks(direction, percent, power):
    """Track commands for trim percent `percent` (+ = left held back) at commanded `power`."""
    a = min(abs(percent), 60)
    strong = min(100, int(round(power * (100 + a) / 100)))
    weak = int(round(strong * (100 - a) / (100 + a)))
    if strong == weak:                 # equal tracks would engage the firmware trim
        weak = strong - 1
    left, right = (weak, strong) if percent >= 0 else (strong, weak)
    if direction == "reverse":
        left, right = -left, -right
    return int(left), int(right)


def trim_of(row):
    """Trim percent actually commanded in a measured row (+ = left held back)."""
    left, right = abs(row["left"]), abs(row["right"])
    return 100.0 * (right - left) / (right + left)


def fit(points):
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        raise RuntimeError("no spread in trim values")
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    intercept = my - slope * mx
    resid = [y - (slope * x + intercept) for x, y in points]
    sd = statistics.pstdev(resid) if len(resid) > 2 else 0.0
    return slope, intercept, sd


def join_network(log, timeout_s):
    log(f"waiting up to {timeout_s} s for {SSID} (power the robot now)")
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        out = subprocess.run(["netsh", "wlan", "show", "networks"], capture_output=True, text=True).stdout
        if SSID in out:
            subprocess.run(["netsh", "wlan", "connect", f"name={SSID}"], capture_output=True, text=True)
            time.sleep(4)
            try:
                ts.read_yaw_volts(ts.Robot(ts.HOST).sensors())
                log(f"joined {SSID}")
                return True
            except Exception:
                pass
        time.sleep(1)
    return False


def sweep_power(runner, power, log):
    ts.RUN_S = RUN_SECONDS[power]
    ts.SETTLE_S = 0.8
    ts.MIN_SAMPLES = 7 if power >= 75 else 10
    rows = {"forward": [], "reverse": []}
    log(f"==== power {power}% ====")

    def stage(name, offsets_by_dir):
        log(f"-- {name} --")
        for step in range(3):
            for direction in ("forward", "reverse"):     # out, then back over the same ground
                t = max(-MAX_T, min(MAX_T, offsets_by_dir[direction][step]))
                row = runner.execute(direction, t, power, log)
                row["trim"] = trim_of(row)
                if row["volts_min"] is not None and row["volts_min"] < MIN_VOLTS:
                    raise RuntimeError(f"battery fell to {row['volts_min']} V; stopping the test")
                rows[direction].append(row)

    stage("coarse", {d: [CENTER[d][power] + off for off in (-12, 0, 12)] for d in rows})
    predicted = {}
    for d in rows:
        slope, intercept, sd = fit([(r["trim"], r["rate"]) for r in rows[d]])
        if abs(slope) < 0.15:
            raise RuntimeError(f"{d} {power}%: veer barely changes with trim (slope {slope:+.3f}); data not usable")
        predicted[d] = max(-MAX_T, min(MAX_T, -intercept / slope))
        log(f"  {d} coarse: veer = {slope:+.3f}*T {intercept:+.2f}  -> zero at T={predicted[d]:+.1f}%")
    stage("refine", {d: [predicted[d] - 5, predicted[d], predicted[d] + 5] for d in rows})

    out = {}
    for d in rows:
        pts = [(r["trim"], r["rate"]) for r in rows[d]]
        slope, intercept, sd = fit(pts)
        zero = -intercept / slope
        note = "" if sd <= 8 else "  (NOISY - repeat this one)"
        log(f"== {d} {power}%: veer = {slope:+.3f}*T {intercept:+.2f}  residual sd {sd:.1f} deg/s  ->  T = {zero:+.1f}%{note}")
        out[d] = dict(power=power, slope=slope, intercept=intercept, sd=sd, trim_percent=zero,
                      rows=[{k: v for k, v in r.items()} for r in rows[d]])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--powers", default=",".join(map(str, POWERS)))
    ap.add_argument("--join", action="store_true", help="wait for the AP and connect to it")
    ap.add_argument("--join-timeout", type=int, default=600)
    ap.add_argument("--countdown", type=int, default=20)
    ap.add_argument("--go", action="store_true", help="actually drive")
    args = ap.parse_args()

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = HERE.parent / "diagnostics"
    lines = []

    def log(msg):
        print(msg, flush=True)
        lines.append(msg)

    if args.join and not join_network(log, args.join_timeout):
        sys.exit("never saw the Gladiator-Setup network")
    robot = ts.Robot(ts.HOST)
    try:
        yaw, volts = ts.read_yaw_volts(robot.sensors())
    except Exception as exc:
        sys.exit(f"cannot reach the robot ({exc})")
    log(f"preflight {stamp}: yaw={yaw} volts={volts}")
    if yaw is None:
        sys.exit("IMU game rotation vector not valid; not driving")
    if volts is None or volts < MIN_VOLTS + 0.5:
        sys.exit(f"battery reads {volts} V: connect a charged pack (the bench USB reading is ~4 V)")
    powers = [int(p) for p in args.powers.split(",")]
    if not args.go:
        log("DRY RUN: preflight only, nothing was driven")
        return

    log(f"STARTING in {args.countdown} s: set the robot down with clear ground ahead AND behind, hands off")
    time.sleep(args.countdown)
    runner, results = ts.Run(robot), {}
    try:
        for p in powers:
            results[p] = sweep_power(runner, p, log)
    except KeyboardInterrupt:
        log("interrupted")
    except Exception as exc:
        log(f"ABORTED: {exc}")
    finally:
        robot.stop()
        (out_dir / f"trim-sweep-{stamp}.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        (out_dir / f"trim-sweep-{stamp}.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
    if results:
        log("\nProposed tables (power 25 / 50 / 75 / 100), signed percent, + holds the LEFT track back:")
        for d, name in (("forward", "FORWARD_TRIM"), ("reverse", "REVERSE_TRIM")):
            vals = [int(round(results[p][d]["trim_percent"])) if p in results else None for p in POWERS]
            log(f"  {name} = {{{{{', '.join('?' if v is None else str(v) for v in vals)}}}}}")


if __name__ == "__main__":
    main()
