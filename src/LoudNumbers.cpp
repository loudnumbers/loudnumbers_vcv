#include "plugin.hpp"
#include <algorithm>
#include <vector>

// Data variables
std::vector<float> data{ 10.f,20.f,40.f,45.f,60.f,62.f,63.f,90.f };
float datamin = *std::min_element(data.begin(), data.end());
float datamax = *std::max_element(data.begin(), data.end());
int row = 0;
int datalength = static_cast<int>(data.size());

// This function scales a number from one range to another
float scalemap(float x, float inmin, float inmax, float outmin, float outmax) {
	return outmin + (outmax - outmin) * ((x - inmin) / (inmax - inmin));
};

struct LoudNumbers : Module {
	enum ParamId {
		RANGE_PARAM,
		LENGTH_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		TRIG_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		MINUSFIVETOFIVE_OUTPUT,
		ZEROTOTEN_OUTPUT,
		VOCT_OUTPUT,
		GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	LoudNumbers() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(RANGE_PARAM, 1, 4, 2, "Octave range", " octaves");
		getParamQuantity(RANGE_PARAM)->snapEnabled = true;
		configParam(LENGTH_PARAM, 0.001f, 1.f, 0.1f, "Gate length", " s");
		configInput(TRIG_INPUT, "Trigger");
		configInput(RESET_INPUT, "Reset");
		configOutput(MINUSFIVETOFIVE_OUTPUT, "-5V to 5V");
		configOutput(ZEROTOTEN_OUTPUT, "0 to 10V");
		configOutput(VOCT_OUTPUT, "Volts per octave");
		configOutput(GATE_OUTPUT, "Gate");
	}

	// Variables to track whether gate out is happening
	bool outgateon = false;
	bool ingateon = false;
	bool firstrun = true;
	float wait = 0.f;

	// Trigger for incoming gate detection
	dsp::SchmittTrigger ingate;

	void process(const ProcessArgs& args) override {

		// Logging some info about the data on first run.
		if (firstrun) {
			INFO("data min %f", datamin);
			INFO("data max %f", datamax);
			INFO("data length %i", datalength);
			firstrun = false;
		}

		// if output gate is on
		if (outgateon) {
			// if wait time has elapsed
			if (wait > params[LENGTH_PARAM].getValue()) {
				// turn off the output gate
				outputs[GATE_OUTPUT].setVoltage(0.f);
				outgateon = false;
			} else {
				// If not, append to the wait variable
				wait += args.sampleTime;
			};
		} else { // If the gate isn't on
			
			// and if a gate is high in the trigger input
			if (ingate.process(inputs[TRIG_INPUT].getVoltage())) {
			//if (inputs[TRIG_INPUT].getVoltage() > 0) {

				// Set the voltages to the data
				outputs[MINUSFIVETOFIVE_OUTPUT].setVoltage(scalemap(data[row], datamin, datamax, -5.f, 5.f));
				outputs[ZEROTOTEN_OUTPUT].setVoltage(scalemap(data[row], datamin, datamax, 0.f, 10.f));
				outputs[VOCT_OUTPUT].setVoltage(scalemap(data[row], datamin, datamax, 0.f, params[RANGE_PARAM].getValue()));

				// Logging for info
				//INFO("row %d", row);
				//INFO("datapoint %f", data[row]);
				//INFO("minusfivetofive %f", scalemap(data[row], datamin, datamax, -5.f, 5.f));
				//INFO("minuszerototen %f", scalemap(data[row], datamin, datamax, 0.f, 10.f));
				//INFO("voct %f", scalemap(data[row], datamin, datamax, 0.f, params[RANGE_PARAM].getValue()));

				// Turn the gate on and reset the wait time
				outputs[GATE_OUTPUT].setVoltage(10.f);
				outgateon = true;
				wait = 0.f;

				// Increment the row number
				row++;

			// Check if row has hit max and loop if so
			if (row == datalength) {row = 0;};
			}
		}

		// If a reset is received, reset the row count
		if (inputs[RESET_INPUT].getVoltage() > 0) {row = 0;};
	};
};


// This is the dataviz display
struct DataViz : Widget {

	//box.size = mm2px(Vec(20.952, 15.594));
	const float margin = mm2px(2.0);
	float width = box.size.x - 2 * margin;
	float height = box.size.y - 2 * margin;

	void drawLayer(const DrawArgs& args, int layer) override {

		// No idea what this does
		if (layer != 1)
			return;

		// Loop over the datapoints
		for (int d = 0; d < datalength; d++) {

			// Calculate x and y coords
			float x = d * width/datalength;
			float y = scalemap(data[d], datamin, datamax, 0.f, height);

			// Draw a circle for each
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, x, y, mm2px(4.0 / 2));
			if (d == row) {
				nvgFillColor(args.vg, color::alpha(color::WHITE, 1.f));
			} else {
				nvgFillColor(args.vg, color::alpha(color::YELLOW, 1.f));
			}
			nvgFill(args.vg);
		}
	}
};

struct LoudNumbersWidget : ModuleWidget {
	LoudNumbersWidget(LoudNumbers* module) {
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

		// mm2px(Vec(10.17, 10.174))
		addChild(createWidget<Widget>(mm2px(Vec(6.939, 15.594))));
		// mm2px(Vec(53.817, 10.174))
		addChild(createWidget<DataViz>(mm2px(Vec(20.952, 15.594))));
		// mm2px(Vec(67.83, 34.271))
		addChild(createWidget<Widget>(mm2px(Vec(6.736, 29.813))));
	}
};


Model* modelLoudNumbers = createModel<LoudNumbers, LoudNumbersWidget>("LoudNumbers");