#include "plugin.hpp"
#include <vector>
#include <algorithm>
#include <iterator>
#include <atomic>
#include <memory>
#include <fstream>
#include <math.h>
#include <osdialog.h>
#define HAS_CODECVT
#include "rapidcsv.h" //https://github.com/d99kris/rapidcsv

std::vector<float> defaultdata{-0.267,-0.007,0.046,0.017,-0.049,0.038,0.014,0.048,-0.223,-0.14,-0.068,-0.074,-0.113,0.032,-0.027,-0.186,-0.065,0.062,-0.214,-0.149,-0.241,0.047,-0.062,0.057,0.092,0.14,0.011,0.194,-0.014,-0.03,0.045,0.192,0.198,0.118,0.296,0.254,0.105,0.148,0.208,0.325,0.183,0.39,0.539,0.306,0.294,0.441,0.496,0.505,0.447,0.545,0.506,0.491,0.395,0.506,0.56,0.425,0.47,0.514,0.579,0.763,0.797,0.677,0.597,0.736};
float defaultdatamin = *std::min_element(defaultdata.begin(), defaultdata.end());
float defaultdatamax = *std::max_element(defaultdata.begin(), defaultdata.end());
int defaultdatalength = static_cast<int>(defaultdata.size());

// This function scales a number from one range to another
float scalemap(float x, float inmin, float inmax, float outmin, float outmax)
{
	// Data with no range (every value identical, or a single datapoint)
	// can't be mapped; treat a flat line as baseline and return the bottom
	// of the output range rather than dividing by zero.
	if (inmax == inmin)
	{
		return outmin;
	}
	return outmin + (outmax - outmin) * ((x - inmin) / (inmax - inmin));
};

// The V/oct span for a given RANGE knob value. The mapping is
// deliberately asymmetric: ranges of 1-3 octaves span 0V up to +range,
// but from 4 octaves the top pins at +4V and the range grows downward.
// That's because most users patch oscillators from somewhere mid-range,
// and going more than ~4 octaves up from there is rarely useful — so a
// wide range adds low notes rather than ever-higher ones. Shared by the
// audio path (setCVOutputs) and the RANGE knob's tooltip.
static void voctRange(float range, float &voctmin, float &voctmax)
{
	if (range < 4)
	{
		voctmin = 0;
		voctmax = range;
	}
	else
	{
		voctmin = 4 - range;
		voctmax = 4;
	}
}

// Custom tooltip for the RANGE knob (issue #19): the asymmetric octave
// mapping above is impossible to discover from the panel, so spell out
// the exact voltage span right where the user is looking when they turn
// the knob, e.g. "2 octaves (0V to +2V)" or "6 octaves (-2V to +4V)".
struct RangeQuantity : ParamQuantity
{
	std::string getDisplayValueString() override
	{
		int range = (int)std::round(getValue());
		float voctmin;
		float voctmax;
		voctRange(range, voctmin, voctmax);
		std::string lo = (voctmin == 0.f) ? "0V" : string::f("%gV", voctmin);
		return string::f("%d octave%s (%s to +%gV)",
						 range, (range == 1) ? "" : "s", lo.c_str(), voctmax);
	}
};

// A complete snapshot of a loaded dataset: the numbers plus everything
// calculated from them. Snapshots are never modified after being built —
// loading new data builds a whole new snapshot and swaps it in. That swap
// is what keeps the audio thread safe while the UI loads a CSV (issue #4):
// the audio thread keeps using the snapshot it grabbed at the start of the
// current process() call, even if the UI publishes a new one meanwhile.
struct Dataset
{
	std::vector<std::string> columns;
	// For each column: does it contain at least one numeric value?
	// Columns that don't can't be selected for sonification.
	std::vector<bool> colhasdata;
	std::vector<float> data;
	float datamin = 0.f;
	float datamax = 0.f;

	int length() const { return static_cast<int>(data.size()); }

	// Calculate min and max, ignoring NaN (missing) values
	void calcMinMax()
	{
		datamin = 0.f;
		datamax = 0.f;
		bool first = true;
		for (float v : data)
		{
			if (std::isnan(v))
			{
				continue;
			}
			if (first)
			{
				datamin = datamax = v;
				first = false;
			}
			else
			{
				datamin = std::min(datamin, v);
				datamax = std::max(datamax, v);
			}
		}
	}
};

// A human-readable name for a delimiter character, for the context menu
// (e.g. "comma", "tab"). Anything without a common name shows as itself.
static std::string delimiterName(char c)
{
	switch (c)
	{
	case ',':
		return "comma";
	case '\t':
		return "tab";
	case ';':
		return "semicolon";
	case '|':
		return "pipe";
	case ' ':
		return "space";
	case ':':
		return "colon";
	default:
		return std::string(1, c);
	}
}

// Guess a CSV's delimiter (issue #16) by counting candidate separators in
// the file's first non-empty line and returning the most common one. This
// is the same kind of heuristic a spreadsheet import uses; it can't be
// perfect, so it falls back to a comma when nothing stands out. Only used
// in DELIM_AUTO mode.
static char sniffDelimiter(const std::string &path)
{
	std::ifstream file(path);
	std::string line;
	while (std::getline(file, line))
	{
		if (!line.empty())
		{
			break;
		}
	}

	// The "common set" of candidates the owner chose for auto-detect.
	const char candidates[] = {',', '\t', ';', '|'};
	char best = ',';
	int bestcount = 0;
	for (char cand : candidates)
	{
		int count = 0;
		for (char ch : line)
		{
			if (ch == cand)
			{
				count++;
			}
		}
		if (count > bestcount)
		{
			bestcount = count;
			best = cand;
		}
	}
	return best;
}

struct LoudNumbers : Module
{

