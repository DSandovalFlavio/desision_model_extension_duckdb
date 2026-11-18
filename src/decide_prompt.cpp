#include "decide_prompt.hpp"

#include <stdexcept>

namespace decide {

static const char *SYSTEM_PROMPT = "Apply the question to the state. Choose exactly one of the listed options. "
                                   "Respond with only its uppercase letter, with no explanation or reasoning.";
static const char *NOUL_TRUE = "the statement is true / the answer is yes";
static const char *NOUL_FALSE = "the statement is false / the answer is no";

//! Python's str.strip() whitespace set, as applied by the chat template's `|trim` to each message.
static std::string Trim(const std::string &s) {
	const char *ws = " \t\n\r\f\v";
	auto b = s.find_first_not_of(ws);
	if (b == std::string::npos) {
		return "";
	}
	auto e = s.find_last_not_of(ws);
	return s.substr(b, e - b + 1);
}

DecisionRequest Prompt::YesNo(std::string state, std::string instructions) {
	DecisionRequest r;
	r.state = std::move(state);
	r.instructions = std::move(instructions);
	r.labels = {"yes", "no"};
	r.descriptions = {NOUL_TRUE, NOUL_FALSE};
	return r;
}

std::string Prompt::RenderWithState(const DecisionRequest &req, const std::string &state) {
	const size_t n = req.labels.size();
	if (n < 2) {
		throw std::invalid_argument("a decision needs at least 2 options");
	}
	if (n > (size_t)MAX_OPTIONS) {
		throw std::invalid_argument("a decision supports at most 26 options");
	}
	std::string options, letters;
	for (size_t i = 0; i < n; i++) {
		const std::string &name = req.labels[i];
		const std::string desc = i < req.descriptions.size() ? Trim(req.descriptions[i]) : std::string();
		options += LETTERS[i];
		options += ". ";
		options += name;
		if (!desc.empty()) {
			options += ": " + desc;
		}
		if (i + 1 < n) {
			options += "\n";
			letters += LETTERS[i];
			letters += ", ";
		} else {
			letters += LETTERS[i];
		}
	}
	std::string user = "<state>\n" + state + "\n</state>\n\nQuestion: " + req.instructions + "\n\nOptions:\n" +
	                   options + "\n\nAnswer with one letter: " + letters + ".";
	return std::string("<|im_start|>system\n") + SYSTEM_PROMPT + "<|im_end|>\n<|im_start|>user\n" + Trim(user) +
	       "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
}

std::string Prompt::Render(const DecisionRequest &req) {
	return RenderWithState(req, req.state);
}

} // namespace decide
