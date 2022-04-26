#include "plugin.hpp"
#include <algorithm>
#include <vector>
#include <osdialog.h>

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
	std::vector<std::string> columns{"Year", "Size", "Length", "Weight"};
	std::vector<float> data{0.f, 10.f, 20.f, 40.f, 45.f, 60.f, 62.f, 63.f, 10.f, 90.f, 100.f, 0.f, 10.f, 20.f, 40.f, 45.f, 60.f, 62.f, 63.f, 10.f, 90.f, 100.f};
	float datamin = *std::min_element(data.begin(), data.end());
	float datamax = *std::max_element(data.begin(), data.end());
	int row = -1; // because the first thing we do is increment it
	int datalength = static_cast<int>(data.size());

	// Style variables
	std::string main = "#003380";
	std::string faded = "#805279";

	// Variables to track whether gate out is happening
	bool outgateon = false;
	bool ingateon = false;
	bool firstrun = true;
	float wait = 0.f;

	// Trigger for incoming gate detection
	dsp::SchmittTrigger ingate;

	void process(const ProcessArgs &args) override
	{

		// Logging some info about the data on first run.
		if (firstrun)
		{
			INFO("data min %f", datamin);
			INFO("data max %f", datamax);
			INFO("data length %i", datalength);
			firstrun = false;
		}

		// if output gate is on - CURRENTLY STUCK ON
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
	};

	// Add CSV loading capabilities to the right click menu
	void appendContextMenu(Menu *menu)
	{
		// Spacer
		menu->addChild(new MenuSeparator);

		// Load CSV
		menu->addChild(createMenuItem("Load CSV", "",
									  [=]()
									  { loadCSV(); }));
	}

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
	}

	// Do some stuff with the CSV
	void processCSV(std::string path)
	{
		INFO("test");
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

			// Draw the line
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 2 * margin, height);

			for (int d = 0; d < module->datalength; d++)
			{

				// Calculate x and y coords
				float x = 2 * margin + (d * width / module->datalength);
				// Y == zero at the TOP of the box.
				float y = height - (scalemap(module->data[d], module->datamin, module->datamax,
											 0.f, height));

				nvgLineTo(args.vg, x, y);
			}

			nvgStrokeColor(args.vg, color::fromHexString(module->faded));
			nvgStrokeWidth(args.vg, mm2px(0.3));
			nvgStroke(args.vg);
			nvgClosePath(args.vg);

			// Draw the circles
			for (int d = 0; d < module->datalength; d++)
			{

				// Calculate x and y coords
				float x = 2 * margin + (d * width / module->datalength);
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
		Widget::drawLayer(args, layer);
	}
};

struct ColumnSelector : Widget
{
	LoudNumbers *module; // NEW
	void drawLayer(const DrawArgs &args, int layer) override
	{
		float width = box.size.x;
		float height = box.size.y;
		if (layer == 1 && module)
		{
			// Draw stuff
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
		DataViz *data_viz = createWidget<DataViz>(mm2px(Vec(6.736, 29.813)));
		data_viz->box.size = mm2px(Vec(67.832, 34.272));
		data_viz->module = module;
		addChild(data_viz);

		// Load and display column selector menu
		ColumnSelector *column = createWidget<ColumnSelector>(mm2px(Vec(6.736, 15.594))); // Position
		column->box.size = mm2px(Vec(67.832, 10.175));									  // Size
		column->module = module;
		addChild(column);
	}
};

Model *modelLoudNumbers = createModel<LoudNumbers, LoudNumbersWidget>("LoudNumbers");
