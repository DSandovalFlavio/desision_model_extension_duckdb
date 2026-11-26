#include "decide_dll.hpp"

#ifndef DECIDE_CUDA_DLLS
#define DECIDE_CUDA_DLLS ""
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <sstream>
#include <vector>

namespace decide {

#ifdef _WIN32

static std::wstring Widen(const std::string &s) {
	return std::wstring(s.begin(), s.end()); // DLL names are ASCII
}

static std::string Narrow(const std::wstring &w) {
	if (w.empty()) {
		return "";
	}
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string out(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], n, nullptr, nullptr);
	return out;
}

static std::wstring ModuleDirectory() {
	HMODULE self = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCWSTR>(&ModuleDirectory), &self)) {
		return L"";
	}
	std::wstring path(32768, L'\0');
	DWORD n = GetModuleFileNameW(self, &path[0], (DWORD)path.size());
	path.resize(n);
	auto slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

std::string ExtensionDirectory() {
	return Narrow(ModuleDirectory());
}

std::string PreloadCudaLibraries() {
	std::vector<std::string> dlls;
	std::stringstream list(DECIDE_CUDA_DLLS);
	for (std::string item; std::getline(list, item, ',');) {
		if (!item.empty()) {
			dlls.push_back(item);
		}
	}
	if (dlls.empty()) {
		return ""; // CPU build
	}
	// The NVIDIA driver provides nvcuda.dll in System32.
	if (!GetModuleHandleW(L"nvcuda.dll") && !LoadLibraryW(L"nvcuda.dll")) {
		return "nvcuda.dll was not found: this is the CUDA build of decide_amox and it needs an NVIDIA GPU with its "
		       "driver installed. Update the NVIDIA driver, or use the cpu build of decide_amox.";
	}
	const std::wstring dir = ModuleDirectory();
	std::string missing;
	for (auto &dll : dlls) {
		std::wstring name = Widen(dll);
		if (GetModuleHandleW(name.c_str())) {
			continue; // already loaded by the host process
		}
		HMODULE h = nullptr;
		if (!dir.empty()) {
			// LOAD_WITH_ALTERED_SEARCH_PATH: dependencies of this DLL (cublas -> cublasLt) are searched in its folder.
			h = LoadLibraryExW((dir + L"\\" + name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		}
		if (!h) {
			h = LoadLibraryW(name.c_str()); // .exe folder, System32, PATH
		}
		if (!h) {
			missing += (missing.empty() ? "" : ", ") + dll;
		}
	}
	if (!missing.empty()) {
		return "the CUDA build of decide_amox could not find " + missing + ". Copy the CUDA DLLs that come in the "
		       "cuda12 zip next to the extension file (" + Narrow(dir) + "), next to the program's .exe, or into a "
		       "folder on PATH; or use the cpu build of decide_amox.";
	}
	return "";
}

#else

std::string ExtensionDirectory() {
	return "";
}

std::string PreloadCudaLibraries() {
	return "";
}

#endif

} // namespace decide
