> Historical measurement record, including dated corrections.
> See the [current CAD index](../../hardware/README.md).

# Gladiator component measurements

Current chassis frame (2026-10-09): **mast is FRONT (+Y)**; low X LEFT,
high X RIGHT. See [orientation](ORIENTATION.md). Dated directional
chassis labels below preserve the original mast-rear measurement provenance.

Units: mm. Caliper-measured 2026-09-15.

**These supersede everything implied by the printed coupons in `cad/TestPrint01/` and
`reference/test-batch-01/`.** Those coupons were confirmed inaccurate and must not be used as a
dimensional source for anything.

## S3 + expander deck (logic, upper deck)

| Feature | Value | Notes |
| --- | ---: | --- |
| Board length | 74 | Re-check alongside the width |
| Board width | 42 | **Wrong. Disproved 2026-09-19** — the holes alone span 44.7-48 across |
| Height, S3 mounted | 17.5 | |
| Height, tallest dupont pins | 28.3 | Governs upper-deck headroom |
| Mounting holes | 4.6 dia | **Contradicted by the 2026-09-19 readings — see below** |

### Two-reading measurement, 2026-09-19

| Direction | Inside span `I` | Outside span `O` | c-t-c = (I+O)/2 | dia = (O-I)/2 |
| --- | ---: | ---: | ---: | ---: |
| Across width | 35.5 | 48 | **41.75** | 6.25 |
| Along length | 59.5 | 72 | **65.75** | 6.25 |

**Settled by these readings:**

- The original 2026-09-15 figures (35.5 / 59.6) were **inside-edge-to-inside-edge**, exactly as
  they were first described. The reinterpretation to outer-edge-to-outer-edge was backwards, and
  the 30.9 x 55.0 pattern it produced is smaller than the truth by about 10 in *both* directions.
  The printed deck's bosses cannot be rescued by re-drilling.
- Both directions independently derive the same hole diameter, 6.25. A consistent derived diameter
  across two independent measurements is the protocol working as intended.
- **The recorded board width of 42 is wrong.** Whichever hole diameter is right, the four holes
  span 44.7 (at 4.6 dia) to 48.0 (at 6.25 dia) across their outer edges, and the board has to be at
  least that wide.

**Board outline measured 2026-09-20: 48 wide x 74 tall.** Jim, with the board in hand — and a
correction to how this has been described: **there is no bare S3 board in this project, it is just
the expander.** The recorded 42 width was wrong; 74 was right. Holes are large and in the corners,
and the two boards *do* just fit side by side on the deck.

### The contradiction, re-read against a real 48 x 74

| Direction | `I` | `O` | dia = (O-I)/2 | c-t-c | Board material outside each hole |
| --- | ---: | ---: | ---: | ---: | ---: |
| Across width | 35.5 | 48.0 | 6.25 | 41.75 | **0.00** |
| Along length | 59.5 | 72.0 | 6.25 | 65.75 | 1.00 |

**The 6.25 reading requires zero material between the hole and the board edge across the width.
That is impossible on a real PCB** — a hole cannot be tangent to the edge. So the `O = 48.0` across
the width is almost certainly **the board width itself**, recorded as an outer-hole span by
mistake. That the two directions then "agree" on 6.25 is an artefact of that error, not the
protocol's cross-check passing.

Against the directly measured 4.6:

| Direction | c-t-c | Outer span | Material outside each hole |
| --- | ---: | ---: | ---: |
| Across width | 40.10 | 44.70 | 1.65 |
| Along length | 64.10 | 68.70 | 2.65 |

Both plausible. **4.6 is the reading that survives.**

### And only 4.6 fits the deck

Checked against the 85-wide front of the deck, with the breadboard taking 35.5 and the bosses at
Ø9:

| Pattern | Boss span | + breadboard | Verdict |
| --- | ---: | ---: | --- |
| 4.6 -> c-t-c 40.10 | 49.10 | 84.60 | **fits, 0.40 to spare** |
| midpoint 40.925 | 49.92 | 85.42 | over by 0.42 |
| 6.25 -> c-t-c 41.75 | 50.75 | 86.25 | over by 1.25 |

