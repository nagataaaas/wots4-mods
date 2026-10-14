// Wide-string forms of the values in the generated wots4_version.h.
#pragma once
#include "wots4_version.h"

#define WOTS4_WIDEN_LITERAL(text) L##text
#define WOTS4_WIDEN(text) WOTS4_WIDEN_LITERAL(text)

namespace wots4::installer {

constexpr const wchar_t* installerVersion = WOTS4_WIDEN(WOTS4_VERSION_STRING);
constexpr const wchar_t* loaderProductName = WOTS4_WIDEN(WOTS4_LOADER_PRODUCT_NAME);

}  // namespace wots4::installer
