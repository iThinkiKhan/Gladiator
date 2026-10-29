# How Gladiator started

[Project home](../README.md) · [Build photos](../photos/README.md)

## 50 bucks and a corny name

Gladiator started with me looking for a way to spend 50 bucks of Father's Day money. I found a bare frame, track, and motor assembly from DFRobot, along with a laser sensing board. I knew I'd have a lot of fun experimenting with it, but I had no particular direction. It was called Black Gladiator — hence the corny name.

It took weeks to arrive, which gave me some time to think about what to make. I had a general idea of a two-deck system. When the frame arrived, I saw the circular hole in it, and that became the idea for the mast.

## The power board

I spent a lot of time designing the power board. That was an achievement by itself, and the first time I'd used JST connectors and headers to make a board like that.

Working on smaller vehicles had taught me that soldered connections were too restrictive when I wanted to change things. I knew this needed to remain expandable. So I learned JSTs and all that, and designed a board I knew I could expand.

It now has ten 5V headers, ten ground headers, five VBATT headers, two JSTs for output, and another JST for input. All the 5V and ground headers are currently used, and I've expanded both once already.

Once I tackled that, I was able to get the bot actually moving, confirm it worked, and play with some software.

## A proper body

At that point, it was a pretty ugly pile of wires and parts on top of the frame. It had a bad tendency to short, and the fuse saved me from fires a couple of times. I needed a proper body, so I began designing the rails and deck.

That took several redesigns. I eventually ended up with fairly slim, arched rails with hard points for exterior mounting, and a deck that was designed *after* the rails had their final fit.

Despite how different V2 looks, the rails and deck are its primary change. The other huge improvement is accessibility for repairs, and that will continue into V3.

## The tracks and masthead

The motors get their power directly from the battery pack. It operates much like a tank, with one 12V motor on each track. The tracks give it tremendous all-terrain capability, and the motors have enough torque to really crank the bot forward.

The masthead has pan and tilt. The pan mechanism uses a belt and pulley to achieve a real 180-degree flip to present the display face.

The motor drivers are the main way of getting heat out. Their mounts were designed to look cool, take up minimal room without expanding the bot's footprint, vent heat well, and provide easy access to the electronics. Most of the redesigns have been about access for tools and maintenance. The primary design has remained largely the same.

## The networking

The networking is largely overbuilt. Multiple processors work in concert, with the S3 on the back as the primary coordinator. The C6 next to it does networking, and the camera board controls the display in the mast.

## Where it is now

All of its current features are working, although the Dupont connections still cause some reliability issues.

## What's next

V2 of the mast is coming next, with better balance, mobility, and access for wire routing. Now that I have it working, I know how I want to improve it next.

Autonomous features are next on the software side. I want to make it into something like a Roomba, mapping and moving around independently.

### Masthead revisions v1

- [ ] Additional access in the mast.
- [ ] More belt tension. I modified v1 with a Dremel.
- [ ] Deeper indents for the servo horns.
- [ ] Stronger legs in the screen holder.
- [ ] Fix the fit of the SD-card-shaped lock. Bed issues kept it from fitting.
- [ ] A taller mast and better head balance. The camera side is longer and heavier.
- [ ] Move the screen out or modify the neck so it doesn't limit tilt so much.
- [ ] A MUCH better locking mechanism for the shaft/head.
- [ ] Custom wiring.
