// sa2-nx's native Direct3D layer (app/src/me/sa2_api_*.cpp, docs/api-renderer.md) is not ported to Space Channel 5
// Part 2: it holds SA2's guest addresses (sa2_api_internal.h). The renderer's 10 s report calls its report function.
#include "me/sa2_api.h"

namespace sa2::api {
void Report() {}
}  // namespace sa2::api