	enum ParamId
	{
		RANGE_PARAM,
		LENGTH_PARAM,
		PARAMS_LEN
	};
	enum InputId
	{
		TRIG_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId
	{
		END_OUTPUT,
		MINUSFIVETOFIVE_OUTPUT,
		ZEROTOTEN_OUTPUT,
		VOCT_OUTPUT,
		GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId
	{
		LIGHTS_LEN
	};

	LoudNumbers()
	{
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<RangeQuantity>(RANGE_PARAM, 1, 8, 2, "Octave range");
		getParamQuantity(RANGE_PARAM)->snapEnabled = true;
		configParam(LENGTH_PARAM, 0.001f, 1.f, 0.1f, "Gate length", " s");
		configInput(TRIG_INPUT, "Trigger");
		configInput(RESET_INPUT, "Reset");
		configOutput(END_OUTPUT, "End of Data Trigger");
		configOutput(MINUSFIVETOFIVE_OUTPUT, "-5V to 5V");
		configOutput(ZEROTOTEN_OUTPUT, "0 to 10V");
		configOutput(VOCT_OUTPUT, "Volts per octave");
		configOutput(GATE_OUTPUT, "Gate");

		// Start with the default dataset so the module works out of the box
		auto ds = std::make_shared<Dataset>();
		ds->columns = {"Temps 1956-2019"};
		ds->colhasdata = {true};
		ds->data = defaultdata;
		ds->calcMinMax();
		dataset = ds;
	}

	// The current dataset. Only access it through getDataset()/setDataset(),
	// which make the handover between the UI and audio threads safe.
	std::shared_ptr<const Dataset> dataset;

	std::shared_ptr<const Dataset> getDataset()
	{
		return std::atomic_load(&dataset);
	}

	void setDataset(std::shared_ptr<const Dataset> ds)
	{
		std::atomic_store(&dataset, std::move(ds));
	}

	// Data variables
	std::string currentpath = "none";
	int colnum = 0; // -1 means no column is selected (see COLUMN_NONE)
	std::string savedcolname; // column name restored from a saved patch
	bool csvloaded = false;

	// CSV delimiter selection (issue #16). The parser reads one character
	// as the column separator. DELIM_AUTO sniffs the file's header and
	// picks the most common candidate; the others are fixed characters, and
	// DELIM_CUSTOM uses whatever single character the user typed. The choice
	// is saved in the patch so a reopened file re-parses the same way. These
	// are UI-thread only — the delimiter shapes the Dataset inside
	// processCSV and never touches the audio thread — so no atomics needed.
	static const int DELIM_AUTO = 0;
	static const int DELIM_COMMA = 1;
	static const int DELIM_TAB = 2;
	static const int DELIM_SEMICOLON = 3;
	static const int DELIM_CUSTOM = 4;
	int delimMode = DELIM_AUTO;
	char customDelim = '|';	  // used when delimMode == DELIM_CUSTOM
	char detectedDelim = ','; // last auto-detect result, for the menu label

	// Set when the module is playing the copy of the data embedded in
	// the patch because the CSV file couldn't be read (issue #18). Only
	// the selected column's data is embedded, so column switching is
	// locked in the menu until a reload from disk succeeds.
	bool embeddedonly = false;

	// Datasets bigger than this aren't embedded in the patch: dataToJson
	// runs on every autosave (~15s), and huge patches would make that
	// slow. Those patches keep the old path-only behaviour.
	static const int EMBED_MAX_VALUES = 10000;

	// Values for colnum / the column request passed to processCSV()
	static const int COLUMN_NONE = -1;	  // nothing selected: prompt the user
	static const int COLUMN_AUTO = -2;	  // pick the first numeric column (new file)
	static const int COLUMN_RESTORE = -3; // restore a saved patch's column by name
	std::atomic<bool> badcsv{false};
	// When badcsv is set, this says which kind of problem it was: the
	// file is missing (moved/renamed/deleted — fixable by putting it
	// back and reloading) versus present but unparseable. The display
	// words its message accordingly.
	std::atomic<bool> filemissing{false};
	std::atomic<int> row{-1}; // because the first thing we do is increment it

	// Set when a reset has moved the playhead back to the first datapoint
	// but it hasn't played yet: the next TRIG should play datapoint 0
	// instead of stepping past it. Atomic because processCSV() (UI thread)
	// clears it while process() (audio thread) reads it.
	std::atomic<bool> resetarmed{false};

	// The datapoint the display playhead sits on: the one most recently
	// played. Unlike 'row', which a reset moves at arm time, this only
	// changes when a TRIG actually plays, so the circle tracks what's
	// sounding rather than where the playhead is armed. -1 means nothing
	// is playing (before the first trigger, or after the playhead runs
	// past the end of the data) and hides the circle. Atomic because the
	// audio thread writes it and the DataViz widget reads it.
	std::atomic<int> playingrow{-1};

	// Set while the sequence is cued at the start but hasn't begun
	// playing: when data is (re)loaded, and after a manual reset. The
	// display then shows a hollow circle on the first datapoint instead
	// of a filled one. Atomic for the same reason as playingrow.
	std::atomic<bool> cued{true};

	// The datapoint queued by a left-click on the chart (issue #14): the
	// next TRIG plays this datapoint instead of stepping to the next one,
	// then sequential play continues from there. -1 means nothing is
	// queued. Set on the UI thread (DataViz click), read and cleared on the
	// audio thread. A queued click and an armed reset can't both win a
	// trigger: whichever happened last takes it (a reset clears the queue;
	// a queued click overrides an armed reset when the trigger fires).
	std::atomic<int> queuedrow{-1};

	// Style variables
	std::string main = "#173561";  // deep blue: line playhead and cued/queued rings
	std::string faded = "#805279"; // muted plum: the data line and its hover
	std::string white = "#FFFBE4"; // cream: on-display messages
	std::string cream = "#FFFDF1"; // cream used for the interactive knob/jack labels; the hover ring
	std::string salmon = "#FF7272"; // panel background colour

	// Save and retrieve menu choice(s), plus a copy of the loaded data
	// so the patch is portable (issue #18).
	json_t* dataToJson() override {
		if (csvloaded) {
			json_t* rootJ = json_object();
			json_object_set_new(rootJ, "default_path", json_string(currentpath.c_str()));
			json_object_set_new(rootJ, "default_column", json_integer(colnum));

			// Save the delimiter choice (issue #16) so a reopened patch
			// re-parses the file the same way. The custom character is
			// stored as a one-character string.
			json_object_set_new(rootJ, "delimiter_mode", json_integer(delimMode));
			char cbuf[2] = {customDelim, '\0'};
			json_object_set_new(rootJ, "custom_delimiter", json_string(cbuf));
			// Also save the column NAME, so that if the file changes on
			// disk we can tell whether the saved column still exists
			// instead of silently playing a different one.
			std::shared_ptr<const Dataset> ds = getDataset();
			if (colnum >= 0 && colnum < (int)ds->columns.size()) {
				json_object_set_new(rootJ, "default_column_name", json_string(ds->columns[colnum].c_str()));
			}

			// Save the column names and which of them hold numbers, so
			// the column menu still makes sense without the file
			json_t* columnsJ = json_array();
			json_t* colhasdataJ = json_array();
			for (size_t i = 0; i < ds->columns.size(); i++) {
				json_array_append_new(columnsJ, json_string(ds->columns[i].c_str()));
				json_array_append_new(colhasdataJ, json_boolean(ds->colhasdata[i]));
			}
			json_object_set_new(rootJ, "columns", columnsJ);
			json_object_set_new(rootJ, "colhasdata", colhasdataJ);

			// Embed the selected column's data, unless the dataset is
			// over the embedding cap (see EMBED_MAX_VALUES)
			if (colnum >= 0 && ds->length() > 0 && ds->length() <= EMBED_MAX_VALUES) {
				json_t* dataJ = json_array();
				for (float v : ds->data) {
					// JSON has no NaN, so missing values become nulls
					json_array_append_new(dataJ, std::isnan(v) ? json_null() : json_real(v));
				}
				json_object_set_new(rootJ, "data", dataJ);
			}
			return rootJ;
		} else {
			return json_object();
		}
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* default_colJ = json_object_get(rootJ, "default_column");
		json_t* default_colnameJ = json_object_get(rootJ, "default_column_name");
		json_t* default_pathJ = json_object_get(rootJ, "default_path");
		if (default_colJ) {
			colnum = json_integer_value(default_colJ);
		}
		if (default_colnameJ) {
			savedcolname = json_string_value(default_colnameJ);
		}

		// Restore the delimiter choice (issue #16) BEFORE parsing the file
		// below, so it's read with the right separator. Patches saved
		// before this feature have no delimiter fields and keep the
		// DELIM_AUTO default.
		json_t* delimModeJ = json_object_get(rootJ, "delimiter_mode");
		if (delimModeJ) {
			delimMode = json_integer_value(delimModeJ);
		}
		json_t* customDelimJ = json_object_get(rootJ, "custom_delimiter");
		if (customDelimJ) {
			const char* s = json_string_value(customDelimJ);
			if (s && s[0]) {
				customDelim = s[0];
			}
		}

		if (default_pathJ) {
			std::string p = json_string_value(default_pathJ);
			INFO("LOADING PATH: %s", p.c_str());
			currentpath = p;
			processCSV(currentpath, COLUMN_RESTORE);
			csvloaded = true;

			// If the file couldn't be read — moved, renamed, deleted, or
			// the patch came from someone else's computer — fall back to
			// the copy of the data embedded in the patch. When the file
			// IS readable it wins, so editing the CSV and reopening the
			// patch still picks up the edits.
			if (badcsv) {
				loadEmbeddedData(rootJ);
			}
		}
	}

	// Build a dataset from the data embedded in the patch, used when the
	// original CSV file can't be read. Leaves the invalid-CSV state
	// untouched if the patch has no embedded data (it predates embedding,
	// or the dataset was over the embedding cap).
	void loadEmbeddedData(json_t* rootJ)
	{
		json_t* dataJ = json_object_get(rootJ, "data");
		if (!dataJ || json_array_size(dataJ) == 0) {
			return;
		}

		auto ds = std::make_shared<Dataset>();

		// The embedded column data (nulls are missing values)
		size_t i;
		json_t* v;
		json_array_foreach(dataJ, i, v) {
			ds->data.push_back(json_is_number(v) ? (float)json_number_value(v) : NAN);
		}
		ds->calcMinMax();

		// The column names, so the menu still lists them (locked)
		json_t* columnsJ = json_object_get(rootJ, "columns");
		json_t* colhasdataJ = json_object_get(rootJ, "colhasdata");
		if (columnsJ) {
			json_array_foreach(columnsJ, i, v) {
				const char* name = json_string_value(v);
				ds->columns.push_back(name ? name : "");
				json_t* h = colhasdataJ ? json_array_get(colhasdataJ, i) : NULL;
				ds->colhasdata.push_back(h ? json_is_true(h) : false);
			}
		}
		if (ds->columns.empty()) {
			// Shouldn't happen, but keep the menu and display sane
			ds->columns.push_back(savedcolname.empty() ? "Embedded data" : savedcolname);
			ds->colhasdata.assign(1, true);
		}
		if (colnum < 0 || colnum >= (int)ds->columns.size()) {
			colnum = 0;
		}

		// Publish, same as the end of processCSV()
		row = -1;
		resetarmed = false;
		playingrow = -1;
		cued = true;
		queuedrow = -1;
		setDataset(ds);
		badcsv = false;
		embeddedonly = true;
		INFO("using embedded data: %i values", ds->length());
	}

	// Trigger for incoming gate detection
	dsp::SchmittTrigger ingate;
	dsp::SchmittTrigger resetgate;
	dsp::PulseGenerator gatePulse;
	dsp::PulseGenerator endPulse;

	// Set the three CV outputs to datapoint r of the dataset. The caller
	// checks that r is in range and not a NaN (missing) value.
	void setCVOutputs(const Dataset &ds, int r)
	{
		// Get v/oct min and max from the RANGE knob (see voctRange)
		float voctmin;
		float voctmax;
		voctRange(params[RANGE_PARAM].getValue(), voctmin, voctmax);

		// Set the voltages to the data
		outputs[MINUSFIVETOFIVE_OUTPUT].setVoltage(scalemap(ds.data[r], ds.datamin, ds.datamax, -5.f, 5.f));
		outputs[ZEROTOTEN_OUTPUT].setVoltage(scalemap(ds.data[r], ds.datamin, ds.datamax, 0.f, 10.f));
		outputs[VOCT_OUTPUT].setVoltage(scalemap(ds.data[r], ds.datamin, ds.datamax, voctmin, voctmax));
	}

	// On a loop
	void process(const ProcessArgs &args) override
	{
		// As long as it's not a bad CSV
		if (badcsv)
		{
			return;
		}

		// Grab the current dataset. If the UI swaps in a new one mid-call,
		// this call keeps working with the one it started with.
		std::shared_ptr<const Dataset> ds = getDataset();
		int len = ds->length();

		// A trigger at RESET arms the sequence: the playhead returns to
		// the first datapoint, but no gate fires and the CV outputs hold
		// their last values. The next TRIG then plays datapoint 0 in time
		// with the clock. With END -> RESET patched, the reset arms while
		// the last note is still sounding, so the loop stays gapless
		// (N datapoints = N clock ticks).
		if (resetgate.process(inputs[RESET_INPUT].getVoltage()))
		{
			row = 0;
			resetarmed = true;

			// A reset overrides any datapoint queued by clicking the chart:
			// the playhead is going back to the start, so the click is
			// dropped (the more recent action wins at trigger time).
			queuedrow = -1;

			// Manual resets and END -> RESET loop resets arrive as
			// identical triggers, but the display treats them
			// differently. A loop reset lands while our own END pulse is
			// still high (the cable adds only a sample of delay), so in
			// that case the filled circle stays on the last datapoint
			// while it plays out. Any other reset is manual: the circle
			// leaves the playing datapoint immediately and a hollow
			// "cued" circle appears on datapoint 0 instead.
			if (endPulse.remaining <= 0.f)
			{
				playingrow = -1;
				cued = true;
			}
		}

		// If a gate is high in the trigger input, play the next datapoint
		if (ingate.process(inputs[TRIG_INPUT].getVoltage()))
		{
			// Take and clear any datapoint queued by a chart click. A
			// queued point wins the trigger: the playhead jumps there and
			// plays it, then normal sequential play resumes from there. It
			// also cancels an armed reset (the click was the later action).
			int q = queuedrow.exchange(-1);
			if (q >= 0 && q < len)
			{
				row = q;
				resetarmed = false;
			}
			else if (resetarmed)
			{
				// A reset already moved the playhead to the first
				// datapoint; play that rather than stepping past it.
				resetarmed = false;
			}
			else
			{
				// Increment the row number
				row++;
			}

			int r = row;
			if (r >= 0 && r < len)
			{
				// This datapoint is now the one sounding, so the display
				// playhead moves here (as a filled circle, so the cued
				// state ends).
				playingrow = r;
				cued = false;

				// If it's not a NaN (missing) value, play it. Missing
				// data fires no gate and the CV outputs hold, so it's
				// audible as silence.
				if (!std::isnan(ds->data[r]))
				{
					setCVOutputs(*ds, r);
					gatePulse.trigger(params[LENGTH_PARAM].getValue());
				}

				// END fires as the last datapoint plays (end of cycle),
				// so END -> RESET re-arms the sequence in time for the
				// next clock tick to play datapoint 0.
				if (r == len - 1)
				{
					endPulse.trigger(0.01);
				}
			}
			else
			{
				// The playhead has run past the end (no reset patched):
				// nothing plays and the display playhead disappears.
				playingrow = -1;
				cued = false;
			}
		}

		// Activate end and gate pulses if triggered
		bool epulse = endPulse.process(1.0 / args.sampleRate);
		outputs[END_OUTPUT].setVoltage(epulse ? 10.0 : 0.0);
		bool gpulse = gatePulse.process(1.0 / args.sampleRate);
		outputs[GATE_OUTPUT].setVoltage(gpulse ? 10.0 : 0.0);
	};

	// Function to load a CSV file
	void loadCSV()
	{

		// Default directory
		std::string dir = asset::user("../");

		// Get a path from the user. Allow tab- and text-delimited files too,
		// not just .csv (issue #16): with a selectable delimiter, .tsv and
		// .txt data files are just as valid.
		char *pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, osdialog_filters_parse("Data files:csv,tsv,txt"));

		// If nothing gets chosen, don't do anything
		if (!pathC)
		{
			return;
		}

		// Otherwise save it to a variable
		std::string path = pathC;
		std::free(pathC);

		// Load it, auto-selecting the first numeric column
		processCSV(path, COLUMN_AUTO);
		csvloaded = true;
	}

