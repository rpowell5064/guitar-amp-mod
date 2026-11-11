#pragma once
// Display names of the modelled originals (real product names). The device build
// (HEXCHAIN_ANAGRAM) ships none of them: nothing on the device reads a model's name, and its
// binaries must carry no third-party marks. Every other build keeps them.
#ifdef HEXCHAIN_ANAGRAM
#define GAS_NAME(s) ""
#else
#define GAS_NAME(s) s
#endif
