# Loud Numbers — developer notes

## Collaboration rules

- **Design decisions belong to the owner.** Anything a user could notice —
  behaviour, mappings, error states, edge-case handling, panel/UI, patch
  format — gets asked about BEFORE implementing, with options and
  trade-offs, even when it surfaces mid-task inside a "bug fix". Don't
  bundle silent design choices into technical work.
- Code structure decisions (how the code is organised internally) are
  delegated, as long as the code is clearly commented for a non-expert
  C++ reader.

A single-module VCV Rack plugin for data sonification. All module logic lives
in `src/LoudNumbers.cpp`; `src/rapidcsv.h` is a vendored third-party CSV
parser (https://github.com/d99kris/rapidcsv); `src/plugin.cpp`/`plugin.hpp`
are standard Rack boilerplate.

## Building

Requires the Rack SDK (https://vcvrack.com/downloads/, "Rack SDK" section).

```sh
export RACK_DIR=/path/to/Rack-SDK
make          # compile plugin.so
make dist     # package dist/LoudNumbers-<version>-<platform>.vcvplugin (needs zstd)
make install  # install into the local Rack user folder for testing
```

There are no submodules and no `make dep` dependencies on Linux/Mac native
builds. CI (`.github/workflows/build-plugin.yml`) cross-builds win-x64,
lin-x64, mac-x64, and mac-arm64 on every push and uploads `.vcvplugin`
artifacts; a `v*` tag that matches the `plugin.json` version creates a
GitHub release.

After every push that triggers a CI build, without being asked, give the
owner: (1) a link to the workflow run so they can download the
**mac-arm64** artifact from its Artifacts section, and (2) the install
path — drop the `.vcvplugin` into
`~/Library/Application Support/Rack2/plugins-mac-arm64` (don't extract
it) and restart Rack.

## Releasing to the VCV Library

1. Bump `version` in `plugin.json` (Rack 2 versions look like `2.x.y`).
2. Push to `main` (or merge the release PR).
3. Comment the commit hash on the plugin's thread in
   https://github.com/VCVRack/library/issues — VCV builds the binaries.

## Design philosophy

- The module is a neutral data-to-CV converter, not an instrument: no internal
  clock, no quantizer, no sound generation. Musicality comes from the patch
  (external clock into TRIG, quantizer after V/OCT). Stay modular-idiomatic.
- Missing/non-numeric CSV cells become NaN: excluded from min/max, produce no
  gate (missing data is audible as silence). In the display, a gap breaks
  the line with a notch: the line reaches ¾ of a step past the valid
  points on either side, so an isolated valid point still shows as a
  short fragment. Notches under a pixel wide (dense data, short gaps)
  are drawn straight through; long gaps stay visible at any density.
  Missing points before the first / after the last valid point draw
  nothing.
- V/oct RANGE mapping is intentionally asymmetric: ranges 1-3 octaves span
  0V..+range; 4-8 pin the top at +4V and grow downward. `voctRange()`
  implements it and the knob's tooltip (`RangeQuantity`) spells out the
  exact span, so the behaviour explains itself in the UI.
- Patches embed the selected column's data (JSON nulls for NaN), capped
  at 10k values because dataToJson runs on every autosave; bigger
  datasets stay path-only. On open, the CSV file wins if it's readable
  (so edits show up); the embedded copy is the fallback, and in that
  state column switching is locked (`embeddedonly`) until
  "Reload CSV from disk" succeeds. A reload with the file still missing
  shows the invalid-CSV state rather than silently keeping stale data.
- Looping is done by the user patching END → RESET, not built in. RESET
  arms rather than plays: it returns the playhead to datapoint 0 with no
  gate, holding the CV outputs, and the next TRIG plays datapoint 0 in
  time with the clock. END fires as the last datapoint plays
  (end-of-cycle), so an END → RESET loop is gapless (N datapoints = N
  clock ticks) and resets are always in time. The display playhead
  tracks what's sounding, not the armed position: a filled circle marks
  the last-played datapoint, and a hollow circle on datapoint 0 means
  the sequence is cued but hasn't begun (fresh data, or a manual
  reset). A reset arriving while END is still high counts as an
  END → RESET loop reset instead: the filled circle stays on the last
  datapoint while it plays out. The circle disappears if the playhead
  runs past the end without a reset.
  Known edge cases of the "END still high" test (owner-approved,
  display-only — the sequencing is identical either way): a manual
  reset landing inside END's 10ms pulse reads as a loop reset, so no
  hollow circle appears; an END → RESET connection routed through
  modules that delay the trigger by more than 10ms reads as manual, so
  the hollow circle flashes at each loop point. And since a missing
  (NaN) datapoint 0 has no vertical position, no cue circle is drawn
  while cued on it.
- Columns with no numeric values can't be selected (greyed out in the
  menu); a file with no numeric columns at all is an invalid CSV.
- Flat data (all values identical, incl. single-row files) maps to the
  bottom of each output range.
- If a saved patch's column no longer exists in the file (matched by
  name), the display says "Right-click to select a column of data"
  rather than guessing a different column.

## Known constraints

- `process()` runs on the audio thread; `processCSV()` and the DataViz widget
  run on UI-side threads. Loaded data crosses that boundary only as an
  immutable `Dataset` snapshot published via `getDataset()`/`setDataset()`;
  the remaining shared scalars (`row`, `playingrow`, `cued`, `badcsv`,
  `resetarmed`) are atomics.
  Don't add unsynchronized shared state — extend the snapshot, or use an
  atomic, instead.
- The owner develops on macOS only; Windows/Linux verification happens via CI.