**This kills the midpoint dodge.** Splitting the difference was proposed on 2026-09-20 because the
board's own holes are bigger than the M3 screws, so a 0.583 offset is absorbed. That is still true
of the *screws* — but the *bosses* do not fit at 40.925. The boss width constraint was never
checked when the midpoint was proposed. It is checked now, and only 40.10 works.

Note also that the bosses are wider than the board at every candidate, standing 0.55 proud of each
edge at 4.6. That is fine, but it means the deck, not the board, is what runs out of room.

### One look settles it, and the board is in hand

**How much board material is there between a hole's edge and the nearest board edge?** About 1.65
means 4.6 and the design proceeds. About 0 means something else is going on and the whole set needs
re-taking.

## Breadboard with C6 + BNO (logic, upper deck)

| Feature | Value | Notes |
| --- | ---: | --- |
| Breadboard length | 46.3 | |
| Breadboard width | 35.5 | |
| Breadboard height | 9.3 | Board alone |
| Height, tallest dupont pins | 21.5 | Including the board |
| BNO board length | 26 | |
| BNO board width | 15 | |
| BNO overhang past breadboard edge | 9.5 | Only 5.5 of its width sits on the breadboard |

The C6 carries an external antenna — antenna position and pigtail length not yet measured, and
it needs to sit clear of the aluminum deck to radiate.

## INA226 (power, lower deck)

| Feature | Value | Notes |
| --- | ---: | --- |
| Board length | 26 | |
| Overall length incl. dupont pins | ~40 | Pins extend past the board end |
| Board width | 22 | |
| Height, tallest point | 11.8 | |
| PCB thickness | 1.6 | Jim, 2026-10-01 |
| Mounting holes nearest the screw terminals | Ø3 | Jim, 2026-10-01. Hole centre positions NOT measured |
| Other mounting holes | Ø2 | Jim, 2026-10-01. Hole centre positions NOT measured |
| Screw terminal block, pin tail to terminal top | 14 | Jim, 2026-10-01. Does not overhang the board edge; the wires protrude from it. Against the 11.8 above, this implies pin tails about 2.2 below the PCB underside (derived). **Superseded 2026-10-01:** Jim measured about 3 between the bottom of the screw terminal and the bottom of the board, read as pin tails about 3.0 below the PCB |
| Header pins protrusion | 7 | Jim, 2026-10-01. Which edge or housing face it is measured from was not stated |
| Header-corner holes (the Ø2 pair) | Ø2 | Jim, 2026-10-01: they touch the corner of the board, with about 1 mm of board left around the hole. Centres NOT measured. The cassette v1 reads this as a corner arc of r 2 around the hole, so 2.0 from the header edge and 2.0 from each side edge: INFERRED |
| Layout (from photos, not measured) | | Two 2-position screw terminals, orange and green, share one 22 edge; the 4-pin header (GND, SCL, SDA, VCC) is on the opposite edge, pins out past the board; two large pads with holes beside the shunt |

## Motor driver x2 (power, lower deck)

| Feature | Value | Notes |
| --- | ---: | --- |
| Board length | 49.5 | |
| Board width | 51 | |
| **Overall, fin tip to tallest component** | **41** | Governing stack height |
| Heatsink width | 51 | Matches board width |
| Heatsink protrusion below the PCB underside | ~28 | |
| Components above the PCB | ~13 | Back-solved from 41 overall |

No dupont pins on these boards — the tallest features are the screw terminals.

### Mounting interface — partly measured (updated 2026-09-19)

**The driver mount's geometry depends on three numbers that appear nowhere in this file.**
They were carried in the design document as statements of fact; tracing them found no
measurement behind any of them. Until they are taken off a real board, the driver mount
is not printable.

