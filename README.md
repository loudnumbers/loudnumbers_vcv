# Loud Numbers VCV

A VCV Rack plugin for Data Sonification.

Download for Windows: <https://www.dropbox.com/s/0ab3yapm2gcvkj4/loudnumbers_vcv_win.zip?dl=1>
Download for Mac: <https://www.dropbox.com/s/ec7titekcczg9d3/loudnumbers_vcv_mac.zip?dl=1>

Unzip to your plugins folder:

Windows: My Documents/Rack2/
MacOS: Documents/Rack2/

Then restart VCV Rack if it's already running and add the module to your rack. Right-click to load a CSV file, and then right-click again to select a column of data from that file.

Your CSV file should have a single header row. Any missing or non-numeric values will be converted to zeros. Use [CSVLint](https://csvlint.io/) to check if your CSV is valid. If it's invalid, VCV Rack will likely crash.

Some csv files to try it out with:
Climate data:
Sunspot data: