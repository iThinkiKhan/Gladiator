> Historical engineering reference, imported from the CAD source snapshot of 2026-10-26.
> Later component snapshots are indexed in [hardware](../../hardware/README.md).

# Gladiator orientation — mast is FRONT

User decision, 2026-10-09: the mast/tower end is the robot's FRONT, replacing
the mast-rear convention. This is a frame change, not a request to rewire the
drivers, mirror printed parts, or modify the fabricated aluminum deck.

## Current robot frame

Keep the established CAD coordinates and dimensions: **+Y forward, +X right,
+Z up; origin at the rear-left lower-deck corner**. Deck bounds remain
X 0–79, Y 0–140. The mast center remains (39.5, 113), 27 mm from the FRONT
edge at Y 140. REAR edge is Y 0. Low X is LEFT; high X is RIGHT.

| Former mast-rear term | Current mast-front term | Unchanged location |
| --- | --- | --- |
| Rear/mast end | FRONT | Y 140, mast center Y 113 |
| Front/battery-end zone | REAR | Y 0; 21 mm battery gap |
| Rear battery gap | FRONT battery gap | Y 96.5–140; 43.5 mm |
| Physical left (high X) | RIGHT | `SideRailRight`, X 61–73; switch v1b stays here |
| Physical right (low X) | LEFT | `SideRailLeft`, low-X rail |
| Mast rear flat/wire window | Mast FRONT flat/wire window | +Y face |

Existing CAD object IDs, spreadsheet aliases (`front_slit_*`, `rear_pilot_*`,
`battery_front_gap`, etc.), dated measurements, and released print files are
legacy identifiers/evidence. They are deliberately not renamed or mirrored:
their coordinates, constraints, and manufactured dimensions remain authoritative.
Interpret directional chassis words in pre-2026-10-09 logs through this table.
Head-local FRONT means the ToF/radar viewing face; head-local REAR means the
display face. Those local face names do not change when the head pans.

## Drive and GPIO mapping

Physical wiring is unchanged; logical tracks exchange names and both reverse
polarity. `(oldLeft, oldRight) = (-newRight, -newLeft)`. All control transports
(BLE/phone, field/web, and gateway) use the same S3 output mapping; do not add a
second inversion to the phone app or C6 relay.

| Current logical track | Physical controller side | R_EN | L_EN | RPWM | LPWM |
| --- | --- | --- | --- | --- | --- |
| LEFT | S3 J3 / native USB | 2 | 2 (tied) | 42 | 41 |
| RIGHT | S3 J1 / USB-to-UART | 16 | 11 | 17 | 12 |

Both tracks are software-inverted in this frame. Existing calibration is
re-expressed, not remeasured: new forward favors LEFT by 14% (57/43 at 50%);
new reverse favors LEFT by 22% (-61/-39 at 50%). Turns and Overdrive stay
untrimmed. Forward moves toward the mast; left/right turns remain relative to
the new front. Physical confirmation must use a supported, tracks-clear robot.

IMU derived rotation and game-rotation Euler angles remove the historical mount
tilt and then right-compose a 180-degree chassis yaw rebase. Raw quaternions and
raw sensor-frame vectors remain untouched. This known frame transform is not a
measured sensor yaw calibration; the unknown sensor yaw offset stays unknown.

## Native CAD and head pose

The chassis master carries a non-manufacturing `RobotOrientation` datum with
explicit FRONT/RIGHT/UP vectors. The head-only master displays its sensor face toward
+Y using the already checked **legacy pan 180°, tilt 0°** pose. Fixed neck,
mast key, and servo carriage stay in place; pan/tilt solids rotate 180° and the
60:40 drive pulley/horn rotate 120°. No print geometry or existing plate changes.

Important: the current head's validated legacy pan interval is **-20° to 180°**.
Relative to mast-front it is **-200° to 0°**, so forward is an endpoint, NOT a
centered scanning/home range. Do not recenter servo indexing or extend travel
without a separate mechanical, cable-clearance, and limit review. The robot's
front decision does not authorize that redesign, and no head servo is moved by
this update. v05 manufacturing source remains at its legacy construction pose;
the orientation-aware head master is the current robot-facing viewing file.
