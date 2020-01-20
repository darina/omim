#include "generator.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"

#include "3party/gflags/src/gflags/gflags.h"

DEFINE_string(srtm_path, "/Users/daravolvenkova/srtm/2000.02.11/", "Path to srtm directory.");
DEFINE_int32(left, 5, "Left longitude of tiles rect.");
DEFINE_int32(right, 7, "Right longitude of tiles rect.");
DEFINE_int32(bottom, 45, "Bottom latitude of tiles rect.");
DEFINE_int32(top, 47, "Top latitude of tiles rect.");
DEFINE_uint64(threads, 4u, "Number of threads.");
DEFINE_uint64(tiles_per_thread, 9u, "Max cached tiles per thread");
DEFINE_uint64(isolines_step, 10u, "Isolines step in meters");
DEFINE_uint64(latlon_step_factor, 1u, "Lat/lon step factor");
DEFINE_string(out_path, "/Users/daravolvenkova/isolines/", "Path to output directory.");

int main(int argc, char ** argv)
{
  google::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_srtm_path.empty())
  {
    LOG(LINFO, ("srtm_path must be set."));
    return 1;
  }

  topography_generator::FiltersSequence<geometry::Altitude> filters;
  filters.emplace_back(new topography_generator::MedianFilter<geometry::Altitude>(1));
  filters.emplace_back(new topography_generator::GaussianFilter<geometry::Altitude>(2.0, 1.0));

  topography_generator::Generator generator(FLAGS_srtm_path, FLAGS_threads,
                                            FLAGS_tiles_per_thread);
  topography_generator::IsolinesParams params;
  params.m_leftLon = FLAGS_left;
  params.m_rightLon = FLAGS_right;
  params.m_bottomLat = FLAGS_bottom;
  params.m_topLat = FLAGS_top;
  params.m_outputDir = FLAGS_out_path;
  params.m_alitudesStep = FLAGS_isolines_step;
  params.m_latLonStepFactor = FLAGS_latlon_step_factor;
  //generator.GenerateIsolines(params);

  generator.PackIsolinesForCountry("France_Rhone-Alpes_Haute-Savoie",
                                   "/Users/daravolvenkova/isolines/",
                                   "/Users/daravolvenkova/isolines/");
  return 0;
}
