# Changelog

Dates are those of the version-bump commit; VCV Library availability followed shortly after each.

## 2.1.0 (2026-07-19)

- Click a datapoint on the chart to cue it for the next trigger; hovering highlights the point under the cursor (#14)
- Selectable CSV delimiter: auto-detect, comma/tab/semicolon presets, or a custom character; .tsv and .txt files can now be loaded (#16)
- Patches embed the loaded data, so they keep playing on machines where the CSV file is missing (#18)
- Missing datapoints play as silence and appear as gaps in the display line (#22)
- RESET now arms: the next trigger plays datapoint 0 in time with the clock, and END fires as the last datapoint plays — so END→RESET loops are gapless
- Display playhead: filled circle = datapoint sounding, hollow circle = cued and waiting
- RANGE knob tooltip spells out the exact voltage span (#19)
- Clearer error states: "CSV file not found" (with a reload hint) vs "Invalid CSV"; reloading from disk keeps your selected column, matched by name
- Fixed crashes when loading CSV files (#4)

## 2.0.4 (2022-10-21)

- Fixed "Invalid CSV" showing after a restart when the module was still on its default data (#15)

## 2.0.3 (2022-09-23)

- Added the END output, which fires a trigger when the end of the data is reached — patch END → RESET to loop (#7)
- Reworked the panel layout: RANGE sits with the V/OCT output and LENGTH with the GATE output (#8)
- Fixed the display sometimes not drawing when values sit near the min/max, and made the datapoint circle smaller (#11)
- Fixed min/max calculation for data that starts with missing values
- Fixed loop behaviour and some drawing bugs; performance improvements

## 2.0.2 (2022-06-22)

- Reset now returns the outputs to 0V instead of freezing them at their last values (#10)
- Fixed a build failure on Windows (#9, thanks @SteveRussell33)

## 2.0.1 (2022-06-06)

- Missing and non-numeric values become nulls that fire no gate, instead of being read as zero (#2)
- The module browser and library page now show the module with its default data instead of an empty display (#3)
- Loading a new CSV no longer keeps the previous file's column selection (#5)
- New panel layout (#8, first pass)
- The load dialog only offers CSV files
- First attempt at fixing a crash when loading certain CSVs (#4)

## 2.0.0 (2022-05-02)

First release. Load a CSV and sonify a column of it: TRIG steps through the data, RESET returns to the start, and the data comes out as -5V–5V, 0V–10V, and V/OCT (with a RANGE knob) plus a GATE output (with a LENGTH knob). The display draws the selected column, and the file path and column are remembered in the patch.
