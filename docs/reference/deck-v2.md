> Historical engineering reference, imported from the CAD source snapshot of 2026-10-26.
> Later component snapshots are indexed in [hardware](../../hardware/README.md).

# Upper deck v2 — design spec

Current robot frame (2026-10-09): **mast is FRONT (+Y)**; low X LEFT, high X
RIGHT. See [ORIENTATION.md](https://github.com/iThinkiKhan/gladiator-cad/blob/db25f68d589b59520871f2c27f7898625261f611/docs/ORIENTATION.md). The dated spec below uses legacy
mast-rear directional names; existing dimensions/holes/parameter aliases stay fixed.

Started 2026-09-20, **revised the same day**. Not yet cut.

## Revision: the power board does NOT hang from the deck

The first version of this spec had the board hanging under the deck on four M2 through-holes.
**That was wrong.** It was built on the assumption that the board could face either way, and it
cannot: Jim, 2026-09-20 —

> The main thing we want is lots of space at the TOP of the power board, because that is where all
> the connections plug in. It should be comfortable to move/remove plugs, JSTs, and duponts.
> Everything plugs down into it vertically.

A board hung under the deck has its top face 2-3 mm from the deck underside. Nothing can plug into
it, either way round: connectors up means they hit the deck, connectors down means they point into
the battery. The mount has to sit the board on something with open air above it.

**So deck v2 goes back to being only the S3 fix**, and the power board keeps a leg mount. That is
the option Jim raised as the alternative, and it is the right one.

## What the leg mount gives, measured on the solids

`PowerShield` already implements it and is clear of everything — zero clash against the battery,
both rails, the mast base and tube, and both decks. Probing straight up through the board footprint
(X 20..60, Y 29..89) finds **nothing overhead but the upper deck at Z 48**.

| | |
| --- | ---: |
| Battery top | Z 21.50 |
| Upper deck underside | Z 48.00 |
| **Total budget** | **26.50** |
| Board top if it sits on the shield lip at Z 30 | Z 31.60 |
| **Clear air above the board, installed** | **~20.4** |

And with the deck off — six M3 screws — access from above is unlimited, which is what makes
plugging and unplugging comfortable.

## The budget is tight, and the current shield does not fit the board

The stack from the battery up: solder side **6.0**, PCB **1.6**, then whatever the connectors stand
once plugged in. Against 26.50 of budget that leaves about **4.9 mm** to spend on clearance under
the board, clearance over the connectors, and the shield floor itself — assuming connectors of
about 14, which is a Dupont-sized guess and **not measured**.

A workable arrangement, if 14 holds:

| | Z |
| --- | ---: |
| Battery top | 21.50 |
| Shield floor (thinned to 1.0) | 22.50 .. 23.50 |
| Solder blobs, 6 proud | 24.00 .. 30.00 |
| PCB | 30.00 .. 31.60 |
| Connectors, ~14 | 31.60 .. 45.60 |
| Upper deck underside | 48.00 |

**`PowerShield` as drawn does not accommodate this.** Its floor sits at Z 24..26 and its lip at
Z 26..30, so a board resting on the lip has only **4.0 mm** of space beneath it for **6.0 mm** of
solder. The floor has to drop and thin, and at 1.0 thick starting at Z 22.5 it clears the battery
by only 1.0. Every gap in that table is between 0.5 and 2.4 — there is no comfortable version of
this.

## The one number that decides it

**How tall is the tallest connector, measured from the PCB top surface, plugged in?**

At 14 the arrangement above works with about 2.4 to spare. At 17 it does not fit at all and
something else has to give — a thinner shield floor, dropping the board closer to the battery, or
accepting that the upper deck has to come off before anything can be unplugged.

Also still open: **which edge the wires exit**, which decides the board's rotation and whether the
shield's open sides face the right way.


## Wire pass-through (Jim, 2026-09-20)

A rectangular opening forward of the mast, to route wires through the deck.

**The clear band, mapped against every obstruction** — deck bosses, the mast collar, and the S3,
breadboard, mast, antenna and driver envelopes:

| Y | Clear X |
| ---: | --- |
| 90.0 | 0.0 .. 79.2 |
| 92.0 | 0.0..11.2, **18.0..61.2**, 68.0..79.2 |
| 95.0 | 0.0..10.0, 12.2..17.0, **19.2..60.0**, 62.2..67.0, 69.2..79.2 |
| 98.0 | 0.0..11.2, **18.0..61.2**, 68.0..79.2 |
| 100.0 | 0.0..34.5, 44.8..79.2 (the mast collar has started) |

So the usable window is bounded by the **S3 board's rear edge at Y 89**, the **mast collar at
Y 99**, and the **two driver-mount bosses at X 10..19 and X 60..69**. That gives roughly
**X 19.5..60, Y 89.5..99** — about 40 x 9.5 at most.

Three candidates were checked against everything; **all three are clear and fully on the plate**:

| Option | Rectangle | Size |
| --- | --- | ---: |
| On the S3 side | X 20.0..41.5, Y 90..98 | 21.5 x 8.0 |
| Centred on the mast axis | X 29.5..49.5, Y 90..98 | 20.0 x 8.0 |
| Full width between the driver bosses | X 20.0..59.0, Y 90..98 | 39.0 x 8.0 |

**Needs picking.** "Centered and forward of the mast, on the S3 side" reads two ways: centred on the
deck, or sitting on the S3 side of the mast axis. The first and third options are the S3-side and
maximum readings; the middle one is the centred-on-the-mast reading.

Only 8-9.5 mm of depth is available whichever is chosen, because the S3 and the mast collar close
in from both sides. If a deeper opening is wanted, the alternative is to put it **under the S3
board** — the board stands on 6 mm bosses, so wires can run in that gap — but that trades easy
access for depth.

## Deck v2 itself

With the power board mount removed from its scope, v2 has two jobs: **fix the S3 boss pattern**
and **add the wire pass-through**.

**The S3 pattern is still the blocker, and it is worth being clear about what did and did not clear
it.** The plate F gauge confirmed the **display's** hole pattern (26.00 x 58.25) — that is the
ST7789, a different board. The **S3 + expander** pattern is untouched by that result and still
carries the contradiction below. The deck as printed has its S3 bosses at 30.9 x 55.0, which is the
figure already disproved. That remains blocked on the contradiction in `measurements/components.md` — the hole
diameter measured directly (4.6) disagrees with the diameter derived from the two-reading spans
(6.25), and the boss positions depend entirely on which is right.

Two measurements clear it: the S3 hole diameter on its own, and the bare board outline.
