# Gladiator

<p align="center"><img src="photos/05-robot-v2/20261026_121446.jpg" alt="Gladiator" width="440"></p>

Gladiator started with me looking for a way to spend 50 bucks of Father's Day money. I found DFRobot's Black Gladiator — a bare frame, track, and motor assembly — along with a laser sensing board. I knew I'd have a lot of fun experimenting with it, but I had no particular direction. That's where the corny name came from.

[Project](docs/PROJECT.md) · [Build log](docs/BUILD_LOG.md) · [Hardware & CAD](hardware/README.md) · [Firmware](firmware/README.md) · [Photos](photos/README.md)

## The project

While I waited weeks for it to arrive, I started thinking about a two-deck system. When the frame got here, the circular hole in it gave me the idea for the mast.

Getting there meant learning JST connectors and headers, building an expandable power board, getting the bot moving, and eventually giving the pretty ugly pile of wires a proper body.

[The full story](docs/PROJECT.md)

## See the build

| Chassis | Robot v1 | Robot v2 |
| --- | --- | --- |
| [![Chassis](photos/thumbnails/01-chassis-v1/20261005_125831.jpg)](photos/01-chassis-v1/README.md) | [![Robot v1](photos/thumbnails/03-robot-v1/20261014_114531.jpg)](photos/03-robot-v1/README.md) | [![Robot v2](photos/thumbnails/05-robot-v2/20261026_121424.jpg)](photos/05-robot-v2/README.md) |

## Features

- **Pan and tilt:** The masthead uses a belt and pulley for pan, giving it a real 180-degree flip to present the display face.
- **Tank-style drive:** One 12V motor on each track, with motor power coming directly from the battery pack. The tracks give it tremendous all-terrain capability, and the motors have enough torque to really crank the bot forward.
- **Multiple processors working together:** The S3 at the back is the primary coordinator, the C6 next to it handles networking, and the camera board controls the display in the mast.
- **Access for maintenance:** The driver mounts, rails, and deck are designed to leave room for tools and make the electronics easier to get to.

## Current status

All of its current features are working, although the Dupont connections still cause some reliability issues.

Despite how different V2 looks, its primary change is the rails and deck: slim, arched rails with exterior mounting points, followed by a deck designed after the rails had their final fit. The other huge improvement is accessibility for repairs, and that will continue into V3.

V2 of the mast is coming next, with better balance, mobility, and access for wire routing. The [masthead revision checklist](docs/PROJECT.md#masthead-revisions-v1) covers those changes, including custom wiring.

Autonomous features are next on the software side. I want it to become something like a Roomba, mapping and moving around independently.

## Hardware and software

The networking is largely overbuilt: multiple processors working in concert, with the S3 coordinating the bot, the C6 doing networking, and the camera board running the mast display.

The motor drivers are the main way of getting heat out. Their mounts were designed to look cool, take up minimal room without expanding the bot's footprint, vent heat well, and provide easy access to the electronics. Most of the redesigns have been about access for tools and maintenance. The primary design has remained largely the same.

## Credits and licensing

<!-- WRITE: Credits, attribution, and your licensing choice. No license has been assigned for you. -->