	// Re-read the current CSV from disk (context menu), keeping the
	// selected column — matched by name, in case the file's columns have
	// been reordered by an edit. If the file can't be read this shows
	// the usual invalid-CSV state rather than silently keeping stale
	// data.
	void reloadCSV()
	{
		// Take the column name to restore from the live dataset — but
		// NOT in the invalid-CSV state, where the live dataset doesn't
		// reflect the file (it's whatever loaded before, or the default
		// data): overwriting savedcolname from it would clobber the
		// column name remembered from the patch.
		if (!badcsv)
		{
			std::shared_ptr<const Dataset> ds = getDataset();
			if (colnum >= 0 && colnum < (int)ds->columns.size())
			{
				savedcolname = ds->columns[colnum];
			}
		}
		processCSV(currentpath, COLUMN_RESTORE);
	}

	// The separator character to parse 'path' with, from the current
	// delimiter mode (issue #16). In auto mode it sniffs the file and
	// remembers the result so the menu can show it.
	char separatorForRead(const std::string &path)
	{
		switch (delimMode)
		{
		case DELIM_COMMA:
			return ',';
		case DELIM_TAB:
			return '\t';
		case DELIM_SEMICOLON:
			return ';';
		case DELIM_CUSTOM:
			return customDelim;
		case DELIM_AUTO:
		default:
			detectedDelim = sniffDelimiter(path);
			return detectedDelim;
		}
	}

