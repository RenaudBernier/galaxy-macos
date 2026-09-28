// Data that lives inside the original executable (main.dol) rather than in
// files on the disc. It is read from the user's disc image at startup; the
// port does not contain any game data.
#pragma once

#include <cstdint>

namespace port::dol {

// Reads main.dol from the disc image and fills the port-defined data symbols.
bool loadEmbeddedData(const char* discPath);

}  // namespace port::dol
