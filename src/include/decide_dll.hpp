#pragma once

#include <string>

namespace decide {

//! Windows CUDA builds only (no-op elsewhere). Loads the CUDA runtime DLLs the extension delay-loads, looking first in
//! the folder of the extension file and then in the normal Windows search order (the .exe folder, System32, PATH).
//! Must run before anything touches CUDA. Returns "" on success, otherwise a message naming what is missing.
std::string PreloadCudaLibraries();

//! Folder of the loaded extension file ("" if unknown).
std::string ExtensionDirectory();

} // namespace decide
