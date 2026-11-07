// Aegis2D — shared-library symbol visibility.
//
// The whole engine builds as ONE dynamic library (aegis_engine: .so on Linux,
// .dll on Windows). Every public class, struct and free function that crosses
// the boundary is annotated with AEGIS_API:
//   * Windows: __declspec(dllexport) while the engine itself is compiled
//     (AEGIS_ENGINE_BUILDING is defined on the engine's own targets),
//   * Windows: __declspec(dllimport) everywhere else,
//   * Linux/macOS: __attribute__((visibility("default"))) — the engine targets
//     compile with -fvisibility=hidden so unannotated symbols stay private.
// Templates, inline/constexpr functions and nested classes inside an exported
// class need no annotation.
#pragma once

#include <aegis/export.h>

#if defined(_WIN32) || defined(_WIN64)
#  if defined(AEGIS_ENGINE_BUILDING)
#    define AEGIS_API __declspec(dllexport)
#  else
#    define AEGIS_API __declspec(dllimport)
#  endif
#  define AEGIS_LOCAL
#else
#  define AEGIS_API __attribute__((visibility("default")))
#  define AEGIS_LOCAL __attribute__((visibility("hidden")))
#endif
