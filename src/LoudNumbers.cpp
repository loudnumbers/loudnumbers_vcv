#include "plugin.hpp"
#include <vector>
#include <algorithm>
#include <iterator>
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
	return outmin + (outmax - outmin) * ((x - inmin) / (inmax - inmin));
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
		configParam(RANGE_PARAM, 1, 8, 2, "Octave range", " octaves");
		getParamQuantity(RANGE_PARAM)->snapEnabled = true;
		configParam(LENGTH_PARAM, 0.001f, 1.f, 0.1f, "Gate length", " s");
		configInput(TRIG_INPUT, "Trigger");
		configInput(RESET_INPUT, "Reset");
		configOutput(END_OUTPUT, "End of Data Trigger");
		configOutput(MINUSFIVETOFIVE_OUTPUT, "-5V to 5V");
		configOutput(ZEROTOTEN_OUTPUT, "0 to 10V");
		configOutput(VOCT_OUTPUT, "Volts per octave");
		configOutput(GATE_OUTPUT, "Gate");
	}

	// Data variables
	std::string currentpath = "none";
	std::vector<std::string> columns{"Temps 1956-2019"};
	std::vector<float> data{-0.267,-0.007,0.046,0.017,-0.049,0.038,0.014,0.048,-0.223,-0.14,-0.068,-0.074,-0.113,0.032,-0.027,-0.186,-0.065,0.062,-0.214,-0.149,-0.241,0.047,-0.062,0.057,0.092,0.14,0.011,0.194,-0.014,-0.03,0.045,0.192,0.198,0.118,0.296,0.254,0.105,0.148,0.208,0.325,0.183,0.39,0.539,0.306,0.294,0.441,0.496,0.505,0.447,0.545,0.506,0.491,0.395,0.506,0.56,0.425,0.47,0.514,0.579,0.763,0.797,0.677,0.597,0.736};
	
	// Copy data to a new minmax vector
	std::vector<float> minmax_data = data;

	// Calculate minmax from the stripped vector	
	float datamin = *std::min_element(minmax_data.begin(), minmax_data.end());
	float datamax = *std::max_element(minmax_data.begin(), minmax_data.end());

	int row = -1; // because the first thing we do is increment it
	int datalength = static_cast<int>(data.size());
	int columnslength = static_cast<int>(columns.size());
	int colnum = 0;
	bool csvloaded = false;
	bool badcsv = false;

	// Style variables
	std::string main = "#003380";
	std::string faded = "#805279";
	std::string white = "#FFFBE4";

	// Variables to track what's happening
	bool firstrun = true;
	bool rowadvanced = false;

	// Save and retrieve menu choice(s).
	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "default_path", json_string(currentpath.c_str()));
		json_object_set_new(rootJ, "default_column", json_integer(colnum));
		return rootJ;
	}
	
	void dataFromJson(json_t* rootJ) override {
		json_t* default_colJ = json_object_get(rootJ, "default_column");
		json_t* default_pathJ = json_object_get(rootJ, "default_path");
		if (default_colJ) {
			colnum = json_integer_value(default_colJ);
		}
		if (default_pathJ) {
			std::string p = json_string_value(default_pathJ);
			INFO("LOADING PATH: %s", p.c_str());
			currentpath = p;
			processCSV(currentpath);
			csvloaded = true;
		}
		
	}

	// Trigger for incoming gate detection
	dsp::SchmittTrigger ingate;
	dsp::SchmittTrigger resetgate;
	dsp::PulseGenerator gatePulse;
	dsp::PulseGenerator endPulse;

	// On a loop
	void process(const ProcessArgs &args) override
	{	
		// As long as it's not a bad CSV
		if (!badcsv) {

			// Log some info about the data on first run.
			if (firstrun)
			{
				INFO("data min: %f", datamin);
				INFO("data max: %f", datamax);
				INFO("data length: %i", datalength);
				firstrun = false;
			}
			
			// If a gate is high in the trigger input, advance the row and set rowadvanced flag
			if (ingate.process(inputs[TRIG_INPUT].getVoltage()))
			{	
				// Increment the row number
				row++;

				// Check if row has hit max and trigger an end pulse if so
				if (row >= datalength)
				{
					endPulse.trigger(0.01);
				}
				
				// Get ready to play a note
				rowadvanced = true;
			}

			// Activate end pulse if triggered
			bool epulse = endPulse.process(1.0 / args.sampleRate);
			outputs[END_OUTPUT].setVoltage(epulse ? 10.0 : 0.0);

			// If a reset gate is received, set reset flag
			if (resetgate.process(inputs[RESET_INPUT].getVoltage()))
			{
				row = 0;

				// Calculate v/oct min and max
				float voctmin;
				float voctmax;

				if (params[RANGE_PARAM].getValue() < 4)
				{
					voctmin = 0;
					voctmax = params[RANGE_PARAM].getValue();
				}
				else
				{
					voctmin = 4 - params[RANGE_PARAM].getValue();
					voctmax = 4;
				}

				// Reset the outputs to the first datapoint if it's a number
				if (!std::isnan(data[0])) {
					outputs[MINUSFIVETOFIVE_OUTPUT].setVoltage(scalemap(data[0], datamin, datamax, -5.f, 5.f));
					outputs[ZEROTOTEN_OUTPUT].setVoltage(scalemap(data[0], datamin, datamax, 0.f, 10.f));
					outputs[VOCT_OUTPUT].setVoltage(scalemap(data[0], datamin, datamax, voctmin, voctmax));
				} else { // If not, reset to 0.
					outputs[MINUSFIVETOFIVE_OUTPUT].setVoltage(0.f);
					outputs[ZEROTOTEN_OUTPUT].setVoltage(0.f);
					outputs[VOCT_OUTPUT].setVoltage(0.f);
				}
			} 

			// If rowadvanced flag is set
			if (rowadvanced && row < datalength) 
			{
				rowadvanced = false;

					INFO("Reached end of data");
				// Get v/oct min and max
				float voctmin;
				float voctmax;

				if (params[RANGE_PARAM].getValue() < 4)
				{
					voctmin = 0;
					voctmax = params[RANGE_PARAM].getValue();
				}
				else
				{
					voctmin = 4 - params[RANGE_PARAM].getValue();
					voctmax = 4;
				}

				// If it's not a NaN value and it's within the range of the data
				if (!std::isnan(data[row]) || row >= datalength) {
					// Set the voltages to the data
					outputs[MINUSFIVETOFIVE_OUTPUT].setVoltage(scalemap(data[row], datamin, datamax, -5.f, 5.f));
					outputs[ZEROTOTEN_OUTPUT].setVoltage(scalemap(data[row], datamin, datamax, 0.f, 10.f));
					outputs[VOCT_OUTPUT].setVoltage(scalemap(data[row], datamin, datamax, voctmin, voctmax));
					gatePulse.trigger(params[LENGTH_PARAM].getValue());
				}
			}

			// Activate gate pulse if triggered
			bool gpulse = gatePulse.process(1.0 / args.sampleRate);
			outputs[GATE_OUTPUT].setVoltage(gpulse ? 10.0 : 0.0);

		}
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

		// Reset column number to 0
		colnum = 0;

		// Then do what you want with the path.
		processCSV(path);
		csvloaded = true;
	}

	// Do some stuff with the CSV
	void processCSV(std::string path)
	{
		INFO("Processing CSV");
		
		try {
			// Setting values that aren't numbers to NaN (rather than throwing error)
			rapidcsv::Document doc(path, 
								rapidcsv::LabelParams(),
							rapidcsv::SeparatorParams(),
							rapidcsv::ConverterParams(true /* pHasDefaultConverter */,
														NAN /* pDefaultFloat */,
														NAN /* pDefaultInteger */));
			columns = doc.GetColumnNames();
			data = doc.GetColumn<float>(columns[colnum]);

			// Copy data to a new minmax vector
			std::vector<float> minmax_data(data);

			// Strip NaN values from it
			minmax_data.erase(std::remove_if(std::begin(minmax_data), std::end(minmax_data),[](const float& value) { return std::isnan(value); }),std::end(minmax_data));

			// Calculate minmax from the stripped vector	
			datamin = *std::min_element(minmax_data.begin(), minmax_data.end());
			datamax = *std::max_element(minmax_data.begin(), minmax_data.end());

			firstrun = true;
			row = -1; // because the first thing we do is increment it
			datalength = static_cast<int>(data.size());
			columnslength = static_cast<int>(columns.size());
			if (currentpath != path) {
				colnum = 0;
				currentpath = path;
			}
			badcsv = false;

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
			} else {
				// Draw the line
				nvgBeginPath(args.vg);
				bool firstpoint = true;
				nvgMoveTo(args.vg, margin, height);

				for (int d = 0; d < module->datalength; d++)
				{
					if (!std::isnan(module->data[d])) {
						// Calculate x and y coords
						float x = margin + (d * width / (module->datalength - 1));
						// Y == zero at the TOP of the box.
						float y = (height - 3) - (scalemap(module->data[d], module->datamin, module->datamax,
													0.f, height-6));

						if (firstpoint) {
							nvgMoveTo(args.vg, x, y);
							// Logging to try to debug missing visualizations
							//INFO("data %f", module->data[d]);
							//INFO("y0 %f", y);
							firstpoint = false;
						} else {
							nvgLineTo(args.vg, x, y);
						}
					}
					
				}

				nvgStrokeColor(args.vg, color::fromHexString(module->faded));
				nvgStrokeWidth(args.vg, mm2px(0.3));
				nvgStroke(args.vg);
				nvgClosePath(args.vg);

				// Draw the circle
				for (int d = 0; d < module->datalength; d++)
				{

					// Calculate x and y coords
					float x = margin + (d * width / (module->datalength - 1));
					// Y == zero at the TOP of the box.
					float y = (height - 3) - (scalemap(module->data[d], module->datamin, module->datamax,
													0.f, height-6));

					// Draw a circle for each
					nvgBeginPath(args.vg);

					if (d == module->row)
					{
						nvgCircle(args.vg, x, y, mm2px(circ_size));
						nvgFillColor(args.vg, color::fromHexString(module->main));
					}

					nvgFill(args.vg);
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

				// Draw the circles
				for (int d = 0; d < defaultdatalength; d++)
				{

					// Calculate x and y coords
					float x = margin + (d * width / defaultdatalength);
					// Y == zero at the TOP of the box.
					float y = (height - 3) - (scalemap(defaultdata[d], defaultdatamin, defaultdatamax,
												0.f, height-6));

					// Draw a circle for each
					nvgBeginPath(args.vg);
					nvgCircle(args.vg, x, y, mm2px(circ_size));
					nvgFillColor(args.vg, color::fromHexString("#805279"));
					nvgFill(args.vg);
					nvgClosePath(args.vg);
				}
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
				module->colnum = val;
				module->processCSV(module->currentpath);
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

		// Spacer
		menu->addChild(new MenuSeparator());

		for (int i = 0; i < module->columnslength; i++) 
		{
			ColumnMenuItem *item = new ColumnMenuItem();
			item->text = module->columns[i];
			item->val = i;
			item->module = module;
			menu->addChild(item);
		}
	}
};

Model *modelLoudNumbers = createModel<LoudNumbers, LoudNumbersWidget>("LoudNumbers");
