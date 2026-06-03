#include "ParameterManager.h"
#include <stdexcept>

void ParameterManager::registerParameter(const std::string& id,
                                          float defaultValue,
                                          float minVal,
                                          float maxVal) {
    params[id] = { std::clamp(defaultValue, minVal, maxVal), minVal, maxVal };
}

void ParameterManager::setParameter(const std::string& id, float value) {
    auto it = params.find(id);
    if (it == params.end())
        throw std::out_of_range("ParameterManager: unknown parameter '" + id + "'");
    it->second.value = std::clamp(value, it->second.minVal, it->second.maxVal);
    if (onChange)
        onChange(id, it->second.value);
}

float ParameterManager::getParameter(const std::string& id) const {
    auto it = params.find(id);
    if (it == params.end())
        throw std::out_of_range("ParameterManager: unknown parameter '" + id + "'");
    return it->second.value;
}

bool ParameterManager::hasParameter(const std::string& id) const noexcept {
    return params.count(id) > 0;
}
