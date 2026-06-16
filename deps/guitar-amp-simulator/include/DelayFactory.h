#pragma once
#include "DelayBase.h"
#include <memory>
#include <string>

enum class DelayType {
    Digital = 0,
    Tape    = 1,
    Echorec = 2,
    Seraph  = 3   // Keeley Halo-style dual delay
};

class DelayFactory {
public:
    static std::unique_ptr<DelayBase> create(DelayType type);
    static const char*  modelName(DelayType type) noexcept;
    static DelayType    fromIndex(int index)       noexcept;
};