	// Re-read the current file after the delimiter changes (issue #16). A
	// different separator gives entirely different columns, so there's no
	// sensible column to keep — this picks the first numeric one, like a
	// fresh load. If the new delimiter doesn't yield usable data the normal
	// invalid-CSV state shows, and the user can pick another delimiter.
	void reparseWithDelimiter()
	{
		if (csvloaded)
		{
			processCSV(currentpath, COLUMN_AUTO);
		}
	}

	// A short label for the current delimiter choice, shown next to the
	// "Delimiter" menu item (issue #16). Auto mode shows what it detected.
	std::string delimiterSummary()
	{
		switch (delimMode)
		{
		case DELIM_COMMA:
			return "comma";
		case DELIM_TAB:
			return "tab";
		case DELIM_SEMICOLON:
			return "semicolon";
		case DELIM_CUSTOM:
			return delimiterName(customDelim);
		case DELIM_AUTO:
		default:
			return "auto (" + delimiterName(detectedDelim) + ")";
		}
	}

	// Load a CSV file and select a column to sonify. 'request' is either a
	// column index (from the menu) or one of the COLUMN_ constants above.
	void processCSV(std::string path, int request)
	{
		INFO("Processing CSV: %s", path.c_str());

		// Remember the path straight away, even if parsing fails below. That
		// way changing the delimiter (or "Reload CSV from disk") can re-read
		// the same file after a failed parse — e.g. loading a colon-separated
		// file, which auto-detect misses, then fixing it with Custom ':'
		// instead of having to load the file all over again.
		currentpath = path;

		try {
			// Distinguish a missing file from an unparseable one, so the
			// display can tell the user which problem they have
			if (!system::isFile(path))
			{
				filemissing = true;
				throw std::runtime_error("file not found");
			}
			filemissing = false;

			// Pick the column separator (issue #16): a fixed character, the
			// custom one, or — in auto mode — sniffed from the file. rapidcsv
			// takes a single char as the separator.
			char sep = separatorForRead(path);

			// Setting values that aren't numbers to NaN (rather than throwing error)
			rapidcsv::Document doc(path,
								rapidcsv::LabelParams(),
							rapidcsv::SeparatorParams(sep),
							rapidcsv::ConverterParams(true /* pHasDefaultConverter */,
														NAN /* pDefaultFloat */,
														0 /* pDefaultInteger */));

			// Build the new dataset off to the side; nothing the audio
			// thread can see changes until setDataset() below.
			auto ds = std::make_shared<Dataset>();
			ds->columns = doc.GetColumnNames();

			// Files that aren't really CSVs (PDFs, executables) sometimes
			// "parse" into garbage instead of throwing. No columns, or
			// control characters in the header, means unusable data.
			if (ds->columns.empty())
			{
				throw std::runtime_error("no columns found");
			}
			for (const std::string &name : ds->columns)
			{
				for (char c : name)
				{
					if ((unsigned char)c < 0x20 && c != '\t')
					{
						throw std::runtime_error("header is not text");
					}
				}
			}

			// Work out which columns contain at least one number; only
			// those can be sonified, so only those are selectable.
			int ncols = (int)ds->columns.size();
			ds->colhasdata.assign(ncols, false);
			for (int i = 0; i < ncols; i++)
			{
				try
				{
					std::vector<float> values = doc.GetColumn<float>(ds->columns[i]);
					for (float v : values)
					{
						if (!std::isnan(v))
						{
							ds->colhasdata[i] = true;
							break;
						}
					}
				}
				catch (...)
				{
					// A column that can't even be read has no data
					ds->colhasdata[i] = false;
				}
			}

			// Decide which column to select
			int col = COLUMN_NONE;
			if (request >= 0)
			{
				// Direct choice from the menu (only numeric columns are
				// clickable, but double-check to be safe)
				if (request < ncols && ds->colhasdata[request])
				{
					col = request;
				}
			}
			else if (request == COLUMN_AUTO)
			{
				// New file: pick the first column that has numbers in it.
				// A file with no numeric columns at all can't be sonified,
				// so it's treated as invalid.
				for (int i = 0; i < ncols; i++)
				{
					if (ds->colhasdata[i])
					{
						col = i;
						break;
					}
				}
				if (col == COLUMN_NONE)
				{
					throw std::runtime_error("no numeric columns");
				}
			}
			else // COLUMN_RESTORE: a saved patch is being reopened
			{
				if (!savedcolname.empty())
				{
					// Find the saved column by name. If the file has
					// changed and it's gone (or lost its numbers), leave
					// nothing selected: the display prompts the user
					// rather than guessing a different column.
					for (int i = 0; i < ncols; i++)
					{
						if (ds->columns[i] == savedcolname && ds->colhasdata[i])
						{
							col = i;
							break;
						}
					}
				}
				else if (colnum >= 0 && colnum < ncols && ds->colhasdata[colnum])
				{
					// Patches saved before column names were stored only
					// have the position; use it if it's still usable.
					col = colnum;
				}
			}

			if (col != COLUMN_NONE)
			{
				ds->data = doc.GetColumn<float>(ds->columns[col]);
				ds->calcMinMax();
				INFO("data min: %f", ds->datamin);
				INFO("data max: %f", ds->datamax);
				INFO("data length: %i", ds->length());
			}
			else
			{
				INFO("no column selected");
			}

			// Publish: from here on the audio thread and UI see the new data.
			// (currentpath was already set at the top of this function.)
			colnum = col;
			row = -1; // because the first thing we do is increment it
			resetarmed = false; // fresh data starts unarmed
			playingrow = -1; // nothing is sounding until the first trigger
			cued = true; // show the hollow circle: cued at the start
			queuedrow = -1; // drop any pending click from the old data
			setDataset(ds);
			badcsv = false;
			embeddedonly = false; // this data came from a real file

		} catch (...) {
			badcsv = true;
			WARN("ERROR: CSV file could not be read.");
		}
 	}
};

// This is the dataviz display
struct DataViz : Widget
{
	LoudNumbers *module; // NEW