| Number the design uses | Value assumed | Status |
| --- | ---: | --- |
| Mounting hole pitch, across the board | 39.5 | **assumed.** The only mention anywhere is one design-doc sentence. |
| Mounting hole pitch, along the board | 39.5 | **assumed**, and assumed equal to the other. The board is 49.5 x 51, so a square hole pattern is a guess, not a given. |
| Heatsink extent across the **51** axis | 32 | **assumed.** Corrected 2026-09-19: this, not the dimension along 49.5, is what the mount depends on. The frame's arms sit in the (51 - 32) / 2 = **9.5** strips along the long edges, and they are 8.5 wide — **0.24 mm clearance per side.** |
| Board mounting hole diameter | not used | needed anyway, to know what screw the pattern takes |

What the mount currently cuts, so you can see what each number moves: four Ø2.7 holes at
39.5 x 39.5 (`DrvHoleSketch`), and two frame arms 8.5 wide.

**Corrected 2026-09-19 by probing the built solid**, because the description here and in the design
document was wrong about which axis the arms lie on. At the board contact face, in board
coordinates, the frame touches the board at **B 0.76..9.26 and B 41.76..50.26** — two strips along
the **51** axis, each running the full length of the 49.5 axis. The opening between them is
**32.5 wide in B**, and that is what the heatsink passes through.

So the arms do **not** sit on bare board at the two ends of the 49.5 axis. They sit in the strips
beside the heatsink along the long edges, the model assumes those strips are
(51 - 32) / 2 = 9.5 wide, and an 8.5 arm in a 9.5 strip leaves **0.24 mm per side**.

**Verified against the built solid 2026-09-19**, with the heatsink now confirmed at 32 and centred:
the frame's two arms occupy B 0.76..9.26 and B 41.76..50.26, each 8.5 wide, sitting inside the
9.5 bare strips with **0.24 mm clearance to the heatsink on one side and 0.26 on the other**, and
0.76 / 0.74 to the board edges. The arms fit — on a perfectly flat board face.

**Measured by Jim 2026-09-19/20:** board 49.5 x 51, hole pitch 39.5 both directions, hole
diameter 3.0, heatsink 32 across the 51 axis and dead centre.

These were carried as "reported, not confirmed" for a day on the strength of the S3 lesson. That
flag is **withdrawn 2026-09-20**: the set is internally consistent (a 27.2 terminal row centred
between holes 39.5 apart leaves 6.15 either side, and holes 5.0 / 5.75 in from the edges of a
49.5 x 51 board is a coherent centred pattern), and nothing in it contradicts anything else. The
S3 failure was an *inference* made on the user's behalf, not a measurement the user took — a
different thing, and it was wrong to keep treating a direct reading as suspect. The one figure that
still does not close is the GPIO block position, and the arm relief was sized to cover it either
way, so it blocks nothing. The outline agrees with the 2026-09-15/16 caliper work and the hole
diameter is genuinely new. **Both pitches are logged as reported, not confirmed** — they came back
exactly equal to values the model had already assumed, which is the shape the S3 failure had. The
two-reading check below settles the pitch and the diameter against each other and costs a minute.
Items 5 and 6 are still completely open, and they are the ones that gate the print.

### Fastening: no self-tapping screws available (Jim, 2026-09-20)

The mount's four Ø2.7 pilots were drawn for M3 **self-tapping** screws. Jim does not have any, so
that route is out. Checked against the solid, with the arm spanning B 0.76..9.26, the hole centre
at B 5.75 and the heatsink edge at B 9.50:

| Option | Hole | Wall on the heatsink side | Verdict |
| --- | ---: | ---: | --- |
| Heat-set insert | 4.6 | **1.21** | **Out.** The coupon's working boss had 2.2, and an insert expands as it seats. |
| M3 clearance, nut behind the frame | 3.4 | **1.81** | **Recommended.** A nut loads the wall in compression rather than expanding it. |
| M3 machine screw formed into the plastic | 2.7 (as built) | 2.16 | Works, but strips after a few removals. |

**The space behind every hole is clear** — a Ø12 x 10 probe centred behind each pilot finds
0.0 mm3 of material, so a nut and a driver both fit. A through-bolt needs roughly **M3 x 18-20**:
1.6 of PCB, 12 of frame, a nut and a head.

