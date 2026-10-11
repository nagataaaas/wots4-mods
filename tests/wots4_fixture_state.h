#pragma once
// What the test Mod (wots4_mod_fixture.cpp) reports through FixtureModState():
//   bits 0-7 : number of Wots4ModInitialize calls
//   bit  8   : context->directory matched the folder of mod.dll
//   bit  16  : DirectInput8Create called during initialization was forwarded
//              to the system dinput8.dll (which rejects the empty IID)
#include <windows.h>

namespace fixture {

using StateFunction = LONG(WINAPI*)();

constexpr LONG makeState(LONG calls, bool directoryMatched, bool reentrantCallForwarded) {
    return calls | (directoryMatched ? 1 << 8 : 0) | (reentrantCallForwarded ? 1 << 16 : 0);
}

}  // namespace fixture
