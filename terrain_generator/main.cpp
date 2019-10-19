#include "generator.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"

#include "3party/gflags/src/gflags/gflags.h"

DEFINE_string(srtm_path, "/Users/daravolvenkova/srtm/2000.02.11/", "Path to srtm directory.");
DEFINE_string(mwm_name, "France_Rhone-Alpes_Haute-Savoie", "Name of mwm. Mwm's boundaries used as boundaries of generated terrain.");
DEFINE_string(out_path, "/Users/daravolvenkova/", "Path to output directory.");

int main(int argc, char ** argv)
{
  google::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_srtm_path.empty())
  {
    LOG(LINFO, ("Terrain generation failed: srtm_path must be set."));
    return 1;
  }

  if (FLAGS_mwm_name.empty())
  {
    LOG(LINFO, ("Terrain generation failed: mwm_name must be set."));
    return 1;
  }

  TerrainGenerator generator(FLAGS_srtm_path, FLAGS_out_path);
  generator.ParseTracks("/Users/daravolvenkova/Downloads/outdoor_tracks.csv",
    "/Users/daravolvenkova/terrains");
  //generator.Generate(FLAGS_mwm_name);
  return 0;
}