Not yet changed in CAD — it alters the hardware needed, so it is Jim's call.

### Mounting is rear-face only (Jim, 2026-09-20)

**The component/GPIO face of the board cannot be mounted to at all** — too many components. The
mount must bear on the **rear face**, in the two 9.5 strips flanking the heatsink, and the heatsink
passes through the frame opening from the underside. The frame already works this way, so this is a
confirmation of the architecture, not a change to it. The front of the board will not be held.

The rear face is not clear, though. It carries the undersides of the headers, in two distinct zones
— one per strip, since the screw terminals and the GPIO header are on opposite long edges:

| Strip | What is on it | Consequence for the arm |
| --- | --- | --- |
| **Screw-terminal side** | a straight, shallow line of tails, **5.3 from the heatsink edge** (corrected 2026-09-20; the earlier "dead centre" was wrong) | a relief channel, but see the collision below — it lands almost exactly on the mounting hole |
| **GPIO side** | a block **8.6 long by the entire 9.5 strip width** | no contact is possible across those 8.6 mm; needs a full-width pocket, with contact only before and after it |

**The terminal-side line collides with the mounting hole.** Checked on the solid, in strip
coordinates (board edge B=0, heatsink edge B=9.5):

| | |
| --- | ---: |
| arm contact face | B 0.76 .. 9.26 |
| mounting hole, Ø2.7 pilot | centre B 5.75, edges 4.40 .. 7.10 |
| pin line at 5.3 from the heatsink | **B 4.20** |
| gap from the pin line to the hole **edge** | **0.20 mm** |

A relief channel over that line therefore undercuts the screw at every plausible row width — at a
1.0 row the channel already reaches B 4.70, past the hole edge at 4.40, and it only gets worse as
the row widens. The board would be unsupported on the outer side of each screw, so tightening it
bends the PCB instead of clamping it.

**This may be moot, and that is the next thing to find out.** The collision only exists where the
pin line actually runs. If the line stops short of the screw positions along the 49.5 axis (holes
are at A 5.0 and A 44.5), the channel can simply stop short too and the screws keep full bearing.
Hence item 11.

**Also unresolved: what the 5.3 was measured to.** Centre of the pin row, or its near edge? Those
differ by half a row width, which is the whole margin here. Same class of ambiguity as the S3
spans — see [[feedback-gladiator-never-confirm-an-inference]]. Logged as reported, not resolved.

Agreed approach (Jim): **recess the arm faces to clear the tails.** The alternative he raised —
redesigning the mount to hop over the heatsink entirely — stays on the shelf unless the recesses
prove unworkable.

**Open thermal caution.** The arms already sit **0.24 mm from the heatsink**, which is contact for
practical purposes, and the current build material is PLA with a softening point near 60 C. A
BTS7960 heatsink under sustained load will pass that. This does not block the geometry, but a PLA
driver mount should be treated as a fit prototype rather than something to drive hard on. See
`printer-calibration.md` on the PLA-to-PETG move.

**Measure these seven, in this order.** Use the same two-reading method that just caught the S3
error — for each hole pair, an inside span `I` and an outside span `O`, then c-t-c = `(I+O)/2` and
dia = `(O-I)/2`, and the two directions must derive the *same* diameter or something is wrong:

