#include "decide_models.hpp"

#include <cstdlib>
#include <filesystem>

namespace decide {

namespace fs = std::filesystem;

// Hashes and sizes from the Hugging Face API (LFS oid), 18-nov-2026. Quantisations are the ones scored in phase 0
// (eval/RESULTS.md): 0.8B Q8_0 85.8 %, 4B Q4_K_M 96.0 %, 9B Q4_K_M 100 % on the 95 ads it reached.
static const std::vector<ModelSpec> MODELS = {
    {"onejev-0.8b", "OneJev1.6", "0.8B", "Q8_0", "mradermacher/OneJev1.6-0.8B-GGUF", "OneJev1.6-0.8B.Q8_0.gguf",
     811844096ULL, "1225c4f8d907636c2d4725ae5ca33a2ba32a64e84efc6a09052839790970b3ae", "Apache-2.0", false,
     "fastest; high volume, lowest latency"},
    {"onejev-4b", "OneJev1.6", "4B", "Q4_K_M", "mradermacher/OneJev1.6-4B-GGUF", "OneJev1.6-4B.Q4_K_M.gguf",
     2708805024ULL, "8a12b976e7d2ea60fb603720ce07e2ef5e85b9b8a2a5519cb0bf2cc72aacac8d", "Apache-2.0", true,
     "default; best quality/speed balance"},
    {"onejev-9b", "OneJev1.6", "9B", "Q4_K_M", "mradermacher/OneJev1.6-9B-GGUF", "OneJev1.6-9B.Q4_K_M.gguf",
     5629109696ULL, "2b3b2007465427fafb5060f0d5172d9c37c2349021f22532ae741fd823f0d738", "Apache-2.0", false,
     "highest quality; fits in 8 GB of VRAM"},
};

const std::vector<ModelSpec> &Models() {
	return MODELS;
}

const ModelSpec *FindModel(const std::string &id) {
	for (auto &m : MODELS) {
		if (id == m.id) {
			return &m;
		}
	}
	return nullptr;
}

const ModelSpec &DefaultModel() {
	for (auto &m : MODELS) {
		if (m.is_default) {
			return m;
		}
	}
	return MODELS.front();
}

std::string ModelIdList() {
	std::string out;
	for (auto &m : MODELS) {
		if (!out.empty()) {
			out += ", ";
		}
		out += "'" + std::string(m.id) + "'";
	}
	return out;
}

std::string DownloadUrl(const ModelSpec &spec) {
	return std::string("https://huggingface.co/") + spec.repo + "/resolve/main/" + spec.file;
}

std::string DefaultModelDir() {
	const char *home = std::getenv("USERPROFILE");
	if (!home || !*home) {
		home = std::getenv("HOME");
	}
	fs::path base = (home && *home) ? fs::path(home) : fs::current_path();
	return (base / ".duckdb" / "decide_amox" / "models").generic_string();
}

std::string ResolveModelFile(const ModelSpec &spec, const std::string &dir) {
	if (dir.empty()) {
		return "";
	}
	std::error_code ec;
	for (auto candidate : {fs::path(dir) / spec.file, fs::path(dir) / spec.id / spec.file}) {
		if (fs::is_regular_file(candidate, ec)) {
			return candidate.generic_string();
		}
	}
	return "";
}

} // namespace decide
