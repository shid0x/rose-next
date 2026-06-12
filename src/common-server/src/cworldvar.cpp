#include "cworldvar.h"

#include "nlohmann/json.hpp"

#include <algorithm>

using json = nlohmann::json;

json
CWorldVAR::to_json() const {
    json res = json::array();
    for (size_t i = 0; i < MAX_WORLD_VAR_CNT; ++i) {
        res.push_back(m_nWorldVAR[i]);
    }
    return res;
}

bool
CWorldVAR::from_json(nlohmann::json& j) {
    if (!j.is_array() || !m_nWorldVAR) {
        return false;
    }

    size_t max = std::min(static_cast<size_t>(MAX_WORLD_VAR_CNT), j.size());
    for (size_t i = 0; i < max; ++i) {
        m_nWorldVAR[i] = j[i];
    }
    return true;
}