	const float margin = mm2px(2.0);

	// The datapoint the mouse is currently over (issue #14), or -1 when the
	// cursor is off the chart. Set by onHover/onLeave and read by drawLayer;
	// both run on the UI thread, so a plain int is safe here (unlike the
	// module-side state, which crosses to the audio thread as an atomic).
	int hoverrow = -1;

	// Map a mouse position (relative to this widget) to a datapoint index by
	// x only, ignoring y as long as the cursor is within the widget. Missing
	// (NaN) datapoints can't be selected — they have no position on the
	// chart — so this snaps to the nearest datapoint that has a value.
	// Returns -1 when there's no selectable data. Shared by hover and click
	// so they pick the same point.
	int datapointAt(Vec pos)
	{
		if (!module || module->badcsv || module->colnum < 0)
		{
			return -1;
		}
		std::shared_ptr<const Dataset> ds = module->getDataset();
		int len = ds->length();
		if (len < 1)
		{
			return -1;
		}
		float width = box.size.x - 2 * margin;
		float xdivisor = std::max(len - 1, 1);
		float stepx = width / xdivisor;
		int d = (int)std::round((pos.x - margin) / stepx);
		// Clamp to the data: hovering the margins snaps to the first/last
		// point rather than selecting nothing.
		if (d < 0) d = 0;
		if (d > len - 1) d = len - 1;

		// If that point is missing, walk outwards to the nearest valid one
		// (ties go to the left). A selectable column always has at least one
		// value, so this normally finds something; -1 is just a safety net.
		if (std::isnan(ds->data[d]))
		{
			for (int off = 1; off < len; off++)
			{
				if (d - off >= 0 && !std::isnan(ds->data[d - off]))
				{
					return d - off;
				}
				if (d + off < len && !std::isnan(ds->data[d + off]))
				{
					return d + off;
				}
			}
			return -1;
		}
		return d;
	}

