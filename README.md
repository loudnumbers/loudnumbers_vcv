# Loud Numbers VCV

A free VCV Rack plugin for data sonification.

## Installation

Install from the VCV Rack library.

## How to use

Right-click to load a CSV file, and then right-click again to select a column of data from that file.

Your CSV file must have a single header row containing column names, and you'll only be able to sonify columns containing numbers. Any missing or non-numeric values in your data will be replaced with null values that don't fire a gate.

Your data is saved inside the patch file (up to 10,000 values), so a patch keeps playing even if you share it with someone else or the CSV file gets moved or deleted. If the file is still where it was, it's re-read when the patch opens, so edits to it show up — and you can right-click → *Reload CSV from disk* to re-read it at any time. Without the file, the patch plays its embedded copy and the column menu is locked until a reload succeeds.

Send a trigger signal into the TRIG input to process the first datapoint and move to the next one. Send a trigger into the RESET input to return to the start of the dataset — nothing plays until the next trigger arrives at TRIG, which plays the first datapoint in time with your clock. The END output fires a trigger as the last datapoint plays, so patching END → RESET loops the dataset seamlessly.

The top two outputs generate voltages from -5V to 5V and 0 to 10V respectively. The lower left output generates 1V/Oct pitch CV, scaled to the number of octaves selected using the RANGE knob. Ranges of 1–3 octaves rise from 0V; from 4 octaves upward the top pitch stays pinned at +4V and wider ranges add lower notes instead of ever-higher ones — hover over the knob to see the exact voltage span. The lower right output generates a gate as each new datapoint is processed - change the lenth of this gate with the LENGTH knob.

## FAQ

**Q: What is data sonification?**

A: Data sonification is the process of turning data into sound. It's a bit like data visualization, but you map numbers onto the properties of sound (volume, pitch, reverb, etc), rather than visuals (colour, shape, size).

**Q: My CSV is invalid!**

A: Use [CSVLint](https://csvlint.io/) to check if your CSV is valid. If it is, submit an issue and attach the CSV file you're trying to load and I'll take a look.

**Q: I'm getting crashes when loading a CSV file**

A: This should be fixed in recent versions. If you're still seeing a crash, please submit an issue and attach the CSV file you're trying to load. Re-encoding your csv file to UTF-8 may help in the meantime.

**Q: How do I make the output sound more musical?**

A: Process the pitch information through a quantizer and consider adjusting the length of your dataset to a multiple of four.

**Q: Where can I get some data to try it with?**

A: Try sonifying [climate data](https://raw.githubusercontent.com/loudnumbers/loudnumbers_vcv/main/temperature.csv), or [sunspot data](https://raw.githubusercontent.com/loudnumbers/loudnumbers_vcv/main/sunspots.csv). The [Data is Plural archive](https://www.data-is-plural.com/) is a great source for more interesting datasets.

## Loud Numbers?

It's the name of [my data sonification studio](https://www.loudnumbers.net/). We have a podcast that's worth a listen if you want to hear what's possible with sonification.

## Thanks

To Mahlen Morris for his generous guidance in the ways of C++, and to Miriam Quick, Dewb, TomW, Obakegaku for thorough testing.
