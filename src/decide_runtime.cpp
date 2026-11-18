#include "decide_runtime.hpp"

#include "ggml-backend.h"
#include "llama.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>

#ifndef DECIDE_LLAMA_BUILD
#define DECIDE_LLAMA_BUILD "unknown"
#endif

namespace decide {

//! llama.cpp is chatty; its log only reaches stderr when DECIDE_VERBOSE is set.
static void QuietLog(ggml_log_level, const char *text, void *) {
	if (std::getenv("DECIDE_VERBOSE")) {
		fputs(text, stderr);
	}
}

Runtime::Runtime() {
	llama_log_set(QuietLog, nullptr);
	llama_backend_init();
}

Runtime::~Runtime() {
	Unload();
	for (auto &kv : vocab_models_) {
		llama_model_free(kv.second);
	}
	vocab_models_.clear();
	// llama_backend_free() is deliberately not called: the process may still be unloading other extensions.
}

Runtime &Runtime::Get() {
	static Runtime instance;
	return instance;
}

// -----------------------------------------------------------------------------------------------------------------
// Tokenizer (vocab only)
// -----------------------------------------------------------------------------------------------------------------

llama_model *Runtime::VocabModel(const std::string &path) {
	auto it = vocab_models_.find(path);
	if (it != vocab_models_.end()) {
		return it->second;
	}
	auto params = llama_model_default_params();
	params.vocab_only = true;
	params.n_gpu_layers = 0;
	llama_model *model = llama_model_load_from_file(path.c_str(), params);
	if (!model) {
		throw std::runtime_error("could not read the GGUF vocabulary from '" + path +
		                         "' (set DECIDE_VERBOSE=1 for llama.cpp logs)");
	}
	vocab_models_.emplace(path, model);
	return model;
}

std::vector<int32_t> Runtime::TokenizeWith(llama_model *model, const std::string &text) {
	const llama_vocab *vocab = llama_model_get_vocab(model);
	std::vector<llama_token> tokens(text.size() + 8);
	int32_t n = llama_tokenize(vocab, text.data(), (int32_t)text.size(), tokens.data(), (int32_t)tokens.size(),
	                           /*add_special*/ false, /*parse_special*/ true);
	if (n < 0) {
		tokens.resize(-n);
		n = llama_tokenize(vocab, text.data(), (int32_t)text.size(), tokens.data(), (int32_t)tokens.size(), false,
		                   true);
	}
	if (n < 0) {
		throw std::runtime_error("tokenization failed");
	}
	tokens.resize(n);
	return std::vector<int32_t>(tokens.begin(), tokens.end());
}

std::vector<int32_t> Runtime::Tokenize(const std::string &path, const std::string &text, bool add_special,
                                       bool parse_special) {
	std::lock_guard<std::mutex> guard(vocab_lock_);
	const llama_vocab *vocab = llama_model_get_vocab(VocabModel(path));
	std::vector<llama_token> tokens(text.size() + 8);
	int32_t n = llama_tokenize(vocab, text.data(), (int32_t)text.size(), tokens.data(), (int32_t)tokens.size(),
	                           add_special, parse_special);
	if (n < 0) {
		tokens.resize(-n);
		n = llama_tokenize(vocab, text.data(), (int32_t)text.size(), tokens.data(), (int32_t)tokens.size(),
		                   add_special, parse_special);
	}
	if (n < 0) {
		throw std::runtime_error("tokenization failed");
	}
	tokens.resize(n);
	return std::vector<int32_t>(tokens.begin(), tokens.end());
}

bool Runtime::IsLoaded(const std::string &path) {
	{
		std::lock_guard<std::mutex> guard(engine_lock_);
		if (model_ && loaded_config_.path == path) {
			return true;
		}
	}
	std::lock_guard<std::mutex> guard(vocab_lock_);
	return vocab_models_.count(path) > 0;
}

// -----------------------------------------------------------------------------------------------------------------
// Decision engine
// -----------------------------------------------------------------------------------------------------------------

bool Runtime::HasGpu() {
	Get();
	return llama_supports_gpu_offload();
}

void Runtime::Unload() {
	if (ctx_) {
		llama_free(ctx_);
		ctx_ = nullptr;
	}
	if (model_) {
		llama_model_free(model_);
		model_ = nullptr;
	}
	slot_ids_.clear();
	loaded_config_ = EngineConfig();
}

void Runtime::EnsureLoaded(const EngineConfig &config) {
	if (model_ && loaded_config_.SameModel(config)) {
		loaded_config_.max_tokens = config.max_tokens;
		loaded_config_.cache_size = config.cache_size;
		return;
	}
	// Only one decision model is resident at a time: switching frees the previous one (VRAM is the limit).
	Unload();
	lru_.clear();
	lru_index_.clear();

	auto mparams = llama_model_default_params();
	gpu_layers_used_ = llama_supports_gpu_offload() ? (config.gpu_layers < 0 ? 999 : config.gpu_layers) : 0;
	mparams.n_gpu_layers = gpu_layers_used_;
	model_ = llama_model_load_from_file(config.path.c_str(), mparams);
	if (!model_) {
		throw std::runtime_error("could not load the model '" + config.path +
		                         "' (set DECIDE_VERBOSE=1 for llama.cpp logs)");
	}

	const int32_t parallel = std::max(1, config.parallel);
	const int32_t batch = std::max(config.batch_tokens, config.max_tokens);
	auto cparams = llama_context_default_params();
	cparams.n_ctx = (uint32_t)batch;     // unified KV: every sequence of one call shares these cells
	cparams.n_batch = (uint32_t)batch;   // a whole group of sequences goes in one llama_process call
	cparams.n_ubatch = 512;
	cparams.n_seq_max = (uint32_t)parallel;
	cparams.kv_unified = true;
	cparams.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO; // measured: essential with 16+ parallel sequences
	cparams.no_perf = true;
	int32_t threads = config.threads > 0 ? config.threads : (int32_t)std::max(1u, std::thread::hardware_concurrency());
	cparams.n_threads = threads;
	cparams.n_threads_batch = threads;
	if (config.kv_q8) {
		cparams.type_k = GGML_TYPE_Q8_0;
		cparams.type_v = GGML_TYPE_Q8_0;
	}
	ctx_ = llama_init_from_model(model_, cparams);
	if (!ctx_) {
		llama_model_free(model_);
		model_ = nullptr;
		throw std::runtime_error("could not create a llama.cpp context (try a smaller decide_parallel or "
		                         "decide_batch_tokens, or SET decide_kv_cache = 'q8_0')");
	}
	n_ctx_ = (int32_t)llama_n_ctx(ctx_);

	// Answer slots: A..Z must each be a single token.
	slot_ids_.clear();
	for (const char *p = Prompt::LETTERS; *p; p++) {
		auto ids = TokenizeWith(model_, std::string(1, *p));
		if (ids.size() != 1) {
			Unload();
			throw std::runtime_error(std::string("answer slot '") + *p + "' is not a single token in this model");
		}
		slot_ids_.push_back(ids[0]);
	}
	loaded_config_ = config;
}

std::vector<int32_t> Runtime::BuildTokens(const EngineConfig &config, const DecisionRequest &req, bool &truncated) {
	truncated = false;
	auto tokens = TokenizeWith(model_, Prompt::Render(req));
	if ((int32_t)tokens.size() <= config.max_tokens) {
		return tokens;
	}
	// Too long: keep the beginning of the state so that the whole prompt fits in max_tokens.
	auto fixed = TokenizeWith(model_, Prompt::RenderWithState(req, std::string())).size();
	int32_t budget = config.max_tokens - (int32_t)fixed - 4;
	if (budget <= 0) {
		throw std::runtime_error("the question and options alone exceed decide_max_tokens");
	}
	auto state_tokens = TokenizeWith(model_, req.state);
	if ((int32_t)state_tokens.size() > budget) {
		state_tokens.resize(budget);
	}
	const llama_vocab *vocab = llama_model_get_vocab(model_);
	std::string text(req.state.size() + 16, '\0');
	int32_t n = llama_detokenize(vocab, state_tokens.data(), (int32_t)state_tokens.size(), &text[0],
	                             (int32_t)text.size(), false, false);
	if (n < 0) {
		text.resize(-n);
		n = llama_detokenize(vocab, state_tokens.data(), (int32_t)state_tokens.size(), &text[0], (int32_t)text.size(),
		                     false, false);
	}
	text.resize(std::max(0, n));
	truncated = true;
	tokens = TokenizeWith(model_, Prompt::RenderWithState(req, text));
	if ((int32_t)tokens.size() > config.max_tokens) {
		tokens.resize(config.max_tokens); // tokenizer boundary drift; never exceed the context
	}
	return tokens;
}

//! Run prompts through the model, grouping as many sequences as fit in one llama_process call.
//! Only the last token of each sequence produces logits (the vocabulary has ~248k entries).
void Runtime::Evaluate(const std::vector<std::vector<int32_t>> &prompts, const std::vector<size_t> &n_slots,
                       std::vector<Probabilities> &out) {
	const int32_t parallel = std::max(1, loaded_config_.parallel);
	llama_batch_ext *batch = llama_batch_ext_init(ctx_);
	llama_memory_t mem = llama_get_memory(ctx_);

	// Longest first: groups of similar length waste fewer KV cells and keep ubatches full.
	std::vector<size_t> order(prompts.size());
	for (size_t i = 0; i < order.size(); i++) {
		order[i] = i;
	}
	std::stable_sort(order.begin(), order.end(),
	                 [&](size_t a, size_t b) { return prompts[a].size() > prompts[b].size(); });

	size_t next = 0;
	try {
		while (next < order.size()) {
			llama_memory_clear(mem, true);
			llama_batch_ext_clear(batch);
			std::vector<std::pair<size_t, int32_t>> outputs; // (prompt index, batch index of its last token)
			int32_t used = 0;
			while (next < order.size() && (int32_t)outputs.size() < parallel) {
				const auto &toks = prompts[order[next]];
				if (!outputs.empty() && used + (int32_t)toks.size() > n_ctx_) {
					break;
				}
				const llama_seq_id seq = (llama_seq_id)outputs.size();
				int32_t idx = -1;
				for (size_t p = 0; p < toks.size(); p++) {
					idx = llama_batch_ext_add_token(batch, seq, toks[p]);
					if (idx < 0) {
						throw std::runtime_error("llama.cpp batch is full");
					}
					llama_pos pos = (llama_pos)p;
					llama_batch_ext_set_pos(batch, idx, &pos);
				}
				llama_batch_ext_set_output_logits(batch, idx, true);
				outputs.emplace_back(order[next], idx);
				used += (int32_t)toks.size();
				next++;
			}

			auto t0 = std::chrono::steady_clock::now();
			int32_t rc = llama_process(ctx_, LLAMA_PROCESS_TYPE_DECODE, batch);
			stats_.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			stats_.process_calls++;
			if (rc != 0) {
				throw std::runtime_error("llama_process failed with code " + std::to_string(rc) +
				                         " (try a smaller decide_parallel or decide_batch_tokens)");
			}
			for (auto &o : outputs) {
				const float *logits = llama_get_logits_ith(ctx_, o.second);
				if (!logits) {
					throw std::runtime_error("no logits for an evaluated sequence");
				}
				const size_t k = n_slots[o.first];
				Probabilities p(k);
				float m = -INFINITY;
				for (size_t s = 0; s < k; s++) {
					p[s] = logits[slot_ids_[s]];
					m = std::max(m, p[s]);
				}
				double z = 0;
				for (size_t s = 0; s < k; s++) {
					p[s] = std::exp(p[s] - m);
					z += p[s];
				}
				for (size_t s = 0; s < k; s++) {
					p[s] = (float)(p[s] / z);
				}
				out[o.first] = std::move(p);
			}
		}
	} catch (...) {
		llama_batch_ext_free(batch);
		throw;
	}
	llama_batch_ext_free(batch);
}

std::vector<Probabilities> Runtime::Decide(const EngineConfig &config, const std::vector<DecisionRequest> &requests) {
	std::lock_guard<std::mutex> guard(engine_lock_);
	EnsureLoaded(config);

	std::vector<Probabilities> result(requests.size());
	std::vector<std::vector<int32_t>> prompts;         // unique prompts to evaluate
	std::vector<size_t> prompt_slots;                  // option count of each unique prompt
	std::vector<std::string> prompt_keys;              // cache key of each unique prompt
	std::vector<std::vector<size_t>> prompt_requests;  // requests answered by each unique prompt
	std::unordered_map<std::string, size_t> seen;      // key -> unique prompt index

	for (size_t i = 0; i < requests.size(); i++) {
		const auto &req = requests[i];
		stats_.decisions++;
		std::string key = config.path + '\x1f' + Prompt::Render(req);
		if (config.cache_size > 0) {
			auto hit = lru_index_.find(key);
			if (hit != lru_index_.end()) {
				lru_.splice(lru_.begin(), lru_, hit->second);
				result[i] = hit->second->second;
				stats_.cache_hits++;
				continue;
			}
		}
		auto dup = seen.find(key);
		if (dup != seen.end()) {
			prompt_requests[dup->second].push_back(i);
			stats_.deduplicated++;
			continue;
		}
		bool truncated = false;
		auto tokens = BuildTokens(config, req, truncated);
		stats_.truncated += truncated ? 1 : 0;
		stats_.prompt_tokens += tokens.size();
		seen.emplace(key, prompts.size());
		prompts.push_back(std::move(tokens));
		prompt_slots.push_back(req.labels.size());
		prompt_keys.push_back(std::move(key));
		prompt_requests.push_back({i});
	}

	if (!prompts.empty()) {
		std::vector<Probabilities> answers(prompts.size());
		Evaluate(prompts, prompt_slots, answers);
		stats_.evaluated += prompts.size();
		for (size_t u = 0; u < prompts.size(); u++) {
			for (auto i : prompt_requests[u]) {
				result[i] = answers[u];
			}
			if (config.cache_size > 0) {
				lru_.emplace_front(prompt_keys[u], answers[u]);
				lru_index_[prompt_keys[u]] = lru_.begin();
				while (lru_.size() > config.cache_size) {
					lru_index_.erase(lru_.back().first);
					lru_.pop_back();
				}
			}
		}
	}
	return result;
}

EngineStats Runtime::Stats() {
	std::lock_guard<std::mutex> guard(engine_lock_);
	return stats_;
}

void Runtime::ResetStats() {
	std::lock_guard<std::mutex> guard(engine_lock_);
	stats_ = EngineStats();
}

void Runtime::ClearCache() {
	std::lock_guard<std::mutex> guard(engine_lock_);
	lru_.clear();
	lru_index_.clear();
}

std::string Runtime::LoadedPath() {
	std::lock_guard<std::mutex> guard(engine_lock_);
	return model_ ? loaded_config_.path : std::string();
}

std::string Runtime::LoadedInfo() {
	std::lock_guard<std::mutex> guard(engine_lock_);
	if (!model_) {
		return "";
	}
	char desc[256];
	llama_model_desc(model_, desc, sizeof(desc));
	return std::string(desc) + " | gpu_layers=" + std::to_string(gpu_layers_used_) +
	       " | n_ctx=" + std::to_string(n_ctx_) + " | parallel=" + std::to_string(loaded_config_.parallel) +
	       " | kv=" + (loaded_config_.kv_q8 ? "q8_0" : "f16");
}

std::string Runtime::LlamaBuild() {
	return DECIDE_LLAMA_BUILD;
}

std::string Runtime::Backends() {
	Get(); // make sure backends are registered
	std::string out;
	for (size_t i = 0; i < ggml_backend_reg_count(); i++) {
		if (!out.empty()) {
			out += ", ";
		}
		out += ggml_backend_reg_name(ggml_backend_reg_get(i));
	}
	return out;
}

} // namespace decide
