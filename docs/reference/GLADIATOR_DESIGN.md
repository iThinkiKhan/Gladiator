> Historical engineering reference, imported from the CAD source snapshot of 2026-10-26.
> Later component snapshots are indexed in [hardware](../../hardware/README.md).

# Gladiator — consolidated design document

Single entry point for the whole vehicle: aluminum chassis, upper structure, mast, and the
modular sensor head. Replaces the previously separate `docs/upper-structure.md` design log
(retired, pointer left in its place) and summarizes the mast-head workstream's own documents,
which remain the source of truth for that subsystem's full detail and reasoning.

Master CAD file: `cad/master/Gladiator_Master.FCStd` on the CAD server
(the CAD workstation), fully parametric via its `Parameters` spreadsheet. STEP export at
`cad/master/Gladiator_Master.step`. Repo mirrored at github.com/iThinkiKhan/gladiator-cad.

**Two active workstreams share this repo and this master file:**
- **Chassis/upper-structure/mast** (this document's main subject) — edits
  `Gladiator_Master.FCStd` directly.
- **Mast head** (modular sensor head, pan/tilt neck, sensor carriers) — works in its own file
  tree under `cad/head/`, does not touch the master. See the Mast Head section below.

## Coordinate conventions

**2026-10-09: mast is FRONT.** +X right, +Y forward, +Z upward; origin is
the rear-left lower-deck corner. Front looks toward **positive Y** (edge Y 140).
Geometry and coordinates are unchanged. See [ORIENTATION.md](https://github.com/iThinkiKhan/gladiator-cad/blob/db25f68d589b59520871f2c27f7898625261f611/docs/ORIENTATION.md)
for current left/right, motor pins, sensor frame, and head pose. Older dated
design prose and parameter/object names below retain the former mast-rear
terminology; they are historical identifiers, not the current robot frame.

## Status at a glance

| Subsystem | State |
| --- | --- |
| Lower aluminum deck | Existing fabricated part, reverse-modeled in CAD. **Never add features to it** — see below. |
| Battery holder | Reference envelope only (open 4S 18650 holder, not a sealed box) |
| Side rails (x2) | **v2 tall (2026-09-27)** — 6.5 taller after the first build, front foot slot moved 3.5 back; not yet printed |
| Upper deck | Printed and good — unchanged part, now sits 6.5 higher (Z 54.5) on the v2 rails |
| Mast base + tube | **v2 (2026-09-27)** — tube top Z 130; base M2 holes 43.4 apart, tied to the corrected deck pilots; not yet printed |
| S3 + breadboard | Laid out on the upper deck, but the S3 hole pattern is wrong in the printed deck — see Known Issues |
| Driver mounts (x2) | Built — open frame, PCB-flush mounting, gusseted joint |
| Driver board reference envelope | Built (2026-09-17) — closes a real gap, see Known Issues |
| Power board shield | Built, but its *position* is still an open wiring question |
| Antenna pylon | Built — real through-panel SMA bulkhead mount |
| Mast head (sensor carrier, pan/tilt) | v0.1 review candidate **imported into the master** (2026-09-17) under `HeadCandidate_v01`, plus a mast anti-rotation key added to `MastTube` — see below |

## The aluminum lower deck is fixed hardware

`ChassisDeck` in the CAD file is a faithful reverse-model of an **already-fabricated** aluminum
plate — every slit, hole, and the tower opening were measured off the real part. **Do not add
features to it.** Any new mounting or adjustment need goes into a printed part instead (e.g. the
side rails' foot slots absorb a 3mm front/rear offset between two existing deck slots, rather than
asking for a new slot in the aluminum). Full measurement detail: `measurements/chassis.md`.

Key figures: deck 79 x 140 x 2mm. Battery holder (open 4-cell 18650, not sealed) occupies the full
width at Y 21-96.5, Z 2-21.5, leaving a 21mm-deep REAR zone and a 43.5mm-deep FRONT zone. Tower/mast
opening: Ø14 at (39.5, 113). Tracks sit about 5mm above the deck and extend about 50mm outboard of
each deck edge (overall vehicle width 179mm).

## Side rails

Two arched structural members, `SideRailLeft`/`SideRailRight`, spanning front-to-back and carrying
the upper deck above the battery. Mount through the aluminum deck's **outermost slits at both
ends** (not the pilot holes), via crosswise adjustment slots in the printed feet — never in the
aluminum.

- **Arch, not a portal**: front foot Y 2-18.5 rising as a near-vertical post (2.5 clear of the
  battery holder's front face at Y 21) to a flat soffit at **Z 36.5 — 15.0 clear of the battery
  top** — then a faceted haunch back down to a rear foot Y 119-135.
- **v2 tall (2026-09-27), after the first physical build.** The printed v1 rails had a Z 30
  soffit (8.5 clear); Jim asked for about 75% more, so 15.0. Everything the rails carry rose by
  the same 6.5 (upper deck, S3/breadboard, antenna post, driver mounts). **Installed cells stand
  4 above the holder** (Jim, 2026-09-27; `BatteryCells`, `battery_cell_protrusion`). So the real
  gap to the cells was 4.5 on v1 and is 11.0 on v2. The 15.0 figure is to the empty holder.
  **Corrected 2026-10-01 (Jim):** installed cells are negligibly over the holder rim. `battery_cell_protrusion`
  is now 0.5, a conservative stand-in, not a measurement, so the gap under the v2 soffit is about 14.5. The 4, 4.5
  and 11.0 above are superseded.
- **Front foot slot moved 3.5 back (v2).** The right front screw would not quite pass the
  aluminium slit at the old Y 10 position. The slit is 33 long, so a screw further back always
  finds it: the crosswise slot is now centred at Y 13.5 (shank front edge Y 12, was 8.5), which
  passes any real slit that starts up to 4.5 behind the modelled Y 7.5. The foot and its well
  were lengthened to carry it (well Y 5-16.5, 2.0 wall to the post face). A T-shaped slot that
  kept the old position as well was tried and rejected — the head bore on only 1.8 mm2 where
  the two slots met; the plain shifted slot keeps the ~6.3 mm2 every foot has had.
- **Front foot head relief (v2).** In the v1 foot an M3 socket head over the slit centre line
  overlapped the outboard skin (5.8 mm3 at X 10.5, still 1.2 mm3 at X 11, the furthest inboard
  the shank can go in a 4 wide slit), so it could never seat flat. A 1.3 relief into the
  outboard wall of the front well (Z 6-9.5, skin 1.2 there) clears it at every X across the slit.
  Rear feet were already clear.
- The foot screws are driven through the **open inboard side of each well**. The Ø6 "driver
  shafts" from the rail top stop at the 2 mm raceway floor over each well and do not reach the
  screw — they have been vestigial since that floor was added.
- **Solid member with a raceway cut into it**, not a hollow shell: 12mm wide, with a 7x8.5mm
  wire channel (rounded R2 inboard corners) sealed at both rail ends so debris thrown up by the
  tracks can't scoop into the wire run. Six retention tabs alternate top/bottom inside the
  channel. Two foot wells (front smaller, rear roomier) sit clear for future small components
  (fuse, buck converter) — nothing committed yet.
- **Rail top is Z 54.5** (48 in v1). Raceway Z 38.5-45.5, 9 mm top slab for the 7.05 long
  heat-set inserts. Three M3 insert bores per rail at Y 16/68/120, unchanged in plan so the
  printed upper deck still bolts straight on.
- Rebuild: `scripts/apply_first_build_fixes.py` (checks with solids and refuses to save on any
  failure). Several rail sketches (raceway, wells, tabs, slots) are not spreadsheet-driven; that
  script rewrites them from its constants, so edit it rather than the spreadsheet alone.

Full build history and every dimension: git log on `docs/upper-structure.md` (retired, but its
history remains in git) plus the script files in `scripts/`.

## Upper deck

`UpperDeck`, 4mm PETG plate sitting on the rail tops (Z 54.5-58.5 since the v2 rails; Z 48-52
before — the part itself is unchanged, and the printed one is reused). **Tapered width**: 85mm through
the board zone (Y 0-89), stepping back to 79mm — flush with the aluminum below — exactly where
the driver mounts start (Y 89). The taper exists purely to fix a tight board layout up front
without touching driver clearance at the rear; see Known Issues for a correction to the original
reasoning.

**Board layout** (adhesive-mounted except the S3, which is screwed to 4 bosses with M3 inserts):

| Item | X range | Y range |
| --- | --- | --- |
| S3 + expander | -0.5 .. 41.5 | 15 .. 89 |
| Breadboard + C6 + BNO | 44.0 .. 79.5 | 15 .. 61.3 |

2.5mm margin on every boundary (left edge, gap between boards, right edge).

**Mast bearing collar**: Ø20.4 bore with a Ø28 collar standing **above** the plate to Z 68.5 (Z 62 on v1 rails), giving
a 14-long upper bearing (vs. the bare 4 of deck). Moved above the deck on 2026-09-17: it makes the
deck single-sided so it prints flat with **zero support** (it previously needed 10893 mm2 of it,
all landing on the face that mates with the rail tops), and it lengthens the bearing couple against
the lower socket from 30 to 42, so it resists mast wobble better rather than worse.

## Mast

`MastBase` (the aluminum-anchored socket) and `MastTube` (the removable Ø20/Ø12 tube).

- **Role**: fixed, non-rotating backbone. It is an interface, not a housing for one sensor — the
  head above it swaps. Any pan/tilt motion lives in the head, not the mast.
- **Anchored at the aluminum deck's existing Ø14 hole** (the primary structural bearing, resisting
  moment as a couple against the upper deck's bearing, now 6.5 further above with the v2 rails).
  Two M2 screws into the two *usable* rear pilot holes supply the anti-rotation a round spigot
  alone cannot — the other two rear pilots are buried under the rail feet and are not usable.
- **Mast base M2 holes are 43.4 apart, at X 17.8 / 61.2, Y 111 (2026-09-27).** In the first
  build, half of each real pilot showed through the printed holes at 19 / 60. Jim confirmed the
  real pilots are **3/4 of a pilot diameter (1.2) further outboard**, and that the Y 128 pair is
  aligned with them. So the deck model's rear pilots were moved (`rear_pilot_outside_bias`
  0.5 → 1.7, all four), and `mast_screw_spacing` / `mast_screw_y` are now **formulas on the deck
  pilot parameters**, so the base cannot drift from the deck again. `PowerShield` follows too.
  (Along the way, a 42.5 version and a revert to 41 were committed and withdrawn. Both came
  from ambiguous readings of the direction and size of the shift.)
- Spigot drops to **Z -3.5** (5 until the 2026-09-26 reprint Jim asked for), with a 0.8 x 45°
  mast socket entry chamfer (`mast_entry_chamfer`) — both folded into the master on 2026-09-27;
  they had lived only in `cad/mast-base/reprint-20260926/`. The lower anchor only carries shear,
  not the head's full bending moment.
- **Bug found and fixed (2026-09-17)**: the wire-exit window (`MastWindowCut`) had
  `Reversed=False`/`Length=20`, cutting *away* from the tube — its volume was bit-for-bit identical
  to the uncut tube, meaning the rear wall had been solid the entire time despite looking finished.
  Found by the mast-head workstream's audit (`docs/mast-head/cad-audit-20260916.txt`), which
  isolated the fix without touching the saved master; applied here as `Reversed=True`,
  `Length=12` — volume dropped by exactly 523.4 mm3, matching their trial. Verified open at the
  rear wall, still solid at the front.
- **Mast top is Z 130** (v2, 2026-09-27; was 120): the 6.5 rail lift plus a little more, per
  Jim. Still provisional — see Mast Head section. The imported head candidate and the index
  flat follow `mast_top_z` through their placements (`head_drawn_mast_top` = 120 is the height
  they were drawn at), so they sit on the new top.
- **Anti-rotation index flat added (2026-09-17)**, at the user's direction ("modifications to the
  mast are allowed if they improve the design"). The neck candidate needs a key to resist twist —
  a round tube alone can't provide it. Rather than re-deriving the flat's geometry, the exact
  cutting tool was extracted from the head candidate's own feature history
  (`MastTubePinCut.Shape.cut(HeadMastIndexFlat.Shape)`, giving a precise 75.18 mm3 solid at
  X 35.5-43.5, Y 122.1-123, Z 105-120) and applied to the real `MastTube` as a
  `PartDesign::Boolean` cut. Volume matches the candidate's own mast exactly (22249.4686),
  and the imported `Neck_Main` and `GH44_Fixed_Head_Adapter` both went from a small clash
  (45.4 mm3 — see Known Issues) to zero.

## Driver mounts (motor driver + heatsink)

One BTS7960 driver per side, canted 60 deg with the heatsink facing outward/upward into clean air
and the component/terminal side facing inboard toward the vehicle interior, mounted rear of the
S3 (Y 90-139.5).

- **Open frame, not a backing plate**: after an early solid-plate version blocked airflow around
  the heatsink, the mount was rebuilt so the PCB bolts flat to a frame lying in its own plane —
  the heatsink passes clean through the frame's open middle into open air, on a 39.5 x 39.5 hole
  pattern.

  **Corrected 2026-09-19.** This paragraph used to say the arms sat on "the 8.75mm of bare board
  the 32mm heatsink leaves at each end", i.e. at the two ends of the 49.5 axis. **The geometry does
  not do that**, and the error sent a measurement request after the wrong dimension. Probing the
  built solid at its board contact face, in board coordinates:

  | | |
  | --- | --- |
  | Frame touches the board | two strips at **B 0.76..9.26 and B 41.76..50.26** — along the **51** axis |
  | Each strip | 8.5 wide, running the full length of the 49.5 axis (A -2 to 52, slightly proud at both ends) |
  | Opening between them | **32.5 wide in B**, full length — this is what the heatsink passes through |

  So the model assumes the heatsink is **32 across the 51 axis**, leaving (51 - 32) / 2 = **9.5 of
  bare board along each long edge**, and the 8.5 arms sit in those strips. The relevant clearance
  is **0.24 mm per side**, not a comfortable margin.

  `drv_hs_len = 32` is therefore the heatsink's width across the 51 direction, despite its name.

- **Arm relief for the header tails (2026-09-20).** The board's rear face is the only mountable
  one, and it carries header undersides 1.5-1.7 proud. Rather than a channel for the terminal row
  and a pocket for the GPIO block, the arms are relieved **2.0 deep across a single span,
  A 8.0..39.5**, leaving a **bearing pad at each screw** (contact survives at A -4..8.02 and
  39.52..54). Four discrete pads is how a PCB normally mounts, and it does not depend on which
  strip is the terminal side — which has never been established — nor on the unresolved
  centre-vs-edge reading of the GPIO block's position. 1071 mm3 removed, verified against the
  solid.

  **Trap for whoever edits `DrvHoleSketch` next: its local axes are not the board axes.** Sketch x
  is the 51 axis and sketch y is the 49.5 axis *reversed* (`B = sketch_x + 0.75`,
  `A = 50.25 - sketch_y`). Because the hole pattern is square, a transposed rectangle looks
  entirely plausible — the first attempt at this relief removed 23 mm3 instead of 1071 and the
  geometry still checked out as valid.
- **Joint reinforced twice**: first by matching the root members' thickness to the frame (they'd
  been left at half the section while the frame was thickened, silently moving the weak point
  inboard); then by enlarging the transition into a real gusset that engulfs the frame's inboard
  edge with margin, rather than meeting it at a thin corner-to-corner touch.
- Deck-fixing holes were initially missing entirely (the foot rested on its two bosses with
  nothing to bolt through) — added, and verified to land exactly on the boss/insert centres.
- Fin envelope reaches to about X -47 to -58 depending on height, staying inside the ~50mm track
  line with a few mm of margin; independently verified via `DriverBoardLeft`/`DriverBoardRight`
  (below) rather than by hand.

## Power board shield

`PowerShield` exists as a built part (floor + end lip, open along the long sides where the rails
are, legs sharing the mast base's existing M2 screws), but its concept was corrected mid-session:
it's a **clip-on backing shield covering the board's solder side**, not a screw-down tray. Its
final position is explicitly an open wiring question, tentatively rear, not yet resolved.

**Power board tray v4 + snap-in base (2026-10-01).** Supersedes v3 for printing
(`cad/power-board/v4-tray/`, `scripts/build_power_tray_v4.py`; not printed). The tray no longer bolts to the deck: two bases
are bolted once at the rear slits with **M3** bolts, and the tray's 1 mm smaller rear pegs drop into their sockets with a
click bump, so a battery change is lift-out, swap, drop-in. The floor is 1.8 lower (Jim: installed cells are
negligibly over the holder rim, so the master `BatteryCells` value of 4 is stale) and the board posts are 4.5 tall, so the
board sits 4.3 lower than v3 and the connector room under the upper deck is 22.1. A later pass the same day lightened the arms (scooped to 9 deep between the collar contacts) and strengthened the
base (thicker walls, filleted tab, a rib behind the bump groove, peg root fillet). The INA226 cassette in front of the
battery box is a separate removable shelf, `cad/power-board/ina-cassette-v1/` (2026-10-01, not printed): it hangs on the tray
front wall, board front-to-back with the header pins out the front. Its two M2 hole positions are inferred, not calipered.

**Power board tray v5 and INA226 cassette v2 (2026-10-03).** Jim printed v4 and found the click mounts far too tight (the thin front wall
tore off before a peg moved), the walls too weak, and the cassette pressure fit unworkable. v5 (`cad/power-board/v5-tray/`) has plain wells
with no bumps or grooves, 2.4 front, rear and floor, root gussets, corner blocks and post webs; the side walls stay 1.0 (0.1 inside the
rails). The cassette v2 (`cad/power-board/ina-cassette-v2/`) is a drop-on hook: plate, lip over the wall top and two toes down the inside,
every gap 0.3. v4 and cassette v1 remain as the record of what was printed. Neither v5 nor v2 is printed. The v5 arms are exactly the v3
arm again (the v4 V-scoop is gone; Jim did not want the arm redesigned), except that, at Jim's request, the long section beside
the mast is 14 tall instead of 20 (top Z 34), joined to the taper by a 45 degree ramp.

## Antenna pylon

`AntennaPost` carries a **standard nut-type SMA bulkhead** connector. Rebuilt twice; the current
design is driven by the real connector's dimensions rather than a guess at them.

**What a standard SMA bulkhead actually needs** (1/4-36 UNS-2A thread, the common nut-mount type):

| | |
| --- | --- |
| Panel cutout | 6.4 - 6.5 (thread major dia is 6.35) |
| Nut | 8.0 across flats, ~9.2 across corners |
| **Maximum panel thickness** | **about 2.2** -- there is only so much thread behind the shoulder |
| Thread to leave proud for the nut | 2 - 3 |
| Tail behind the panel | rigid body + crimp, then RG316 (2.5 OD, 15 min bend radius) or RG178 (1.8 OD, 9) |

That last row is what drove the redesign. The connector's tail is long and stiff, and the earlier
design tried to hide it inside a 16-deep cavity and then bend the cable inside the part -- it did
not fit, and there was nowhere for the nut to clamp because the "SMA hole" was a blind pocket.

**Current design: a vertical bulkhead plate, tail pointing where the cable already wants to go.**

- **Plate** X 56..80, Z 58..86, **6 thick** (Y 2..8) -- stiff enough to take knocks on the antenna.
- **Counterbore Ø11 x 4 deep from the rear face**, leaving a **2.0 clamping web** at the hole. The
  connector is inserted from behind, its shoulder seats in the counterbore, and the 2.0 web is
  inside the 2.2 limit a standard bulkhead can clamp. The plate is thick *and* the clamped section
  is thin, which a plain 6 plate could not do.
- **Ø6.5 through-hole** at X 68, Z 78. The nut goes on the **front face**, which is a clear flat
  with full 360 degree spanner swing -- no counterbore to reach into.
- **Tail runs straight out the back.** The axis at Z 78 clears the breadboard (top Z 73.5), so the
  rigid tail and its cable head rearward over the breadboard straight to the C6. **No bend is
  forced on the cable by the mount at all** -- the earlier version's whole problem.
- **Base flange** X 56..80, Y 2..13, Z 58..62, on two deck bosses relocated to (60, 10) and
  (76, 10) so their screws sit clear of the plate and are reachable from directly above.
- **Two gussets** brace the plate back onto the flange, so the antenna load is not carried by the
  plate/flange corner alone.

Antenna points forward and hinges up, as these antennas are designed to. Verified by probe: open
through on the hole axis, 2.0 of solid web either side of it inside the counterbore, full 6 plate
outside it.

## Known issues, fixed and open

**Open, and it has already cost a print (2026-09-19): the S3 hole pattern in the printed upper
deck is wrong.** The four `S3BossSketch` bosses sit at 30.9 x 55.0 center-to-center, centered on
the board envelope at (20.5, 52) — deck coords (5.05/35.95, 24.5/79.5). The printed part was
verified to match the model exactly (boss tops clustered from
`Gladiator_P4_UpperDeck_print-flat-bosses-up.stl`: 8.05/38.95 x 24.5/79.5 in STL coords, which is
the model shifted by the export's +3 in X). So this is not an export bug or a print bug — the
**input measurement was wrong**, and the model faithfully built the wrong number.

The failure mode is worth naming, because it is the same one `measurements/` was supposed to
prevent: an *ambiguous* caliper reading (35.5 / 59.6, reported "from nearest edges of the
circles") was resolved by **inference** rather than by going back to the board, the inference was
written into the table as "Confirmed 2026-09-15", and from then on nothing downstream could tell
it apart from a real measurement. See `measurements/components.md` for the three candidate
readings and the two-reading (inside span + outside span) protocol that makes the convention
self-evident.

**Rule this earns:** a derived or inferred dimension never gets marked confirmed. It stays flagged
until a measurement confirms it, and no irreversible step — certainly not a print — consumes it
while it is flagged.

Recovery options for the deck as printed are in the Build state section.

**Fixed this session:**
- Mast wire window cut the wrong way (see Mast section).
- Antenna mount was a blind hole with no clamping mechanism (see Antenna section).
- Driver mount joint was under-sized relative to the member it fed into (see Driver Mounts).
- Deck fixing holes were missing from the driver mount foot entirely.
- A spreadsheet row mix-up (`bb_x0` vs `bb_y0`, rows 80/81) briefly moved the breadboard's Y
  position by mistake instead of its X position — caught immediately by the same clash-check habit
  that caught everything else here, fixed before it reached a commit others would build on.

**Corrected finding, not a fix but worth knowing:** an earlier hand-trigonometry check claimed
widening the upper deck would interfere with the driver board's component envelope (margin -2.7mm
at 85mm width), and that dropping the driver height for mast FOV had only ~2.5mm of room before
hitting the deck edge. Building the actual `DriverBoardLeft` reference solid and querying it
directly (rather than trusting the hand calculation, which had inconsistently used 49.5mm instead
of 51mm for the board's canted dimension across different scripts) shows **no such conflict
exists** — real clearance to even an 85mm-wide deck is 12-14mm, and there's 32mm of vertical
clearance above the tracks. The tapered deck is still a valid, working fix for the board-layout
tightness that motivated it, but the driver-drop question for mast FOV is more open than
previously stated — worth revisiting if head FOV still matters, this time gated by the frame's
structural rebuild effort rather than a clearance ceiling.

**Standing caution:** the mast's rear-pilot-hole audit found only 2 of 4 rear M2 pilots are
actually usable (the other 2 are buried under the rail feet) — corrected a note that had claimed
all 4 were free for board mounts.

**Also fixed this session (2026-09-17):** the head candidate's `Neck_Main` and
`GH44_Fixed_Head_Adapter` clashed with the real `MastTube` by 45.4 mm3 — not a modeling error in
either file, but a genuine missing feature: the neck's clamp geometry assumes the mast's
anti-rotation index flat exists, which it didn't yet on the real mast. Resolved by adding that
flat to the real mast (see Mast section) rather than papering over the clash.

## Build state — what exists as physical hardware

**Plate A was printed on 2026-09-18 (PLA): both side rails and the upper deck.** Those three parts
are now real objects, not just geometry, and the same rule that applies to the aluminium deck now
applies to them: **do not solve a new problem by adding features to them.** Anything that needs to
attach must either work with them as printed, or wait for a deliberate v2 reprint.

Printed so far: side rails x2, upper deck. Everything else is still only geometry.

### First full build — findings and v2 parts (2026-09-27)

Jim assembled rails, upper deck and mast on the real chassis. Upper deck: very good, kept as is.
Changes, all in `cad/build2-20260927/` (STLs, `validation.json`, comparison drawing) and applied
to the master by `scripts/apply_first_build_fixes.py`:

| Finding | Change | Reprint |
| --- | --- | --- |
| Rails too short; battery clearance 8.5 | Soffit 36.5, rail top 54.5: **15.0 clear** | Both rails |
| Right front screw would not quite pass the slit | Front foot slot moved 3.5 back; head relief | (same rails) — fit coupon first |
| Mast must rise with the deck, "a tad more" | Mast top 120 → **130** (+10) | Mast tube |
| Mast base holes miss the pilots: half a pilot visible, the real ones 1.2 further out | Deck rear pilots corrected (both pairs); M2 holes 41 → **43.4**, tied by formula | Mast base |
| Rear pilots at Y 128 now sit under the rail rear feet (7.9 mm3 M2 overlap), so the tray's pilot tabs can't reach | **Power board tray v3** (`cad/power-board/v3-cradle/`): pilot tabs dropped, rear slit bolts only; arms 20 deep (were ~4-6); floor 1.6; board ends open 2.9 for wires; lifted 4.3 to clear the cells (v2 sat 3.3 into them) | Tray |
| Installed cells stand 4 above the holder | `BatteryCells` reference envelope, Z 21.5-25.5: real clearance under the v2 soffit is **11.0**, not 15.0 | — (superseded 2026-10-01: cells are negligibly over the rim, envelope now Z 21.5-22.0) |

The master's mast base also carries the 2026-09-26 reprint's 3.5 spigot and 0.8 socket chamfer. The
driver mounts, antenna post and head candidate sit on the deck or the mast top and moved with
them — no new or grown overlaps across 48 solids. Jim chose to keep the v2 height (11.0 to the
installed cells), and to skip the front-foot fit coupon and go straight to the rails.

### The printed deck's S3 bosses do not fit the board (2026-09-19)

The bosses are at 30.9 x 55.0; the board's real pattern is not that (see Known Issues). Because
the true pattern is almost certainly *larger* in both directions, re-drilling the existing Ø9
bosses is unlikely to work: moving a Ø4.6 bore 2.3 in X and 2.3 in Y is a 3.25 diagonal shift on a
4.5-radius boss, which puts the bore edge 5.55 out — it breaks out of the boss. Options, in the
order they should be considered:

1. **Adapter plate** — a thin printed plate that bolts down to the four existing bosses at
   30.9 x 55.0 (M3 into the heat-set inserts already designed for them) and carries its own four
   standoffs at the board's true pattern. Small, fast, and it obeys the rule about not modifying
   printed parts. Costs a few mm of stack height, which the deck has.
2. **Two-screw mount plus a bonded or clamped third point**, if the true pattern happens to share
   one usable axis with the printed one.
3. **v2 deck reprint** — only worth it bundled with the power-board mount problem the v2 note
   below already describes, not on its own.

Do not pick one until the re-measure lands: the offset magnitude decides between them.

### Consequence: the power distribution board needs a different answer

The board (60 x 40 x 16) does not fit anywhere on the lower deck, and the standing candidate was to
hang it from the **upper deck's underside**. That underside is completely bare — verified, 0.000
mm3 below the plate plane — which is exactly why the deck prints support-free, and it is now
printed that way.

So for this build the mount has to attach without new deck features. What the printed deck actually
offers from below:

- four Ø12 holes through the plate at its corners, near (3, 6), (76, 6), (6, 134), (73, 134)
- six Ø3.6 deck-to-rail screws at (12/67, 16/68/120), which land on the rail tops — no space
  beneath them
- a large bare flat region between the rails, X roughly 18..61, which is the bed face and therefore
  the flattest surface on the part

A bracket using the corner Ø12 holes, a clamp onto the rails' inboard faces, or bonding to that
flat bed face are all workable without touching either printed part. Not yet designed.

### For a v2 deck, eventually

If the deck is ever reprinted, design the power board mount into it properly — but note the
trade-off it forces: underside features cost the deck its zero-support print **and** put support
scarring on the face that mates with the rail tops. A v2 should solve the mount and that print
orientation together, not bolt the mount on and accept the scarring.

## Open items

1. **S3 mounting pattern** — blocking. The printed deck's bosses are wrong; the board's true
   center-to-center pattern needs the two-reading re-measure in `measurements/components.md`
   before either an adapter plate or a v2 deck can be designed.
2. **Power board mounting** — the board has no mount at all, and the upper deck it was going to
   hang from is now printed with a bare underside. Needs a bracket that works with the printed
   parts; see the Build state section. The shield's own position also still needs the wiring plan.
3. **Mast top height (Z 130 since 2026-09-27)** — raised with the v2 rails. The S3 board also
   rose 6.5, so the ToF-over-the-S3-corner sightline question is not settled by this; the
   mast-head workstream should re-run it against the new heights.
4. **Driver height / mast FOV** — see the corrected finding above; real room exists if this
   becomes a priority again.
5. **Antenna whip/pigtail exact envelope** — not yet measured against the pylon's cable exit.
6. **INA226 placement** — deliberately not designed yet, per earlier instruction.
7. Anything the mast-head workstream's own "Exact measurements needed" and "Next design gate"
   sections still list (servo/sensor dimensions, harness design, interface freeze) — see below.

## Mast head (modular sensor head)

**Status 2026-10-01.** The body is printed. **Head v0.4 stage 1 (the pan stack) is built** in
`cad/head/v04-pan-stack/`, from the measured numbers and the round-2 coupon results.
- **v0.3 (belt pan drive) is on hold.** Its 40T-drives-60T ratio turns the head *less* than the
  servo, and its belt centre distance is 39.745 where it should be 39.486.
- **v0.4** puts **60T on the servo and 40T on the head** (1.5x, 133 degrees of servo for 200 of
  head), on a sliding servo carriage (37.0 to 40.4), with a horn plate that takes the real SG90
  cross horn. It is re-based on the printed v2 mast (top Z 130) and checked against the current
  master: 0 collisions in about 78,000 pose tests, 0 ToF intersections, cable bore clear.
- **Stage 2 (the tilt side) was done the same day.** The yoke, GH44 receiver, dual carrier and
  display frame are regenerated with calibrated holes, the same horn-plate drive, an M3 pivot bolt,
  and the real ST7789 window. Not designed yet: sensor mounting on the carrier, and the harness.
- Print plate 1 (pedestal, retainer, clamp cap) waits on no coupon. The rotor, carriage, neck and
  drive pulley wait on H3b (post), H4b (M2 pilot) and H6 (horn pocket and arm thickness).
- The horn arm thickness and the boss-to-horn gap are **assumed** and set the belt alignment. See
  `cad/head/v04-pan-stack/README.md`.
- The v0.1 summary below is history.

**A copy of the v0.1 review candidate was brought into the master on 2026-09-17**, at the user's
request, so the full assembly can be opened and reviewed in one file. This is an import of a
snapshot, not a live link — the head workstream's own files (below) remain authoritative and will
continue to change independently; re-import when their candidate is next revised.

**What was imported, and what wasn't:**
- The 30 actual head parts (neck, pan/tilt hardware, all 5 carrier variants, the fixed-adapter
  alternative, hardware/sensor reference envelopes) were copied into a new `HeadCandidate_v01`
  group in the master, preserving their own sub-grouping (`FixedNeck`, `PanAssembly`,
  `TiltAssembly`, `CarrierVariants`, `HardwareReferences`, `CableReferences`, `FixedOption`,
  `FitCoupons`).
- Their file also contains a **complete duplicate copy of the whole robot** (their own
  `ChassisDeck`, `MastTube`, etc., captured from `source-master.FCStd` for their internal
  visualization) — this was deliberately **not** imported, since the master already has the
  authoritative version of all of that.
- The one exception: their candidate's mast anti-rotation index flat *was* brought over, as an
  actual modification to the real `MastTube` — see the Mast section above.

**Verified after import:** all 30 imported parts have valid single solids; re-checked every one of
them against all 15 of the master's own robot solids (chassis, rails, mast, deck, driver mounts,
antenna, driver board envelopes) — zero clashes anywhere, including the two mutually-exclusive
neck-vs-fixed-adapter options (both check clean against the mast; they are alternatives to each
other, not meant to be installed together).

**This import does not mean the interface is frozen.** Everything about "provisional," "not yet
measured," "assumed," and "no purchase made" in the source documents below still applies exactly
as written there — importing the geometry doesn't resolve any of those open items, it just makes
the current state reviewable in one file alongside the rest of the robot.

Full detail, reasoning, and the evidence trail live in the mast-head workstream's own files,
which this document does not duplicate and which continue to evolve independently:

- `docs/MAST_HEAD_DIRECTION.md` — design direction, architecture rationale, hardware findings
- `docs/mast-head/cad-audit-20260916.txt` — raw audit of the saved master (the source that found
  the mast window bug)
- `measurements/mast-head.md` — hardware measurement log (servos, sensors)
- `cad/head/v01/README.md` and `validation.json` — the v0.1 review candidate: file inventory, the
  GH44-v0.1 carrier interface spec, validation results

**Summary, as of the v0.1 review candidate (2026-09-16), for coordination purposes only —
treat the files above as authoritative for anything more specific:**

- Architecture: fixed mast -> removable neck (pan/tilt motion unit) -> standardized GH44 carrier
  interface -> interchangeable sensor carriers (blank, SEN0628 ToF, SEN0610 radar, ESP32-CAM,
  combined dual). A fixed (non-moving) adapter reuses the same carrier interface for early sensor
  work.
- GH44-v0.1 interface (proposed, not frozen): 44x44mm mating plate, four M3 holes on a 32x32mm
  square, a keyed 26x22mm locating register with one clipped corner (prevents 180 deg misalignment),
  18x10mm cable window through both halves.
- Motion: limited-angle pan (+-40 deg) and tilt (+-25 deg) via SG90 servos — explicitly not
  continuous rotation, to keep a simple wire path through the mast bore. Pan load goes through a
  dedicated bearing pair, not the servo output shaft alone.
- The v0.1 candidate lives entirely in `cad/head/v01/`, built from `source-master.FCStd` (a
  captured copy, hash-verified against this repo's commit `5489d23`) — **the actual
  `Gladiator_Master.FCStd` was never touched**, confirmed by `validation.json`'s
  `master_unchanged` / `master_unchanged_after_save` checks.
- Extensive caveats throughout their documents on what is measured vs. assumed vs. provisional
  (servo shaft centering, sensor hole coordinates, bearing/PETG fit, RF transparency of a radar
  cover, etc.) — this summary does not carry that nuance forward; read their documents before
  acting on any specific number.
- **Coordination note for whoever next edits the master**: adding the head's interface features
  (e.g. a mast anti-rotation key/flat, proposed over Z 105-120 in the candidate) to the real mast
  should happen as a deliberate, coordinated change once the interface is closer to frozen, not
  silently — both workstreams edit the same underlying geometry conceptually even though the files
  are currently separate.

## Sources

- `measurements/chassis.md` — aluminum deck measurements, with explicit uncertainty notes
- `measurements/components.md` — board/battery/track measurements, caliper-verified 2026-09-15/16
- `docs/upper-structure.md` — retired; full build history preserved in git log
- Mast-head workstream files listed above
- `scripts/` — every build script used to construct the master file, kept for reproducibility and
  as the actual source of truth for exact parametric relationships (the CAD file's `Parameters`
  spreadsheet is the runtime source of truth; the scripts are how it got there)

## Printer calibration

`measurements/printer-calibration.md` **is the authoritative record of how the printer actually
prints**, and anything in the CAD that depends on a real-world fit should be checked against it
before being committed to. Build material is PLA until a deliberate move to PETG.

Round 1 (fit coupon v2, 2026-09-18, PLA): **flat walls print slightly over** (the 88.00 plate
measured 88.25) while **every curved feature prints about 0.25 under, convex and concave alike**.
The rail slot is the proof - 3.4 wide with flat walls takes an M3 comfortably, while a 3.4 round
hole on the same plate will not pass one. That is toolpath under-shoot on curves, not flow and not
a scale error.

Every fit-critical round dimension was moved one step up to compensate, and the bores that start on
the build plate got a 0.35 entrance relief so elephant's foot cannot pinch them. These are
**calibration-compensated values for this printer as it prints today**; if the curve error is tuned
out they should all be revisited together. The design intent behind each one is tabulated in the
calibration log.

**Those radii are now actually parametric.** They had been plain numbers in the sketches with no
expression attached, so changing `rail_insert_dia` did nothing at all, despite this document saying
it fed all 16 bores. 31 constraints across 13 features are now bound to the spreadsheet.

## Print readiness (audited 2026-09-17)

Audited every printed part for overhangs, minimum section, hole/insert geometry and bed fit by
querying the solids directly. Method note: the first pass was wrong twice — `normalAt()` is
already orientation-aware and was being flipped a second time, and the face resting **on the build
plate** was being counted as needing support. Numbers below are from the corrected pass, which
carries a built-in self-check against an independently measured figure.

### Defects found and fixed

| | Was | Now |
| --- | --- | --- |
| **Antenna insert bore floor.** Antenna bosses were built 4 tall while every other boss is 6, but all use the same 7.5 insert bore — so the bore broke through to 0.5 mm above the deck's underside. A heat-set insert would have pushed straight through. | 0.50 mm | **2.50 mm** (bosses now 6, matching the rest; pylon reseated +2 to suit) |
| **Rail foot wells opened into the raceway.** The well ceiling and the raceway floor were both at Z 32, so over each foot the two cavities merged with no wall between them, leaving a 0.2 mm feather edge where the raceway floor ran out. | 0.20 mm | **2.00 mm continuous floor** (well ceiling lowered to Z 30) |
| **Driver PCB screw holes too close to the arm edge.** 2.7 mm self-tap pilots in 6 mm arms left 1.70 mm walls — an M3 self-tapper would likely split them. | 1.70 mm | **2.20 mm** (arms widened 6 -> 8.5, out to the heatsink's edge at u 8.75/40.75) |

The driver frame is now a single U-shaped profile rather than three overlapping rectangles. The
overlapping version produced an invalid face that silently killed the feature chain — caught
because the verification looked for the screw holes and found none.

### Recommended print orientation

Support area excludes the face resting on the bed. "Height" is the build height in that orientation.

**Re-measured 2026-09-17 (evening)** against the mesh facets rather than sampled surface
normals, with the method first checked against two shapes of known overhang area (a plain box:
0 mm2; a T-beam: exactly 1200 mm2). The earlier figures in this table were low across the board
and the mast base recommendation was wrong; both are corrected here.

| Part | Lay it | Support | Height |
| --- | --- | ---: | ---: |
| Side rails x2 | **on the outboard face** (-X for the left rail, +X for the right) | 320 mm2 | 12 |
| Mast tube | **vertical**, as modelled | 65 mm2 | 114 |
| Antenna mount | **on its front face** (-Y down) | 21 mm2 | 11 |
| Power shield | **on its side** (-Y down) | 281 mm2 | 73 |
| Mast base | **upside down, spigot UP** (see below) | 878 mm2 | 25 |
| Driver mounts x2 | **inverted** (+Z down) | 938 mm2 | 72 |
| Upper deck | plate flat, everything up | **0 mm2** | 14 |

**The mast base correction.** This table previously said to lay it on its side, at 361 mm2. Laid
on a side face it actually needs 602 mm2, only 188 mm2 of it touches the bed, and — the real
problem — the Ø13.8 spigot and the Ø20.4 mast socket both become horizontal bores, so the two
fit-critical features on the part print out of round. Upside down costs more support (878 mm2)
but keeps both coaxial with the build direction, and puts the deck-mating face upward where
nothing scars it. The collar doubles as a pillar carrying the plate, so the support is only the
plate's outer area, landing on the non-critical top face. Bed contact is a thin Ø26/Ø20.4 ring,
so it needs a brim.

The rails are the standout: laid on the outboard face the whole arch profile is one layer outline,
so it prints essentially support-free at only 12 tall. The trade-off is that the three M3 insert
bores then lie **in** the layer plane rather than across it, and a heat-set insert expanding
sideways can wedge layers apart. There is 6 mm of material around each bore and PETG bonds well
between layers, so this should hold — but if an insert splits a rail, reprint that rail standing up
(Z up, 1245 mm2 of support) which puts the bores across the layers instead.

Everything fits a 220 x 220 bed; the largest footprint is the upper deck at 85 x 140.

### Export orientation bug, found and fixed 2026-09-17

`scripts/export_prints.py` chose the rail's print rotation by trying +90 and then -90 about Y
and taking the first that produced a 12 mm build height. **Both rotations satisfy that test**, so
it took +90 and laid the rail on its **inboard** face — raceway opening downward, 1448 mm2 of
support instead of 320 — while writing the file as `..._print-on-outboard-face.stl`. The
bounding box is identical either way, so no downstream check could have caught it.

The exporter now names the direction that must end up facing the bed, computes the rotation from
it, and **asserts the resulting bed-contact area matches the intended face** (outboard 3350 mm2
vs inboard 2089 mm2), so the wrong face fails loudly instead of producing a plausible file. Every
part is exported and checked through that one path; see `docs/print-plan.md` for the plate
grouping and the order the plates have to run in.

### Resolved: the upper deck now prints support-free

The deck used to have features on **both** faces -- bosses up, mast bearing collar down -- so no
flat orientation was support-free, and the only sane one left the entire 10893 mm2 plate underside
floating on support. That underside is the face that mates with the rail tops, so the scarring
landed exactly where flatness matters.

**Fixed 2026-09-17 by moving the collar above the deck** (see Upper deck). The deck is now
single-sided: plate flat on the bed, bosses and collar both growing upward, **0 mm2 of support**.
The mating face is now the bed face, which is the flattest surface a printer produces.

### Other observations, not changed

- **Mast tube layer direction.** Printed vertically (the only sane orientation for a Ø20/Ø12 tube),
  the layer lines run perpendicular to the bending stress a sensor head applies — the weakest
  possible arrangement, and a mast that fails will fail at a layer line. **Decided 2026-09-30
  (Jim): the printed v2 tube (top Z 130) is the mast; buying a tube is dropped.** The weakness is
  accepted. Keep the head light and don't lift the robot by it.
- **Index flat engagement.** The head candidate's anti-rotation flat is only **0.9 mm deep** and,
  being a tangential cut on a cylinder, feathers to about 0.07 mm at its edges. With a stated 0.2 mm
  key clearance and normal PETG tolerance, the effective engagement could approach zero. Flagged for
  the mast-head workstream — a bounded keyway with real side walls would engage more reliably than
  a tangent flat.
- **Antenna cavity is an open channel, not a closed cavity.** The geometry is a tunnel running
  clean through the pylon front-to-back; the Ø8 "cable exit through the rear wall" documented
  earlier cuts a face that is already open. This is good for assembly — the SMA connector slides in
  from either end rather than having to be fed through an 8 mm hole into a sealed void — but the
  earlier description in this document was wrong and has been corrected here.
- The SMA clamping panel is 2 mm of PETG. That is within the normal panel range for an SMA
  bulkhead, but use a washer under the nut so it does not dig in.
