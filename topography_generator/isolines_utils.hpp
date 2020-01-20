#pragma once

#include <string>

namespace topography_generator
{
std::string GetIsolinesFilePath(int lat, int lon, std::string const & dir);
std::string GetIsolinesFilePath(std::string const & countryId, std::string const & dir);
}  // namespace topography_generator
