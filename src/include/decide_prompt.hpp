#pragma once

#include <string>
#include <vector>

namespace decide {

//! One decision to make: a state (the text to classify), a question and its options.
//! For yes/no questions the options are {"yes", "no"} with the qev default descriptions.
struct DecisionRequest {
	std::string state;
	std::string instructions;
	std::vector<std::string> labels;       // option names, in order (slot A, B, C...)
	std::vector<std::string> descriptions; // same length as labels; "" = name only
};

//! Prompt format "qev-labels-v2" (OneJev1.6, Apache-2.0), rendered through the Qwen3.5 chat template with
//! thinking disabled. Must stay byte-identical to eval/run_eval.py (state_first layout) and to
//! llama-server /apply-template, see test/sql/decide_prompt.test.
class Prompt {
public:
	static constexpr const char *VERSION = "qev-labels-v2";
	static constexpr const char *LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static constexpr int MAX_OPTIONS = 26;

	static DecisionRequest YesNo(std::string state, std::string instructions);

	//! Chat-rendered prompt. The model answers with the letter of an option at the next position.
	static std::string Render(const DecisionRequest &req);
	//! Same, with the state replaced by a placeholder; used to size the fixed part of the prompt.
	static std::string RenderWithState(const DecisionRequest &req, const std::string &state);
};

} // namespace decide
