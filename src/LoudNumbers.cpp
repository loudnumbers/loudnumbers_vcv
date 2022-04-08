#include "plugin.hpp"


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
		configParam(RANGE_PARAM, 0.f, 1.f, 0.f, "");
		configParam(LENGTH_PARAM, 0.f, 1.f, 0.f, "");
		configInput(TRIG_INPUT, "");
		configInput(RESET_INPUT, "");
		configOutput(MINUSFIVETOFIVE_OUTPUT, "");
		configOutput(ZEROTOTEN_OUTPUT, "");
		configOutput(VOCT_OUTPUT, "");
		configOutput(GATE_OUTPUT, "");
	}

	void process(const ProcessArgs& args) override {
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
		addChild(createWidget<Widget>(mm2px(Vec(20.952, 15.594))));
		// mm2px(Vec(67.83, 34.271))
		addChild(createWidget<Widget>(mm2px(Vec(6.736, 29.813))));
	}
};


Model* modelLoudNumbers = createModel<LoudNumbers, LoudNumbersWidget>("LoudNumbers");