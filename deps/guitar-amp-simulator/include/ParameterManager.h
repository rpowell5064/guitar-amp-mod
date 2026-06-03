#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <stdexcept>
#include <algorithm>

// Thread-agnostic parameter registry.
// Parameters are registered with a name, default, min and max.
// UI code calls set(); DSP code calls get() at the top of each block.
class ParameterManager {
public:
    using ChangeCallback = std::function<void(const std::string&, float)>;

    struct ParamInfo {
        float value;
        float minVal;
        float maxVal;
    };

    void registerParameter(const std::string& id,
                           float defaultValue,
                           float minVal = 0.0f,
                           float maxVal = 1.0f);

    void  setParameter(const std::string& id, float value);
    float getParameter(const std::string& id) const;
    bool  hasParameter(const std::string& id) const noexcept;

    // Optional callback, fired on every setParameter call.
    void setChangeCallback(ChangeCallback cb) { onChange = std::move(cb); }

private:
    std::unordered_map<std::string, ParamInfo> params;
    ChangeCallback onChange;
};