	// Track the datapoint under the cursor for the hover highlight. Consume
	// the event so this widget stays the hover target and gets onLeave.
	void onHover(const HoverEvent &e) override
	{
		Widget::onHover(e);
		hoverrow = datapointAt(e.pos);
		e.consume(this);
	}

	// Cursor left the chart: stop drawing the hover highlight.
	void onLeave(const LeaveEvent &e) override
	{
		hoverrow = -1;
	}

	// A left-click queues the datapoint under the cursor to play on the next
	// trigger (issue #14). Only the left button is consumed, so right-click
	// still opens the module's context menu.
	void onButton(const ButtonEvent &e) override
	{
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
		{
			int d = datapointAt(e.pos);
			if (d >= 0)
			{
				module->queuedrow = d;
				e.consume(this);
				return;
			}
		}
		Widget::onButton(e);
	}

	void drawLayer(const DrawArgs &args, int layer) override
	{
		float width = box.size.x - 2 * margin;
		float height = box.size.y - 2 * margin;
		float circ_size = 1.f;
		// The API states that the module  should only write to layer 1.
		// And we don't want to run this until 'module' has actually been set.
		if (layer == 1 && module)
		{
			if (module->badcsv) {
				nvgFillColor(args.vg, color::fromHexString(module->white));
				nvgFontSize(args.vg, 14);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
				if (module->filemissing) {
					// The file has moved rather than being unreadable:
					// tell the user how to fix it
					nvgText(args.vg, width/2, height/2 - 16, "CSV file not found", NULL);
					nvgText(args.vg, width/2, height/2, "Right-click to reload", NULL);
					nvgText(args.vg, width/2, height/2 + 16, "or load a new file", NULL);
				} else {
					nvgText(args.vg, width/2, height/2, "Invalid CSV", NULL);
				}
			} else if (module->colnum < 0) {
				// A file is loaded but no column is selected (e.g. a saved
				// patch's column no longer exists in the file)
				nvgFillColor(args.vg, color::fromHexString(module->white));
				nvgFontSize(args.vg, 14);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
				nvgText(args.vg, width/2, height/2 - 8, "Right-click to select", NULL);
				nvgText(args.vg, width/2, height/2 + 8, "a column of data", NULL);
			} else {
				// Take a snapshot of the dataset for this draw call
				std::shared_ptr<const Dataset> ds = module->getDataset();
				int len = ds->length();

				// Avoid dividing by zero when placing a single datapoint
				float xdivisor = std::max(len - 1, 1);

				// Draw the line. Consecutive valid datapoints are joined
				// directly; missing (NaN) datapoints break the line with a
				// notch (issue #22): on each side of a gap the line reaches
				// 3/4 of a step past the last valid point, sloping towards
				// the next one, then stops. Since every valid point owns
				// 3/4 of a step of line on each side, an isolated point
				// between gaps still shows as a short fragment. A notch
				// that would be under a pixel wide (very dense data) isn't
				// visible anyway, so the line is drawn straight through
				// instead — but long gaps stay visible even in dense data,
				// because their notch is wide. Missing points before the
				// first or after the last valid point draw nothing.
				float stepx = width / xdivisor; // gap between datapoints
				float reach = 0.75f * stepx;	// how far stubs extend

				nvgBeginPath(args.vg);
				int prev = -1; // index of the previous valid datapoint
				float prevx = 0.f;
				float prevy = 0.f;

				for (int d = 0; d < len; d++)
				{
					if (std::isnan(ds->data[d])) {
						continue;
					}

					// Calculate x and y coords
					float x = margin + (d * stepx);
					// Y == zero at the TOP of the box.
					float y = (height - 3) - (scalemap(ds->data[d], ds->datamin, ds->datamax,
												0.f, height-6));

					if (prev < 0) {
						// First valid datapoint: start the line here
						nvgMoveTo(args.vg, x, y);
					} else if (d == prev + 1) {
						// Adjacent to the previous one: join directly
						nvgLineTo(args.vg, x, y);
					} else {
						// There's a gap of missing datapoints in between
						float notch = (x - prevx) - 2.f * reach;
						if (notch < 1.f) {
							// Sub-pixel notch: draw straight through
							nvgLineTo(args.vg, x, y);
						} else {
							// Stub out of the previous point, break, then
							// stub into this one (y interpolated along the
							// straight line between the two points)
							float t = reach / (x - prevx);
							nvgLineTo(args.vg, prevx + reach, prevy + (y - prevy) * t);
							nvgMoveTo(args.vg, x - reach, y - (y - prevy) * t);
							nvgLineTo(args.vg, x, y);
						}
					}

					prev = d;
					prevx = x;
					prevy = y;
				}

				nvgStrokeColor(args.vg, color::fromHexString(module->faded));
				nvgStrokeWidth(args.vg, mm2px(0.3));
				nvgStroke(args.vg);
				nvgClosePath(args.vg);

				// Draw the playhead circle. A hollow circle on the first
				// datapoint means the sequence is cued there but hasn't
				// begun (fresh data, or a manual reset); a filled circle
				// marks the datapoint that's currently sounding (not
				// 'row', which a reset moves before anything plays).
				// Missing (NaN) datapoints have no vertical position, so
				// no circle is drawn on them.
				//
				// A datapoint queued by clicking the chart (issue #14) is
				// shown with the same hollow "cued" circle, sitting on the
				// clicked point instead of datapoint 0 — it takes priority
				// so you can see what the next trigger will play.
				int queued = (int)module->queuedrow;
				bool hollow;
				int r;
				if (queued >= 0)
				{
					hollow = true;
					r = queued;
				}
				else
				{
					hollow = module->cued;
					r = hollow ? 0 : (int)module->playingrow;
				}
				if (r >= 0 && r < len && !std::isnan(ds->data[r]))
				{
					// Calculate x and y coords
					float x = margin + (r * width / xdivisor);
					// Y == zero at the TOP of the box.
					float y = (height - 3) - (scalemap(ds->data[r], ds->datamin, ds->datamax,
													0.f, height-6));
					nvgBeginPath(args.vg);
					nvgCircle(args.vg, x, y, mm2px(circ_size));
					if (hollow)
					{
						// Outline only, no fill, so the data line stays
						// visible through the ring. Marks a datapoint cued
						// to play next: datapoint 0 when the sequence is
						// cued at the start, or a datapoint queued by
						// clicking the chart (issue #14).
						nvgStrokeColor(args.vg, color::fromHexString(module->main));
						nvgStrokeWidth(args.vg, mm2px(0.3));
						nvgStroke(args.vg);
					}
					else
					{
						nvgFillColor(args.vg, color::fromHexString(module->main));
						nvgFill(args.vg);
					}
					nvgClosePath(args.vg);
				}

				// Draw the hover highlight (issue #14): a hollow cream ring
				// marks the datapoint the mouse is over (nearest by x,
				// ignoring y). Left-clicking queues that datapoint. It uses
				// the cream of the knob and jack labels, the module's other
				// interactive elements, so hovering reads as "you can act
				// here". datapointAt() only ever returns a datapoint that has
				// a value, so missing points are skipped rather than
				// highlighted.
				if (hoverrow >= 0 && hoverrow < len && !std::isnan(ds->data[hoverrow]))
				{
					float x = margin + (hoverrow * width / xdivisor);
					float y = (height - 3) - (scalemap(ds->data[hoverrow], ds->datamin, ds->datamax,
															 0.f, height - 6));
					nvgBeginPath(args.vg);
					nvgCircle(args.vg, x, y, mm2px(circ_size));
					nvgStrokeColor(args.vg, color::fromHexString(module->cream));
					nvgStrokeWidth(args.vg, mm2px(0.3));
					nvgStroke(args.vg);
					nvgClosePath(args.vg);
				}

			}
		} else {
			// Draw the line
				nvgBeginPath(args.vg);
				bool firstpoint = true;
				nvgMoveTo(args.vg, margin, height);

				for (int d = 0; d < defaultdatalength; d++)
				{
					// Calculate x and y coords
					float x = margin + (d * width / defaultdatalength);
					// Y == zero at the TOP of the box.
					float y = (height - 3) - (scalemap(defaultdata[d], defaultdatamin, defaultdatamax,
												0.f, height-6));
					if (firstpoint) {
						nvgMoveTo(args.vg, x, y);
						firstpoint = false;
					} else {
						nvgLineTo(args.vg, x, y);
					}

				}

				nvgStrokeColor(args.vg, color::fromHexString("#805279"));
				nvgStrokeWidth(args.vg, mm2px(0.3));
				nvgStroke(args.vg);
				nvgClosePath(args.vg);

		}
		Widget::drawLayer(args, layer);
	}
};

struct LoudNumbersWidget : ModuleWidget
{
	LoudNumbersWidget(LoudNumbers *module)
	{
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/LoudNumbers.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(56.03, 78.221)), module, LoudNumbers::RANGE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(71.36, 78.221)), module, LoudNumbers::LENGTH_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 78.221)), module, LoudNumbers::TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.34, 78.221)), module, LoudNumbers::RESET_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10, 96.195)), module, LoudNumbers::END_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(25.34, 96.195)), module, LoudNumbers::MINUSFIVETOFIVE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.69, 96.195)), module, LoudNumbers::ZEROTOTEN_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(56.03, 96.195)), module, LoudNumbers::VOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(71.36, 96.195)), module, LoudNumbers::GATE_OUTPUT));

		// Load and display dataviz widget
		DataViz *data_viz = createWidget<DataViz>(mm2px(Vec(6.736, 17.647)));
		data_viz->box.size = mm2px(Vec(67.832, 46.438));
		data_viz->module = module;
		addChild(data_viz);
	}

	struct ColumnMenuItem : MenuItem
	{
		LoudNumbers *module;
		int val;
		void onAction(const event::Action &e) override {
			if (module->csvloaded)
			{
				module->processCSV(module->currentpath, val);
			}
		}
		void step() override {
			rightText = (module->colnum == val) ? "✔" : "";
		}
	};

	// A radio row in the Delimiter submenu (issue #16). Choosing one sets
	// the mode and re-reads the file with the new separator.
	struct DelimiterModeItem : MenuItem
	{
		LoudNumbers *module;
		int mode;
		void onAction(const event::Action &e) override {
			module->delimMode = mode;
			module->reparseWithDelimiter();
		}
		void step() override {
			rightText = (module->delimMode == mode) ? "✔" : "";
			MenuItem::step();
		}
	};

	// A text box in the Delimiter submenu for typing a custom separator
	// character (issue #16). Pressing Enter applies the first character
	// typed, switches to custom mode, re-reads the file, and closes the
	// menu. rapidcsv only supports a single-character separator.
	struct DelimiterField : ui::TextField
	{
		LoudNumbers *module;
		DelimiterField()
		{
			box.size.x = 100;
			placeholder = "e.g. |";
		}
		void onSelectKey(const event::SelectKey &e) override
		{
			if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER))
			{
				if (!text.empty())
				{
					module->customDelim = text[0];
					module->delimMode = LoudNumbers::DELIM_CUSTOM;
					module->reparseWithDelimiter();
				}
				// Close the menu once the character is committed
				ui::MenuOverlay *overlay = getAncestorOfType<ui::MenuOverlay>();
				if (overlay)
				{
					overlay->requestDelete();
				}
				e.consume(this);
				return;
			}
			ui::TextField::onSelectKey(e);
		}
	};

	// Add CSV loading capabilities to the right click menu
	void appendContextMenu(Menu* menu) override
	{
		LoudNumbers* module = dynamic_cast<LoudNumbers*>(this->module);

		// Spacer
		menu->addChild(new MenuSeparator());

		// Load CSV
		menu->addChild(createMenuItem("Load CSV", "",
									  [=]()
									  {
										  module->loadCSV();
									  }));

		// Re-read the current file from disk (e.g. after editing it)
		if (module->csvloaded)
		{
			menu->addChild(createMenuItem("Reload CSV from disk", "",
										  [=]()
										  {
											  module->reloadCSV();
										  }));
		}

		// Delimiter submenu (issue #16). Only offered when a file is loaded
		// and readable — when playing patch-embedded data (file missing)
		// there's nothing to re-parse, so the choice is locked, like the
		// column list.
		if (module->csvloaded && !module->embeddedonly)
		{
			menu->addChild(createSubmenuItem("Delimiter", module->delimiterSummary(),
											 [=](Menu *sub)
											 {
												 // Detect automatically, showing what it found
												 DelimiterModeItem *autoItem = new DelimiterModeItem();
												 autoItem->module = module;
												 autoItem->mode = LoudNumbers::DELIM_AUTO;
												 autoItem->text = "Detect automatically (" + delimiterName(module->detectedDelim) + ")";
												 sub->addChild(autoItem);

												 // Fixed presets
												 const char *labels[] = {"Comma", "Tab", "Semicolon"};
												 const int modes[] = {LoudNumbers::DELIM_COMMA, LoudNumbers::DELIM_TAB, LoudNumbers::DELIM_SEMICOLON};
												 for (int i = 0; i < 3; i++)
												 {
													 DelimiterModeItem *it = new DelimiterModeItem();
													 it->module = module;
													 it->mode = modes[i];
													 it->text = labels[i];
													 sub->addChild(it);
												 }

												 // Custom, showing the current custom character
												 DelimiterModeItem *customItem = new DelimiterModeItem();
												 customItem->module = module;
												 customItem->mode = LoudNumbers::DELIM_CUSTOM;
												 customItem->text = "Custom (" + delimiterName(module->customDelim) + ")";
												 sub->addChild(customItem);

												 // Field to type a custom character
												 sub->addChild(new MenuSeparator());
												 sub->addChild(createMenuLabel("Custom character, then Enter:"));
												 DelimiterField *field = new DelimiterField();
												 field->module = module;
												 field->text = std::string(1, module->customDelim);
												 sub->addChild(field);
											 }));
		}

		// Spacer
		menu->addChild(new MenuSeparator());

		// Take a snapshot of the dataset for the column list
		std::shared_ptr<const Dataset> ds = module->getDataset();

		for (int i = 0; i < (int)ds->columns.size(); i++)
		{
			ColumnMenuItem *item = new ColumnMenuItem();
			item->text = ds->columns[i];
			item->val = i;
			item->module = module;
			// Columns with no numeric values can't be sonified, so grey
			// them out. When playing patch-embedded data (the CSV file
			// couldn't be read), only the selected column's data exists,
			// so the whole list is locked until a reload succeeds.
			item->disabled = !ds->colhasdata[i] || module->embeddedonly;
			menu->addChild(item);
		}
	}
};

Model *modelLoudNumbers = createModel<LoudNumbers, LoudNumbersWidget>("LoudNumbers");
