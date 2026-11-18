#pragma once

#include "decide_prompt.hpp"

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct llama_model;
struct llama_context;

namespace decide {

//! How to run a model. Changing any field except max_tokens/cache_size reloads the model.
struct EngineConfig {
	std::string path;          // GGUF file
	int32_t gpu_layers = -1;   // -1 = all layers on the GPU (ignored by CPU-only builds)
	int32_t parallel = 16;     // sequences evaluated together in one llama_process call
	int32_t batch_tokens = 4096; // max tokens per call (= unified KV size)
	int32_t threads = 0;       // 0 = hardware concurrency
	bool kv_q8 = false;        // q8_0 KV cache (more parallel sequences in less VRAM)
	int32_t max_tokens = 1024; // per prompt; longer states are truncated
	size_t cache_size = 100000; // LRU entries (0 = off)

	bool SameModel(const EngineConfig &o) const {
		return path == o.path && gpu_layers == o.gpu_layers && parallel == o.parallel &&
		       batch_tokens == o.batch_tokens && threads == o.threads && kv_q8 == o.kv_q8;
	}
};

//! Probabilities over the request's options, in option order.
using Probabilities = std::vector<float>;

struct EngineStats {
	uint64_t decisions = 0;   // requests answered
	uint64_t cache_hits = 0;  // answered from the LRU cache
	uint64_t deduplicated = 0; // identical prompts inside one call
	uint64_t evaluated = 0;   // prompts actually run through the model
	uint64_t prompt_tokens = 0;
	uint64_t truncated = 0;
	uint64_t process_calls = 0;
	double seconds = 0;       // time spent in llama_process
};

//! Process-wide llama.cpp state shared by every DuckDB connection.
//!  - vocab-only models for decide_tokenize (no weights);
//!  - one full model + context for decisions. A single context means a single owner of the GPU: DuckDB threads
//!    hand whole chunks to Decide(), which serialises them and batches each chunk across sequences.
class Runtime {
public:
	static Runtime &Get();

	std::vector<int32_t> Tokenize(const std::string &path, const std::string &text, bool add_special = false,
	                              bool parse_special = true);
	bool IsLoaded(const std::string &path);

	//! Answer a batch of requests (typically one DataChunk). Thread-safe.
	std::vector<Probabilities> Decide(const EngineConfig &config, const std::vector<DecisionRequest> &requests);

	EngineStats Stats();
	void ResetStats();
	void ClearCache();
	std::string LoadedPath();
	std::string LoadedInfo(); // human-readable: model, layers on GPU, context, parallel

	static std::string LlamaBuild();
	static std::string Backends();
	static bool HasGpu();

	~Runtime();

private:
	Runtime();
	llama_model *VocabModel(const std::string &path);
	void EnsureLoaded(const EngineConfig &config);
	void Unload();
	std::vector<int32_t> TokenizeWith(llama_model *model, const std::string &text);
	std::vector<int32_t> BuildTokens(const EngineConfig &config, const DecisionRequest &req, bool &truncated);
	void Evaluate(const std::vector<std::vector<int32_t>> &prompts, const std::vector<size_t> &n_slots,
	              std::vector<Probabilities> &out);

	// vocab-only models (tokenizer)
	std::mutex vocab_lock_;
	std::unordered_map<std::string, llama_model *> vocab_models_;

	// decision engine
	std::mutex engine_lock_;
	EngineConfig loaded_config_;
	llama_model *model_ = nullptr;
	llama_context *ctx_ = nullptr;
	std::vector<int32_t> slot_ids_; // token ids of A..Z
	int32_t n_ctx_ = 0;
	int32_t gpu_layers_used_ = 0;

	// LRU cache: key = path + '\x1f' + rendered prompt
	std::list<std::pair<std::string, Probabilities>> lru_;
	std::unordered_map<std::string, std::list<std::pair<std::string, Probabilities>>::iterator> lru_index_;

	EngineStats stats_;
};

} // namespace decide
