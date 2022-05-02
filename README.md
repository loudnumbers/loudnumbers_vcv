# Loud Numbers VCV

A free VCV Rack plugin for data sonification.

## Installation

Install from the VCV Rack library.

## How to use

Right-click to load a CSV file, and then right-click again to select a column of data from that file.

Your CSV file must have a single header row containing column names, and you'll only be able to sonify columns containing numbers. Any missing or non-numeric values in your data will be converted to zeros.

Send a trigger signal into the TRIG input to process the first datapoint and move to the next one. Send a trigger into the RESET input to return to the start of the dataset.

The top two outputs generate voltages from -5V to 5V and 0 to 10V respectively. The lower left output generates 1V/Oct pitch CV, scaled to the number of octaves selected using the RANGE knob. The lower right output generates a gate as each new datapoint is processed - change the lenth of this gate with the LENGTH knob.

## FAQ

**Q: What is data sonification?**

A: Data sonification is the process of turning data into sound. It's a bit like data visualization, but you map numbers onto the properties of sound (volume, pitch, reverb, etc), rather than visuals (colour, shape, size).

**Q: My CSV is invalid!**

A: Use [CSVLint](https://csvlint.io/) to check if your CSV is valid. If it is, submit an issue and attach the CSV file you're trying to load and I'll take a look.

**Q: How do I make the output sound more musical?**

A: Process the pitch information through a quantizer and consider adjusting the length of your dataset to a multiple of four.

**Q: Where can I get some data to try it with?**

A: Try sonifying [climate data](https://raw.githubusercontent.com/loudnumbers/loudnumbers_vcv/main/temperature.csv), or [sunspot data](https://raw.githubusercontent.com/loudnumbers/loudnumbers_vcv/main/sunspots.csv). The [Data is Plural archive](https://www.data-is-plural.com/) is a great source for more interesting datasets.

## Loud Numbers?

It's the name of [my data sonification studio](https://www.loudnumbers.net/). We have a podcast that's worth a listen if you want to hear what's possible with sonification.

## Thanks

To Mahlen Morris for his generous guidance in the ways of C++, and to Miriam Quick, Dewb, TomW, Obakegaku for thorough testing.
