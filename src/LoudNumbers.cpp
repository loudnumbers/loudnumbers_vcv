#include "plugin.hpp"
#include <vector>
#include <algorithm>
#include <iterator>
#include <atomic>
#include <memory>
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

	// Style variables
	std::string main = "#003380";
	std::string faded = "#805279";
	std::string white = "#FFFBE4";
	std::string salmon = "#FF7272"; // panel background, fills the hollow circle

	// Save and retrieve menu choice(s), plus a copy of the loaded data
	// so the patch is portable (issue #18).
	json_t* dataToJson() override {
		if (csvloaded) {
			json_t* rootJ = json_object();
			json_object_set_new(rootJ, "default_path", json_string(currentpath.c_str()));
			json_object_set_new(rootJ, "default_column", json_integer(colnum));
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
			if (resetarmed)
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

		// Get a path from the user
		char *pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, osdialog_filters_parse("Source:csv"));

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
		std::shared_ptr<const Dataset> ds = getDataset();
		if (colnum >= 0 && colnum < (int)ds->columns.size())
		{
			savedcolname = ds->columns[colnum];
		}
		processCSV(currentpath, COLUMN_RESTORE);
	}

	// Load a CSV file and select a column to sonify. 'request' is either a
	// column index (from the menu) or one of the COLUMN_ constants above.
	void processCSV(std::string path, int request)
	{
		INFO("Processing CSV: %s", path.c_str());

		try {
			// Setting values that aren't numbers to NaN (rather than throwing error)
			rapidcsv::Document doc(path,
								rapidcsv::LabelParams(),
							rapidcsv::SeparatorParams(),
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
			colnum = col;
			currentpath = path;
			row = -1; // because the first thing we do is increment it
			resetarmed = false; // fresh data starts unarmed
			playingrow = -1; // nothing is sounding until the first trigger
			cued = true; // show the hollow circle: cued at the start
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
				nvgText(args.vg, width/2, height/2, "Invalid CSV", NULL);
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
				bool hollow = module->cued;
				int r = hollow ? 0 : (int)module->playingrow;
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
						// Outline only: fill with the panel background so
						// the circle reads as an empty slot
						nvgFillColor(args.vg, color::fromHexString(module->salmon));
						nvgFill(args.vg);
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
