// Upstream's resource files, by the names it asks for them under.
// The bytes are in resources.S.

#include "common/types.h"

#include <cstddef>
#include <string_view>

extern "C" {
#ifdef CHIMERA_ARCADE
extern const u8 chimera_res_arcadedb[];
extern const u8 chimera_res_arcadedb_end[];
#else
extern const u8 chimera_res_gamedb[];
extern const u8 chimera_res_gamedb_end[];
#endif
}

const u8* ChimeraResource(std::string_view filename, size_t* size)
{
#ifdef CHIMERA_ARCADE
  if (filename == "arcadedb.yaml")
  {
    *size = static_cast<size_t>(chimera_res_arcadedb_end - chimera_res_arcadedb);
    return chimera_res_arcadedb;
  }
#else
  if (filename == "gamedb.yaml")
  {
    *size = static_cast<size_t>(chimera_res_gamedb_end - chimera_res_gamedb);
    return chimera_res_gamedb;
  }
#endif

  // discdb.yaml (track hashes, for verifying a dump against Redump) and
  // chtdb.txt (a library of cheat codes) are not the machine's.
  *size = 0;
  return nullptr;
}
