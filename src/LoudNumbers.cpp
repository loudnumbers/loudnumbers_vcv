#include "plugin.hpp"
#include <algorithm>
#include <vector>
#include <osdialog.h>
#include "rapidcsv.h" //https://github.com/d99kris/rapidcsv

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
		configOutput(MINUSFIVETOFIVE_OUTPUT, "-5V to 5V");
		configOutput(ZEROTOTEN_OUTPUT, "0 to 10V");
		configOutput(VOCT_OUTPUT, "Volts per octave");
		configOutput(GATE_OUTPUT, "Gate");
	}

	// Data variables
	std::string currentpath = "none";
	std::vector<std::string> columns{"Temps 1956-2019"};
	std::vector<float> data{-0.267,-0.007,0.046,0.017,-0.049,0.038,0.014,0.048,-0.223,-0.14,-0.068,-0.074,-0.113,0.032,-0.027,-0.186,-0.065,0.062,-0.214,-0.149,-0.241,0.047,-0.062,0.057,0.092,0.14,0.011,0.194,-0.014,-0.03,0.045,0.192,0.198,0.118,0.296,0.254,0.105,0.148,0.208,0.325,0.183,0.39,0.539,0.306,0.294,0.441,0.496,0.505,0.447,0.545,0.506,0.491,0.395,0.506,0.56,0.425,0.47,0.514,0.579,0.763,0.797,0.677,0.597,0.736};
	float datamin = *std::min_element(data.begin(), data.end());
	float datamax = *std::max_element(data.begin(), data.end());
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

	// Variables to track whether gate out is happening
	bool outgateon = false;
	bool ingateon = false;
	bool firstrun = true;
	float wait = 0.f;

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

	void process(const ProcessArgs &args) override
	{
		if (!badcsv) {
			// Logging some info about the data on first run.
			if (firstrun)
			{
				INFO("data min %f", datamin);
				INFO("data max %f", datamax);
				INFO("data length %i", datalength);
				firstrun = false;
			}

			// if output gate is on
			if (outgateon)
			{
				// if wait time has elapsed
				if (wait > params[LENGTH_PARAM].getValue())
				{
					// turn off the output gate
					outputs[GATE_OUTPUT].setVoltage(0.f);
					outgateon = false;
				}
				else
				{
					// If not, append to the wait variable
					wait += args.sampleTime;
				};
			}
			else
			{ // If the gate isn't on

				// and if a gate is high in the trigger input
				if (ingate.process(inputs[TRIG_INPUT].getVoltage()))
				{

					// Increment the row number
					row++;

					// Check if row has hit max and loop if so
					if (row == datalength)
					{
						row = 0;
					}

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

					// Set the voltages to the data
					outputs[MINUSFIVETOFIVE_OUTPUT].setVoltage(scalemap(data[row], datamin, datamax, -5.f, 5.f));
					outputs[ZEROTOTEN_OUTPUT].setVoltage(scalemap(data[row], datamin, datamax, 0.f, 10.f));
					outputs[VOCT_OUTPUT].setVoltage(scalemap(data[row], datamin, datamax, voctmin, voctmax));

					// Logging for info
					// INFO("row %d", row);
					// INFO("datapoint %f", data[row]);

					// Turn the gate on and reset the wait time
					outputs[GATE_OUTPUT].setVoltage(10.f);
					outgateon = true;
					wait = 0.f;
				}
			}

			// If a reset is received, reset the row count
			if (inputs[RESET_INPUT].getVoltage() > 0)
			{
				row = -1;
			};
		}
	};

	// Function to load a CSV file
	void loadCSV()
	{

		// Default directory
		std::string dir = asset::user("../");

		// Get a path from the user
		char *pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);

		// If nothing gets chosen, don't do anything
		if (!pathC)
		{
			return;
		}

		// Otherwise save it to a variable
		std::string path = pathC;
		std::free(pathC);

		// Then do what you want with the path.
		processCSV(path);
		csvloaded = true;
	}

	// Do some stuff with the CSV
	void processCSV(std::string path)
	{
		INFO("Processing CSV");
		
		try {
			// Setting values that aren't numbers to 0 (rather than throwing error)
			rapidcsv::Document doc(path, 
								rapidcsv::LabelParams(),
							rapidcsv::SeparatorParams(),
							rapidcsv::ConverterParams(true /* pHasDefaultConverter */,
														0.0 /* pDefaultFloat */,
														1 /* pDefaultInteger */));
			columns = doc.GetColumnNames();
			data = doc.GetColumn<float>(columns[colnum]);
			datamin = *std::min_element(data.begin(), data.end());
			datamax = *std::max_element(data.begin(), data.end());
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
		float circ_size = 0.5;
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
					// Calculate x and y coords
					float x = margin + (d * width / module->datalength);
					// Y == zero at the TOP of the box.
					float y = height - (scalemap(module->data[d], module->datamin, module->datamax,
												0.f, height));

					if (firstpoint) {
						nvgMoveTo(args.vg, x, y);
						firstpoint = false;
					} else {
						nvgLineTo(args.vg, x, y);
					}
					
				}

				nvgStrokeColor(args.vg, color::fromHexString(module->faded));
				nvgStrokeWidth(args.vg, mm2px(0.3));
				nvgStroke(args.vg);
				nvgClosePath(args.vg);

				// Draw the circles
				for (int d = 0; d < module->datalength; d++)
				{

					// Calculate x and y coords
					float x = margin + (d * width / module->datalength);
					// Y == zero at the TOP of the box.
					float y = height - (scalemap(module->data[d], module->datamin, module->datamax,
												0.f, height));

					// Draw a circle for each
					nvgBeginPath(args.vg);

					if (d == module->row)
					{
						nvgCircle(args.vg, x, y, mm2px(1.f));
						nvgFillColor(args.vg, color::fromHexString(module->main));
					}
					else if (d == 0 && module->row == module->datalength)
					{
						nvgCircle(args.vg, x, y, mm2px(1.f));
						nvgFillColor(args.vg, color::fromHexString(module->main));
					}
					else
					{
						nvgCircle(args.vg, x, y, mm2px(circ_size));
						nvgFillColor(args.vg, color::fromHexString(module->faded));
					}

					nvgFill(args.vg);
					nvgClosePath(args.vg);
				}

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

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.988, 78.221)), module, LoudNumbers::RANGE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(31.405, 78.221)), module, LoudNumbers::LENGTH_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.988, 96.195)), module, LoudNumbers::TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(31.405, 96.195)), module, LoudNumbers::RESET_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(49.97, 78.221)), module, LoudNumbers::MINUSFIVETOFIVE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(68.387, 78.221)), module, LoudNumbers::ZEROTOTEN_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(49.97, 96.195)), module, LoudNumbers::VOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(68.387, 96.195)), module, LoudNumbers::GATE_OUTPUT));

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
