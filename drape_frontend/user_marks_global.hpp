#pragma once

#include <set>
#include <vector>

namespace df
{
using MarkID = uint32_t;
using LineID = uint32_t;
using MarkGroupID = size_t;
using MarkIDCollection = std::vector<MarkID>;
using LineIDCollection = std::vector<LineID>;
using MarkIDSet = std::set<MarkID>;
using LineIDSet = std::set<LineID>;
using GroupIDList = std::vector<MarkGroupID>;
using GroupIDSet = std::set<MarkGroupID>;
}  // namespace df
