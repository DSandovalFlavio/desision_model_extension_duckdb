#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace decide {

//! One decision model the extension knows how to run. All OneJev1.6 sizes share architecture (qwen35),
//! tokenizer, chat template, prompt (qev-labels-v2) and readout (option-letter logits), so a model is just a file.
struct ModelSpec {
	const char *id;         // name used in SQL: SET amox_model = '<id>'
	const char *family;     // e.g. "OneJev1.6"
	const char *size_label; // "0.8B", "4B", "9B"
	const char *quant;      // GGUF quantisation
	const char *repo;       // Hugging Face repository
	const char *file;       // GGUF file name inside the repository
	uint64_t size_bytes;    // exact file size
	const char *sha256;     // LFS sha256 of the file
	const char *license;
	bool is_default;
	const char *notes;
};

const std::vector<ModelSpec> &Models();
const ModelSpec *FindModel(const std::string &id);
const ModelSpec &DefaultModel();
std::string ModelIdList(); // "'onejev-0.8b', 'onejev-4b', 'onejev-9b'"

std::string DownloadUrl(const ModelSpec &spec);

//! Default cache directory: <home>/.duckdb/decide/models
std::string DefaultModelDir();

//! Look for the model file under `dir`: <dir>/<file> or <dir>/<id>/<file>. Returns "" if not found.
std::string ResolveModelFile(const ModelSpec &spec, const std::string &dir);

} // namespace decide