| # | Measurement | Why it matters | Currently assumed |
| ---: | --- | --- | ---: |
| 1 | Hole pitch across the board — `I` and `O` | Sets the frame's hole positions | 39.5 |
| 2 | Hole pitch along the board — `I` and `O`, **measured separately** | The board is 49.5 x 51, so a square pattern is a guess | 39.5 |
| 3 | Hole diameter, measured directly | Confirms 1 and 2, and picks the screw | not recorded |
| 4 | Is the hole block centred on the 49.5 x 51 outline? If not, board edge to the nearest hole's near edge, on all four sides | The model assumes centred — the same assumption that went unstated for the S3 | centred |
| ~~5~~ | ~~Heatsink extent across the **51** direction~~ | **MEASURED 2026-09-19: 32, and dead centre.** | 32 ✓ |
| ~~6~~ | ~~Bare board along each **long edge**~~ | **Resolved by 5**: centred, so (51 - 32) / 2 = 9.5 each. *Derived, not measured directly.* | 9.5 ✓ |
| ~~8~~ | ~~Tail protrusion~~ | **MEASURED 2026-09-20: 1.5 - 1.7 tall.** Relief cut to 2.0, giving 0.3 clearance on the worst case. | 2.0 relief ✓ |
| ~~9~~ | ~~GPIO block position~~ | **MEASURED: 8.6 long, 3.75 from one hole, 23 from the other.** Those do not quite close against the 39.5 hole spacing (3.75 + 8.6 + 23 = 35.35), and the reading closes best if the 3.75 and 23 were taken from hole *edges*, putting the block at A 10.25..18.85. **Inference, not confirmed** — so the relief was sized to cover the block under either reading. | covered ✓ |
| ~~10~~ | ~~Terminal row width~~ | **MEASURED: widest pins about 2.0.** With the near edge 5.3 from the heatsink the row sits at B 2.20..4.20. | covered ✓ |
| ~~11~~ | ~~Does the terminal line reach the screws?~~ | **MEASURED: 27.2 long, centred between the holes** — so A 11.15..38.35, clearing each screw hole edge by **4.65 mm**. The collision is resolved; the screws keep full bearing. | clear ✓ |
| ~~12~~ | ~~5.3 to centre or near edge?~~ | **ANSWERED: near edge, to the heatsink.** Row therefore runs B 2.20..4.20, away from the hole. | ✓ |
| 7 | PCB underside to fin tips, and total height standing on the fins | Closes the 28 + 1.6 + 18.7 ≈ 48 vs 41 contradiction above | 28 / 41 |

Also note whether the four holes fall on bare board or inside the heatsink's footprint — the
open-frame design depends on the screws being reachable from the component side.

**None of these may be recorded as confirmed unless they were measured.** If a number has to be
derived, mark it derived and keep it out of anything that gets printed.


**Orientation:** the heatsink is on the **underside** of the board — fins project from the face
opposite the components, not from an edge.

**Mounting intent:** one driver per side, **canted diagonally with the heatsink facing outward and
the fins pointing up and away from the tracks.** The component face therefore looks down-and-inward,
putting the screw terminals toward the vehicle interior where they can be wired. Fins pointing up
and out keeps them out of track debris and in clean air — track fouling is not a concern in this
orientation.

Envelope, measured outward/upward from the board's inner edge (board 51 across the cant, fins 28
proud of the board face):

| Cant from horizontal | Outboard reach | Vertical rise |
| ---: | ---: | ---: |
| 30 deg | ~58 | ~50 |
| 45 deg | ~56 | ~56 |
| 60 deg | ~50 | ~58 |

At any of these the driver assemblies reach roughly 50-58 outboard of wherever they anchor. With
tracks reaching 50 outboard (X -50 / 129), anchoring at the deck edge X 0 / 79 lands them right at the track line; anchoring further out makes them the widest point.

Fallback if canting off the rails proves awkward: mounts that rise above and to the sides of the
upper deck, carrying the drivers at the same angle but higher up.

## Display (ST7789)

| Feature | Value | Notes |
| --- | ---: | --- |
| Active area | **51.2 wide x 25.6 high** | Jim, 2026-09-20 |
| Mounting hole pattern | **26.00 x 58.25 centres, CONFIRMED** | Plate F gauge, 2026-09-20: "fits the back of the board perfectly". The pattern is settled. |
| Active area position | **6.2 from the left edge, 1.5 from the top edge** | Jim, 2026-10-01. The left edge is the pin-hole side. Derived: right 5.1, bottom 1.9. |
| Pin header | **not soldered yet** | Jim, 2026-10-01. Relieve the left (pin-hole) edge for a standard 2.54 header. |

