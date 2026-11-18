#include "decide_store.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension_helper.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <unordered_set>

namespace duckdb {
namespace decide_store {

static constexpr idx_t IO_CHUNK = 8 * 1024 * 1024;
static constexpr const char *VERIFIED_SUFFIX = ".verified";

// -----------------------------------------------------------------------------------------------------------------
// SHA-256 (FIPS 180-4). Self-contained so the loadable extension does not depend on DuckDB-internal symbols.
// -----------------------------------------------------------------------------------------------------------------
class Sha256 {
public:
	Sha256() {
		static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
		memcpy(h_, init, sizeof(h_));
	}
	void Update(const uint8_t *data, size_t len) {
		total_ += len;
		if (fill_) {
			size_t take = std::min(len, (size_t)64 - fill_);
			memcpy(buf_ + fill_, data, take);
			fill_ += take;
			data += take;
			len -= take;
			if (fill_ == 64) {
				Block(buf_);
				fill_ = 0;
			}
		}
		while (len >= 64) {
			Block(data);
			data += 64;
			len -= 64;
		}
		if (len) {
			memcpy(buf_, data, len);
			fill_ = len;
		}
	}
	std::string HexDigest() {
		uint64_t bits = total_ * 8;
		uint8_t pad = 0x80;
		Update(&pad, 1);
		uint8_t zero = 0;
		while (fill_ != 56) {
			Update(&zero, 1);
		}
		uint8_t len_be[8];
		for (int i = 0; i < 8; i++) {
			len_be[i] = (uint8_t)(bits >> (56 - 8 * i));
		}
		Update(len_be, 8);
		static const char *hex = "0123456789abcdef";
		std::string out;
		for (auto w : h_) {
			for (int s = 28; s >= 0; s -= 4) {
				out += hex[(w >> s) & 0xf];
			}
		}
		return out;
	}

private:
	static uint32_t Rotr(uint32_t x, int n) {
		return (x >> n) | (x << (32 - n));
	}
	void Block(const uint8_t *p) {
		static const uint32_t k[64] = {
		    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
		uint32_t w[64];
		for (int i = 0; i < 16; i++) {
			w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
		}
		for (int i = 16; i < 64; i++) {
			uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
		for (int i = 0; i < 64; i++) {
			uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		h_[0] += a;
		h_[1] += b;
		h_[2] += c;
		h_[3] += d;
		h_[4] += e;
		h_[5] += f;
		h_[6] += g;
		h_[7] += h;
	}

	uint32_t h_[8];
	uint8_t buf_[64];
	size_t fill_ = 0;
	uint64_t total_ = 0;
};

std::string Sha256Hex(const std::string &bytes) {
	Sha256 s;
	s.Update((const uint8_t *)bytes.data(), bytes.size());
	return s.HexDigest();
}

// -----------------------------------------------------------------------------------------------------------------
// Verification stamps
// -----------------------------------------------------------------------------------------------------------------
static string Stamp(FileSystem &fs, const string &path, const decide::ModelSpec &spec) {
	auto handle = fs.OpenFile(path, FileFlags::FILE_FLAGS_READ);
	return StringUtil::Format("%s %llu %lld", spec.sha256, (unsigned long long)handle->GetFileSize(),
	                          (long long)fs.GetLastModifiedTime(*handle).value);
}

static string ReadSmall(FileSystem &fs, const string &path) {
	auto handle = fs.OpenFile(path, FileFlags::FILE_FLAGS_READ);
	auto size = handle->GetFileSize();
	string out(size, '\0');
	if (size) {
		handle->Read(&out[0], size, 0);
	}
	return out;
}

static void WriteStamp(FileSystem &fs, const string &path, const decide::ModelSpec &spec) {
	auto marker = path + VERIFIED_SUFFIX;
	try {
		fs.TryRemoveFile(marker);
		auto stamp = Stamp(fs, path, spec);
		auto handle = fs.OpenFile(marker, FileFlags::FILE_FLAGS_WRITE | FileFlags::FILE_FLAGS_FILE_CREATE_NEW);
		handle->Write((void *)stamp.data(), stamp.size());
		handle->Sync();
	} catch (std::exception &) {
		fs.TryRemoveFile(marker); // only an optimisation: next time the file is hashed again
	}
}

static bool HasStamp(FileSystem &fs, const string &path, const decide::ModelSpec &spec) {
	auto marker = path + VERIFIED_SUFFIX;
	try {
		return fs.FileExists(marker) && ReadSmall(fs, marker) == Stamp(fs, path, spec);
	} catch (std::exception &) {
		return false;
	}
}

//! Prints "decide: <what> 42% (1.1/2.5 GB, 38 MB/s)" to stderr every few percent.
class Progress {
public:
	Progress(string what, uint64_t total) : what_(std::move(what)), total_(total), t0_(Clock::now()) {
	}
	void Add(uint64_t n) {
		done_ += n;
		int pct = total_ ? (int)(done_ * 100 / total_) : 0;
		if (pct >= next_) {
			double secs = std::chrono::duration<double>(Clock::now() - t0_).count();
			fprintf(stderr, "\rdecide: %s %3d%% (%.2f/%.2f GB, %.0f MB/s)   ", what_.c_str(), pct, done_ / 1e9,
			        total_ / 1e9, secs > 0 ? done_ / 1e6 / secs : 0.0);
			fflush(stderr);
			next_ = pct + 5;
		}
	}
	void Finish() {
		fprintf(stderr, "\n");
		fflush(stderr);
	}

private:
	using Clock = std::chrono::steady_clock;
	string what_;
	uint64_t total_, done_ = 0;
	int next_ = 0;
	Clock::time_point t0_;
};

static void VerifyExisting(ClientContext &context, FileSystem &fs, const string &path,
                           const decide::ModelSpec &spec) {
	if (HasStamp(fs, path, spec)) {
		return;
	}
	auto handle = fs.OpenFile(path, FileFlags::FILE_FLAGS_READ);
	auto size = handle->GetFileSize();
	if (size != spec.size_bytes) {
		throw IOException("decide: '%s' has %llu bytes but %s needs %llu; it is incomplete or a different file. "
		                  "Delete it to download it again.",
		                  path, (unsigned long long)size, spec.id, (unsigned long long)spec.size_bytes);
	}
	Sha256 sha;
	Progress progress(string("verifying ") + spec.id, size);
	std::vector<uint8_t> buffer(IO_CHUNK);
	while (true) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		auto read = handle->Read(buffer.data(), IO_CHUNK);
		if (read <= 0) {
			break;
		}
		sha.Update(buffer.data(), (size_t)read);
		progress.Add((uint64_t)read);
	}
	progress.Finish();
	handle.reset();
	auto actual = sha.HexDigest();
	if (actual != spec.sha256) {
		throw IOException("decide: checksum mismatch for '%s' (expected %s, got %s). Delete the file to download "
		                  "it again.",
		                  path, spec.sha256, actual);
	}
	WriteStamp(fs, path, spec);
}

static string Download(ClientContext &context, FileSystem &fs, const decide::ModelSpec &spec, const string &dir) {
	auto url = decide::DownloadUrl(spec);
	ExtensionHelper::TryAutoLoadExtension(context, "httpfs");
	auto target_dir = fs.JoinPath(dir, spec.id);
	fs.CreateDirectoriesRecursive(target_dir);
	auto target = fs.JoinPath(target_dir, spec.file);
	// Unique temporary name: concurrent processes never write the same file; the final rename is atomic.
	std::random_device random;
	auto tmp = StringUtil::Format("%s.tmp.%08x%08x", target, random(), random());
	Sha256 sha;
	try {
		auto source = fs.OpenFile(url, FileFlags::FILE_FLAGS_READ);
		auto sink = fs.OpenFile(tmp, FileFlags::FILE_FLAGS_WRITE | FileFlags::FILE_FLAGS_FILE_CREATE_NEW);
		Progress progress(string("downloading ") + spec.id, spec.size_bytes);
		std::vector<uint8_t> buffer(IO_CHUNK);
		while (true) {
			if (context.IsInterrupted()) {
				throw InterruptException();
			}
			auto read = source->Read(buffer.data(), IO_CHUNK);
			if (read <= 0) {
				break;
			}
			sha.Update(buffer.data(), (size_t)read);
			sink->Write(buffer.data(), (idx_t)read);
			progress.Add((uint64_t)read);
		}
		progress.Finish();
		sink->Sync();
	} catch (InterruptException &) {
		fs.TryRemoveFile(tmp);
		throw;
	} catch (std::exception &ex) {
		fs.TryRemoveFile(tmp);
		ErrorData error(ex);
		throw IOException("decide: could not download %s: %s\nDownload it by hand into '%s', or SET "
		                  "decide_model_path = '/path/to/%s'. (The httpfs extension is required for downloads.)",
		                  url, error.RawMessage(), target_dir, spec.file);
	}
	auto actual = sha.HexDigest();
	if (actual != spec.sha256) {
		fs.TryRemoveFile(tmp);
		throw IOException("decide: checksum mismatch after downloading %s (expected %s, got %s)", url, spec.sha256,
		                  actual);
	}
	fs.TryRemoveFile(target + VERIFIED_SUFFIX);
	fs.MoveFile(tmp, target);
	WriteStamp(fs, target, spec);
	return target;
}

// -----------------------------------------------------------------------------------------------------------------

static std::mutex store_lock;
static std::unordered_set<std::string> verified_paths; // verified in this process

std::string EnsureModelFile(ClientContext &context, const decide::ModelSpec &spec, const std::string &dir_setting,
                            bool auto_download) {
	auto &fs = FileSystem::GetFileSystem(context);
	auto dir = fs.ExpandPath(dir_setting);
	std::lock_guard<std::mutex> guard(store_lock); // one download / verification at a time
	auto path = decide::ResolveModelFile(spec, dir);
	if (!path.empty()) {
		if (verified_paths.count(path)) {
			return path;
		}
		VerifyExisting(context, fs, path, spec);
		verified_paths.insert(path);
		return path;
	}
	if (!auto_download) {
		throw InvalidInputException(
		    "Model '%s' (%s) is not downloaded. Expected '%s' under decide_model_dir = '%s'.\n"
		    "SET decide_auto_download = true, run SELECT decide_download('%s'), download it from %s, or SET "
		    "decide_model_path.",
		    spec.id, spec.file, spec.file, dir, spec.id, decide::DownloadUrl(spec));
	}
	path = Download(context, fs, spec, dir);
	verified_paths.insert(path);
	return path;
}

} // namespace decide_store
} // namespace duckdb
