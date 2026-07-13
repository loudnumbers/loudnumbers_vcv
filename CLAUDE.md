# Loud Numbers — developer notes

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
  gate (missing data is audible as silence).
- V/oct RANGE mapping is intentionally asymmetric: ranges 1-3 octaves span
  0V..+range; 4-8 pin the top at +4V and grow downward.
- Looping is done by the user patching END → RESET, not built in.

## Known constraints

- `process()` runs on the audio thread; `processCSV()` and the DataViz widget
  run on UI-side threads. They currently share mutable state (`data`,
  `datamin`, `datamax`, `datalength`, `row`) without synchronization — the
  suspected cause of the crashes in issue #4. Don't add more unsynchronized
  shared state; the planned fix is to publish an immutable dataset snapshot.
- The owner develops on macOS only; Windows/Linux verification happens via CI.