**Two things the gauge did not cover, and the frame must.**

- **The pin header stands proud on the back.** Jim: the gauge fits the back "not counting for any
  connections rising on the pin side". The frame has to relieve that, or it will sit on the header
  instead of the board. Height not yet measured.
- **There is no opening for the active area**, which is why the gauge cannot be mounted from the
  front. The frame needs a **51.2 x 25.6** window, positioned against the hole pattern — and the
  offset between the window and the holes has not been measured either. Centred is an assumption.

## Power distribution board (power)

| Feature | Value | Notes |
| --- | ---: | --- |
| Board length | 60 | |
| Board width | 40 | |
| Height, tallest point | ~16 | |
| PCB thickness | 1.6 | Jim, 2026-09-20 |
| Mounting holes | **yes, 4, M2** | Corrected 2026-09-20 — an earlier note that it had none was wrong |
| Underside protrusion | **~6** | Longest lead today; Jim asks for room to grow, so design to 8-10 |

### Mounting hole pattern, two-reading, 2026-09-20

| Direction | Inside span `I` | Outside span `O` | c-t-c = (I+O)/2 | dia = (O-I)/2 |
| --- | ---: | ---: | ---: | ---: |
| Across width | 33 | 36 | **34.5** | 1.5 |
| Along length | 52.5 | 56 | **54.25** | 1.75 |

**Centre-to-centre 34.5 x 54.25 is solid** and cross-checks against the outline: on a 60 x 40
board that leaves 2.75 and 2.875 of margin, near enough symmetric, which is what a sane layout
looks like.

**The derived diameters disagree, and neither matches the description.** 1.5 across the width
against 1.75 along the length, where the protocol says both directions must derive the *same*
diameter or a reading is off; and Jim describes them as "exactly M2 sized", which would be about
2.0. A true 2.0 hole with c-t-c 34.5 / 54.25 predicts spans of 32.5/36.5 and 52.25/56.25 — within
about half a millimetre of the readings in every case, so the likeliest story is a 2.0 hole with
±0.25-0.5 of measurement noise.

**Confirmed by Jim 2026-09-20: the holes are 2.0.** The 1.5 / 1.75 disagreement was measurement
noise, as the prediction suggested. Pattern and diameter are both settled. It does not block the bracket either way: c-t-c positions the standoffs and is
robust, while the diameter only has to accept an M2 screw, which Jim has confirmed it does.

Still open: which edge the wires land on and which way they exit.

## Power slide switch (power, left rail rear)

Jim, calipers, 2026-10-05. Metal-shell slide switch, 3 pins.

| Feature | Value | Notes |
| --- | ---: | --- |
| Body width | 6.8 | |
| Body length (slide axis) | 12.7 | Jim: "tall"; mounted vertically, up = ON |
| Body depth | 6.37 | knob face to pin face, body only |
| Knob height above body | 5.0 | |
| Knob envelope along slide axis | 6.0 | Jim: travel "including the switch length" |
| Pin length | 3.7 | |
| Pin tip to knob top | 15.0 | = 3.7 + 6.37 + 5.0, consistent |
| Knob width | 4.0 | Jim, 2026-10-05 |

Holder: `cad/power-switch/v1b/`. It mounts on the robot-LEFT rail, which is the master object `SideRailRight`.

## Breadboard power rails x2 (buses)

| Feature | Value | Notes |
| --- | ---: | --- |
| Length | 84 | |
| Width | 9.5 | |
| Depth | 9.3 | |

## Chassis observations (from reference photo, 2026-09-15)

- **Tracks extend about 50 outboard of each deck edge**, measured 2026-09-16. Deck is X 0..79, so
  the tracks occupy X -50..0 and X 79..129, and **overall vehicle width is 179**. Consequences:
  the upper deck is now 79, flush with the aluminum, so it has no overhang at all; and a canted driver
  reaching ~50-58 outboard of its anchor becomes the widest point on the vehicle unless it is
  anchored at roughly X 0 / X 79 or further inboard.
