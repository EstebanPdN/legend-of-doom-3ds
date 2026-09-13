# Built-in updater

Start opens the existing menu. Update sits between Save Game and Quit Game in
both the title menu and the pause menu (which retains Resume Game first).
The Update caption is copied from the engine's Options renderer, preserving its
font and red translation. Information uses NewSmallFont, like Controls. The
changelog stays on the top screen, with bundled notes available offline. Both
screens retain the font shading and dark outlines at the same scale. Release
notes use the dimmed TITLEPIC artwork, independently of the idle lore page or
paused scene, and wrap to the measured font width. The lower information and
actions are centered between the fixed Update heading and Back button.

Opening Update checks GitHub. The selected stable/experimental channel persists
under `sdmc:/3ds/legend-of-doom/update/channel.txt`. A selects, B returns or
cancels a transfer, and the bottom screen supports touch. L/R pages through the changelog without leaving the update controls. Installation requires an explicit Yes; No is selected
by default. Installation closes the game, so save progress first.

CIA updates include the matching engine and game resources. The updater checks
HTTPS certificates, download size, SHA-256 and the CIA title ID before asking
AM to overwrite the installed title. It never deletes that title first. HOME,
sleep and menu cancellation are disabled during installation. Temporary
downloads are removed after completion/failure. Saves and configuration are
outside the CIA and are preserved. Errors are recorded in
`sdmc:/3ds/legend-of-doom/update.log`.

The 3DSX build displays `INSTALL THE CIA TO UPDATE`: replacing only its executable
would leave its external engine/game data stale.

## Release compatibility

The source is `EstebanPdN/legend-of-doom-3ds`, via GitHub's public releases API.
Drafts are ignored. The highest numeric version in the selected channel is
validated; an invalid newest asset is an error, not a fallback to an older build.
Supported tags are `v1.0`, `v1.0.1` and `v1.0-E1` (the leading v is optional).
Stable versions sort after experiments with the same base version. Equal or
older versions cannot be installed.

The CIA asset must have GitHub's SHA-256 digest and one of these names:

- `legend-of-doom-3ds-v1.0.cia`
- `legend-of-doom-3ds-v1.0-hardware-hybrid-0123456789ab.cia`

The version must match its tag; the latter suffix is a 12-digit hex build ID.
The download URL must belong to that exact repository, tag and asset. Multiple
matching CIA assets are rejected as ambiguous. The updater reads up to 100
releases, limits metadata to 2 MiB and CIA files to 128 MiB.

## Local verification

Build the native Jansson 2.14 source with CMake, then run on macOS:

```sh
python3 platform/3ds/tests/run-update-tests.py --jansson-prefix /path/to/native/jansson --live-check
```

The manifest harness uses AddressSanitizer and UndefinedBehaviorSanitizer.
The I/O harness exercises transfer bounds, real host SHA-256, corrupt/truncated
files, cancellation, wrong title, insufficient space and partial install writes.
AM is mocked; this does not prove installation on a physical console.
The optional live check uses the bundled CA to query both GitHub channels.

The build accepts `LOD3DS_SCRIPT_VALIDATOR` pointing to a native executable built
from this tree. Its `gzdoom.pk3` and `game_support.pk3` must be built and placed
next to that executable before running the check.

On a New 3DS, test Update from title and pause, both channels, B/Start/touch/L/R,
no Wi-Fi, cancellation, and returning to gameplay. A channel without a public release displays
`NO STABLE RELEASE YET` / `NO EXPERIMENTAL RELEASE`.
Installing a newer release must still be checked on hardware once a suitable
release exists. Creating this build does not publish anything.

The first local updater build used version 0.47. Install v0.8-E3 manually with
FBI/QR: that older updater orders 0.47 above 0.8 and cannot migrate itself to
this numbering. The 0.8-E series compares subsequent experiments numerically.

Version 1.0 is a stable release. Select Stable in v0.8-E3 to receive it; the
experimental channel only lists experimental releases.
