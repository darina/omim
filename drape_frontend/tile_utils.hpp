#pragma once

#include "tile_key.hpp"
#include "../std/function.hpp"
#include "../std/set.hpp"

namespace df
{

using TTilesCollection = set<TileKey>;

/// this function determines the coverage of a tile in specified zoom level
void CalcTilesCoverage(TileKey const & tileKey, int targetZoom, TTilesCollection & tiles);

/// this function determines the coverage of tiles in specified zoom level
void CalcTilesCoverage(set<TileKey> const & tileKeys, int targetZoom, TTilesCollection & tiles);

/// this function determines the coverage of a tile in specified zoom level. Each new tile can be processed
/// in processTile callback
void CalcTilesCoverage(TileKey const & tileKey, int targetZoom, function<void(TileKey const &)> processTile);

/// this function checks if targetTileKey is above tileKey
bool IsTileAbove(TileKey const & tileKey, TileKey const & targetTileKey);

/// this function checks if targetTileKey is below tileKey
bool IsTileBelow(TileKey const & tileKey, TileKey const & targetTileKey);

/// this function returns parent tile on specified zoom level
TileKey GetParentTile(TileKey const & tileKey, int targetZoom);

} // namespace df