- The vehicle is **tracked** (skid-steer), with track runs flanking the deck on both sides. Any
  hardware projecting outboard of the deck edges — notably the motor driver heatsinks — has to
  clear the track runs laterally; vertically they sit low (tops near Z 7) so anything at deck
  height or above is clear of them.
- The "battery case" is an **open 4-cell 18650 holder**, not a sealed box. Cells sit in open
  channels on spring contacts and are extracted **vertically**. The 79 x 75.5 footprint is
  consistent with four cells side by side across the width (~19 each) with their 65 length
  running fore-aft.
- Consequence: nothing may be permanently fixed over the battery unless it lifts away with the
  upper deck. This supports hanging the power distribution board from the deck underside —
  removing the deck exposes the cells for swapping.
- Because it is skid-steer, yaw scrubbing and vibration are significant. The BNO wants to sit near
  the center of rotation (roughly mid-chassis, X ~39.5, Y ~70) and ideally isolated from the
  deck's resonance rather than out at an extremity.
- The C6's external antenna is a long whip on an SMA pigtail. It needs a mounting point clear of
  the aluminum deck and of the motor drivers.

## Fit consequences against the existing chassis

Lower deck is 79 x 140 x 2. The battery box (79 x 75.5 x 19.5, Z 2..21.5) occupies the full deck
width at Y 21..96.5, leaving two exposed zones: **front Y 0..21 (21 deep)** and **rear Y
96.5..140 (43.5 deep)**. The tower bore sits in the rear zone at (39.5, 113), spanning X
32.5..46.5 and Y 106..120.

- **Motor drivers cannot lie flat anywhere.** Their 49.5 x 51 footprint exceeds the 43.5-deep
  rear zone in both orientations, and two side by side would need 102 of the 79 available width.
  Vertical or outboard mounting is mandatory, not merely preferred.
- **The power distribution board cannot sit in the rear zone.** At 60 x 40 it only fits that zone
  rotated (60 across X, 40 along Y) — but centered that way it spans X 9.5..69.5 and Y
  96.5..136.5, and the mast passes straight through it. The clear areas either side of the mast
  are only 32.5 wide, short of the 60 needed. It also cannot go in the 21-deep front zone.
  Candidate: hang it from the **underside of the upper deck** over the battery, so it lifts away
  with the deck and preserves battery access.
- **Power rails only fit fore-aft.** At 84 long they exceed the 79 deck width, so they cannot run
  crosswise. Candidate: recess them into the top of the side rails, where the wiring already runs.
- **Inter-deck height budget.** If the power distro (16 tall) hangs beneath the upper deck over
  the 21.5-high battery, the deck underside lands around Z 40, i.e. a rail roughly 38 tall above
  the lower deck. That is the current leading constraint on overall height.

## Still to measure

- **S3 + expander: hole diameter and true board outline** — the two-reading spans are in hand
  (41.75 / 65.75 c-t-c at 6.25 dia, 40.1 / 64.1 at 4.6); one direct hole-diameter measurement and
  one board-outline measurement close it. See the S3 section.

- Motor driver: total height standing on its fins, and PCB underside to fin tips (see note above)
- C6 antenna connector position and pigtail length
- ~~Track top height~~ - measured: ~5 above the deck, so tops sit near Z 7 (possibly more in motion).
- ~~Mast OD~~ - decided as a design choice, not measured: 20 OD / 12 bore, 13.8 base spigot.
- Battery: true width (79 vs 80), holder height with cells seated, cable exit and connector
- Vertical clearance needed above the holder to extract an 18650 (cells are 65 long; they may
  need to tip out rather than lift straight up)
- Battery front/rear gaps measured directly (resolves the 43.5 calculated vs ~45.5 estimated conflict in `chassis.md`)
- Motors: whether any part rises above the lower deck (Z > 2)
- Wire counts and gauges for front-to-rear runs, to size the raceway
- Screws on hand (M2 / M2.5 / M3, lengths) and heat-set insert sizes
- Printer bed size
