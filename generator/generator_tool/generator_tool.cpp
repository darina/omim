#include "generator/altitude_generator.hpp"
#include "generator/borders.hpp"
#include "generator/camera_info_collector.hpp"
#include "generator/centers_table_builder.hpp"
#include "generator/check_model.hpp"
#include "generator/cities_boundaries_builder.hpp"
#include "generator/cities_ids_builder.hpp"
#include "generator/city_roads_generator.hpp"
#include "generator/descriptions_section_builder.hpp"
#include "generator/dumper.hpp"
#include "generator/feature_builder.hpp"
#include "generator/feature_generator.hpp"
#include "generator/feature_sorter.hpp"
#include "generator/generate_info.hpp"
#include "generator/isolines_section_builder.cpp"
#include "generator/maxspeeds_builder.hpp"
#include "generator/metalines_builder.hpp"
#include "generator/osm_source.hpp"
#include "generator/platform_helpers.hpp"
#include "generator/popular_places_section_builder.hpp"
#include "generator/processor_factory.hpp"
#include "generator/ratings_section_builder.hpp"
#include "generator/raw_generator.hpp"
#include "generator/restriction_generator.hpp"
#include "generator/road_access_generator.hpp"
#include "generator/routing_index_generator.hpp"
#include "generator/search_index_builder.hpp"
#include "generator/statistics.hpp"
#include "generator/traffic_generator.hpp"
#include "generator/transit_generator.hpp"
#include "generator/translator_collection.hpp"
#include "generator/translator_factory.hpp"
#include "generator/ugc_section_builder.hpp"
#include "generator/unpack_mwm.hpp"
#include "generator/utils.hpp"
#include "generator/wiki_url_dumper.hpp"

#include "routing/cross_mwm_ids.hpp"
#include "routing/speed_camera_prohibition.hpp"

#include "indexer/classificator.hpp"
#include "indexer/classificator_loader.hpp"
#include "indexer/data_header.hpp"
#include "indexer/drawing_rules.hpp"
#include "indexer/features_offsets_table.hpp"
#include "indexer/features_vector.hpp"
#include "indexer/index_builder.hpp"
#include "indexer/map_style_reader.hpp"
#include "indexer/rank_table.hpp"

#include "storage/country_parent_getter.hpp"

#include "platform/platform.hpp"

#include "coding/endianness.hpp"
#include "coding/transliteration.hpp"

#include "base/file_name_utils.hpp"
#include "base/timer.hpp"

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "build_version.hpp"
#include "defines.hpp"

#include "3party/gflags/src/gflags/gflags.h"

using namespace std;

namespace
{
char const * GetDataPathHelp()
{
  static string const kHelp =
      "Directory where the generated mwms are put into. Also used as the path for helper "
      "functions, such as those that calculate statistics and regenerate sections. "
      "Default: " +
      Platform::GetCurrentWorkingDirectory() + "/../../data'.";
  return kHelp.c_str();
}
}  // namespace

// Coastlines.
DEFINE_bool(make_coasts, false, "Create intermediate file with coasts data.");
DEFINE_bool(fail_on_coasts, false,
            "Stop and exit with '255' code if some coastlines are not merged.");
DEFINE_bool(emit_coasts, false,
            "Push coasts features from intermediate file to out files/countries.");

// Generator settings and paths.
DEFINE_string(osm_file_name, "", "Input osm area file.");
DEFINE_string(osm_file_type, "xml", "Input osm area file type [xml, o5m].");
DEFINE_string(data_path, "", GetDataPathHelp());
DEFINE_string(user_resource_path, "", "User defined resource path for classificator.txt and etc.");
DEFINE_string(intermediate_data_path, "", "Path to stored nodes, ways, relations.");
DEFINE_string(output, "", "File name for process (without 'mwm' ext).");
DEFINE_bool(preload_cache, false, "Preload all ways and relations cache.");
DEFINE_string(node_storage, "map",
              "Type of storage for intermediate points representation. Available: raw, map, mem.");
DEFINE_uint64(planet_version, base::SecondsSinceEpoch(),
              "Version as seconds since epoch, by default - now.");

// Preprocessing and feature generator.
DEFINE_bool(preprocess, false, "1st pass - create nodes/ways/relations data.");
DEFINE_bool(generate_features, false, "2nd pass - generate intermediate features.");
DEFINE_bool(add_ads, false, "generation with ads.");
DEFINE_bool(generate_geometry, false,
            "3rd pass - split and simplify geometry and triangles for features.");
DEFINE_bool(generate_index, false, "4rd pass - generate index.");
DEFINE_bool(generate_search_index, false, "5th pass - generate search index.");
DEFINE_bool(dump_cities_boundaries, false, "Dump cities boundaries to a file");
DEFINE_bool(generate_cities_boundaries, false, "Generate the cities boundaries section");
DEFINE_string(cities_boundaries_data, "", "File with cities boundaries");

DEFINE_bool(generate_cities_ids, false, "Generate the cities ids section");

DEFINE_bool(generate_world, false, "Generate separate world file.");
DEFINE_bool(have_borders_for_whole_world, false,
            "If it is set to true, the optimization of checking that the "
            "fb belongs to the country border will be applied.");

DEFINE_string(
    nodes_list_path, "",
    "Path to file containing list of node ids we need to add to locality index. May be empty.");

DEFINE_bool(generate_isolines_info, false, "Generate the isolines info section");
DEFINE_string(isolines_path, "",
              "Path to isolines directory. If set, adds isolines linear features.");
// Routing.
DEFINE_bool(make_routing_index, false, "Make sections with the routing information.");
DEFINE_bool(make_cross_mwm, false,
            "Make section for cross mwm routing (for dynamic indexed routing).");
DEFINE_bool(make_transit_cross_mwm, false, "Make section for cross mwm transit routing.");
DEFINE_bool(disable_cross_mwm_progress, false,
            "Disable log of cross mwm section building progress.");
DEFINE_string(srtm_path, "",
              "Path to srtm directory. If set, generates a section with altitude information "
              "about roads.");
DEFINE_string(transit_path, "", "Path to directory with transit graphs in json.");
DEFINE_bool(generate_cameras, false, "Generate section with speed cameras info.");
DEFINE_bool(
    make_city_roads, false,
    "Calculates which roads lie inside cities and makes a section with ids of these roads.");
DEFINE_bool(generate_maxspeed, false, "Generate section with maxspeed of road features.");

// Sponsored-related.
DEFINE_string(booking_data, "", "Path to booking data in tsv format.");
DEFINE_string(opentable_data, "", "Path to opentable data in tsv format.");
DEFINE_string(promo_catalog_cities, "",
              "Path to list geo object ids of cities which contain promo catalog in json format.");

DEFINE_string(ugc_data, "", "Input UGC source database file name.");

DEFINE_string(wikipedia_pages, "", "Input dir with wikipedia pages.");
DEFINE_string(idToWikidata, "", "Path to file with id to wikidata mapping.");
DEFINE_string(dump_wikipedia_urls, "", "Output file with wikipedia urls.");

DEFINE_bool(generate_popular_places, false, "Generate popular places section.");
DEFINE_string(popular_places_data, "",
              "Input Popular Places source file name. Needed both for World intermediate features "
              "generation (2nd pass for World) and popular places section generation (5th pass for "
              "countries).");
DEFINE_string(brands_data, "", "Path to json with OSM objects to brand ID map.");
DEFINE_string(brands_translations_data, "", "Path to json with brands translations and synonyms.");

DEFINE_string(postcodes_dataset, "", "Path to dataset with postcodes data");

// Printing stuff.
DEFINE_bool(calc_statistics, false, "Calculate feature statistics for specified mwm bucket files.");
DEFINE_bool(type_statistics, false, "Calculate statistics by type for specified mwm bucket files.");
DEFINE_bool(dump_types, false, "Prints all types combinations and their total count.");
DEFINE_bool(dump_prefixes, false, "Prints statistics on feature's' name prefixes.");
DEFINE_bool(dump_search_tokens, false, "Print statistics on search tokens.");
DEFINE_string(dump_feature_names, "", "Print all feature names by 2-letter locale.");

// Service functions.
DEFINE_bool(generate_classif, false, "Generate classificator.");
DEFINE_bool(generate_packed_borders, false, "Generate packed file with country polygons.");
DEFINE_string(unpack_borders, "",
              "Convert packed_polygons to a directory of polygon files (specify folder).");
DEFINE_bool(unpack_mwm, false,
            "Unpack each section of mwm into a separate file with name filePath.sectionName.");
DEFINE_bool(check_mwm, false, "Check map file to be correct.");
DEFINE_string(delete_section, "", "Delete specified section (defines.hpp) from container.");
DEFINE_bool(generate_traffic_keys, false,
            "Generate keys for the traffic map (road segment -> speed group).");

DEFINE_bool(dump_mwm_tmp, false, "Prints feature builder objects from .mwm.tmp");

// Common.
DEFINE_bool(verbose, false, "Provide more detailed output.");

using namespace generator;

MAIN_WITH_ERROR_HANDLING([](int argc, char ** argv)
{
  CHECK(IsLittleEndian(), ("Only little-endian architectures are supported."));

  google::SetUsageMessage(
      "Takes OSM XML data from stdin and creates data and index files in several passes.");
  google::SetVersionString(std::to_string(omim::build_version::git::kTimestamp) + " " +
                           omim::build_version::git::kHash);
  google::ParseCommandLineFlags(&argc, &argv, true);

  Platform & pl = GetPlatform();
  auto threadsCount = pl.CpuCores();

  if (!FLAGS_user_resource_path.empty())
  {
    pl.SetResourceDir(FLAGS_user_resource_path);
    pl.SetSettingsDir(FLAGS_user_resource_path);
  }

  string const path =
      FLAGS_data_path.empty() ? pl.WritableDir() : base::AddSlashIfNeeded(FLAGS_data_path);

  // So that stray GetWritablePathForFile calls do not crash the generator.
  pl.SetWritableDirForTests(path);

  feature::GenerateInfo genInfo;
  genInfo.m_verbose = FLAGS_verbose;
  genInfo.m_intermediateDir = FLAGS_intermediate_data_path.empty()
                                  ? path
                                  : base::AddSlashIfNeeded(FLAGS_intermediate_data_path);
  genInfo.m_targetDir = genInfo.m_tmpDir = path;

  /// @todo Probably, it's better to add separate option for .mwm.tmp files.
  if (!FLAGS_intermediate_data_path.empty())
  {
    string const tmpPath = base::JoinPath(genInfo.m_intermediateDir, "tmp");
    if (Platform::MkDir(tmpPath) != Platform::ERR_UNKNOWN)
      genInfo.m_tmpDir = tmpPath;
  }
  if (!FLAGS_node_storage.empty())
    genInfo.SetNodeStorageType(FLAGS_node_storage);
  if (!FLAGS_osm_file_type.empty())
    genInfo.SetOsmFileType(FLAGS_osm_file_type);

  genInfo.m_osmFileName = FLAGS_osm_file_name;
  genInfo.m_failOnCoasts = FLAGS_fail_on_coasts;
  genInfo.m_preloadCache = FLAGS_preload_cache;
  genInfo.m_bookingDataFilename = FLAGS_booking_data;
  genInfo.m_opentableDataFilename = FLAGS_opentable_data;
  genInfo.m_promoCatalogCitiesFilename = FLAGS_promo_catalog_cities;
  genInfo.m_popularPlacesFilename = FLAGS_popular_places_data;
  genInfo.m_brandsFilename = FLAGS_brands_data;
  genInfo.m_brandsTranslationsFilename = FLAGS_brands_translations_data;
  genInfo.m_citiesBoundariesFilename = FLAGS_cities_boundaries_data;
  genInfo.m_versionDate = static_cast<uint32_t>(FLAGS_planet_version);
  genInfo.m_haveBordersForWholeWorld = FLAGS_have_borders_for_whole_world;
  genInfo.m_createWorld = FLAGS_generate_world;
  genInfo.m_makeCoasts = FLAGS_make_coasts;
  genInfo.m_emitCoasts = FLAGS_emit_coasts;
  genInfo.m_fileName = FLAGS_output;
  genInfo.m_idToWikidataFilename = FLAGS_idToWikidata;
  genInfo.m_isolinesDir = FLAGS_isolines_path;

  // Use merged style.
  GetStyleReader().SetCurrentStyle(MapStyleMerged);

  classificator::Load();

  // Generate intermediate files.
  if (FLAGS_preprocess)
  {
    LOG(LINFO, ("Generating intermediate data ...."));
    if (!GenerateIntermediateData(genInfo))
      return EXIT_FAILURE;
  }

  // Generate .mwm.tmp files.
  if (FLAGS_generate_features || FLAGS_generate_world || FLAGS_make_coasts)
  {
    RawGenerator rawGenerator(genInfo, threadsCount);
    if (FLAGS_generate_features)
      rawGenerator.GenerateCountries(FLAGS_add_ads);
    if (FLAGS_generate_world)
      rawGenerator.GenerateWorld(FLAGS_add_ads);
    if (FLAGS_make_coasts)
      rawGenerator.GenerateCoasts();

    if (!rawGenerator.Execute())
      return EXIT_FAILURE;

    genInfo.m_bucketNames = rawGenerator.GetNames();
  }

  if (genInfo.m_bucketNames.empty() && !FLAGS_output.empty())
    genInfo.m_bucketNames.push_back(FLAGS_output);

  if (FLAGS_dump_mwm_tmp)
  {
    for (auto const & fb : feature::ReadAllDatRawFormat(genInfo.GetTmpFileName(FLAGS_output)))
      std::cout << DebugPrint(fb) << std::endl;
  }

  // Load mwm tree only if we need it
  unique_ptr<storage::CountryParentGetter> countryParentGetter;
  if (FLAGS_make_routing_index || FLAGS_make_cross_mwm || FLAGS_make_transit_cross_mwm)
    countryParentGetter = make_unique<storage::CountryParentGetter>();

  if (!FLAGS_dump_wikipedia_urls.empty())
  {
    auto const tmpPath = base::JoinPath(genInfo.m_intermediateDir, "tmp");
    auto const datFiles = platform_helpers::GetFullDataTmpFilePaths(tmpPath);

    WikiUrlDumper wikiUrlDumper(FLAGS_dump_wikipedia_urls, datFiles);
    wikiUrlDumper.Dump(threadsCount);

    if (!FLAGS_idToWikidata.empty())
    {
      WikiDataFilter wikiDataFilter(FLAGS_idToWikidata, datFiles);
      wikiDataFilter.Filter(threadsCount);
    }
  }

  // Enumerate over all dat files that were created.
  size_t const count = genInfo.m_bucketNames.size();
  for (size_t i = 0; i < count; ++i)
  {
    string const & country = genInfo.m_bucketNames[i];
    string const datFile = base::JoinPath(path, country + DATA_FILE_EXTENSION);
    string const osmToFeatureFilename =
        genInfo.GetTargetFileName(country) + OSM2FEATURE_FILE_EXTENSION;

    if (FLAGS_generate_geometry)
    {
      using MapType = feature::DataHeader::MapType;

      MapType mapType = MapType::Country;
      if (country == WORLD_FILE_NAME)
        mapType = MapType::World;
      if (country == WORLD_COASTS_FILE_NAME)
        mapType = MapType::WorldCoasts;

      // On error move to the next bucket without index generation.

      LOG(LINFO, ("Generating result features for", country));
      if (!feature::GenerateFinalFeatures(genInfo, country, mapType))
        continue;

      LOG(LINFO, ("Generating offsets table for", datFile));
      if (!feature::BuildOffsetsTable(datFile))
        continue;

      if (mapType == MapType::Country)
      {
        string const metalinesFilename = genInfo.GetIntermediateFileName(METALINES_FILENAME);

        LOG(LINFO, ("Processing metalines from", metalinesFilename));
        if (!feature::WriteMetalinesSection(datFile, metalinesFilename, osmToFeatureFilename))
          LOG(LCRITICAL, ("Error generating metalines section."));
      }
    }

    if (FLAGS_generate_index)
    {
      LOG(LINFO, ("Generating index for", datFile));

      if (!indexer::BuildIndexFromDataFile(datFile, FLAGS_intermediate_data_path + country))
        LOG(LCRITICAL, ("Error generating index."));
    }

    if (FLAGS_generate_search_index)
    {
      LOG(LINFO, ("Generating search index for", datFile));

      /// @todo Make threads count according to environment (single mwm build or planet build).
      if (!indexer::BuildSearchIndexFromDataFile(datFile, true /* forceRebuild */,
                                                 1 /* threadsCount */))
      {
        LOG(LCRITICAL, ("Error generating search index."));
      }

      if (!FLAGS_postcodes_dataset.empty())
      {
        if (!indexer::BuildPostcodes(path, country, FLAGS_postcodes_dataset, true /*forceRebuild*/))
          LOG(LCRITICAL, ("Error generating postcodes section."));
      }

      LOG(LINFO, ("Generating rank table for", datFile));
      if (!search::SearchRankTableBuilder::CreateIfNotExists(datFile))
        LOG(LCRITICAL, ("Error generating rank table."));

      LOG(LINFO, ("Generating centers table for", datFile));
      if (!indexer::BuildCentersTableFromDataFile(datFile, true /* forceRebuild */))
        LOG(LCRITICAL, ("Error generating centers table."));
    }

    if (FLAGS_generate_cities_boundaries)
    {
      CHECK(!FLAGS_cities_boundaries_data.empty(), ());
      LOG(LINFO, ("Generating cities boundaries for", datFile));
      generator::OsmIdToBoundariesTable table;
      if (!generator::DeserializeBoundariesTable(FLAGS_cities_boundaries_data, table))
        LOG(LCRITICAL, ("Error deserializing boundaries table"));
      if (!generator::BuildCitiesBoundaries(datFile, osmToFeatureFilename, table))
        LOG(LCRITICAL, ("Error generating cities boundaries."));
    }

    if (FLAGS_generate_cities_ids)
    {
      LOG(LINFO, ("Generating cities ids for", datFile));
      if (!generator::BuildCitiesIds(datFile, osmToFeatureFilename))
        LOG(LCRITICAL, ("Error generating cities ids."));
    }

    if (!FLAGS_srtm_path.empty())
      routing::BuildRoadAltitudes(datFile, FLAGS_srtm_path);

    if (!FLAGS_transit_path.empty())
      routing::transit::BuildTransit(path, country, osmToFeatureFilename, FLAGS_transit_path);

    if (FLAGS_generate_cameras)
    {
      if (routing::AreSpeedCamerasProhibited(platform::CountryFile(country)))
      {
        LOG(LINFO,
            ("Cameras info is prohibited for", country, "and speedcams section is not generated."));
      }
      else
      {
        string const camerasFilename = genInfo.GetIntermediateFileName(CAMERAS_TO_WAYS_FILENAME);

        BuildCamerasInfo(datFile, camerasFilename, osmToFeatureFilename);
      }
    }

    if (FLAGS_make_routing_index)
    {
      if (!countryParentGetter)
      {
        // All the mwms should use proper VehicleModels.
        LOG(LCRITICAL,
            ("Countries file is needed. Please set countries file name (countries.txt). "
             "File must be located in data directory."));
        return EXIT_FAILURE;
      }

      string const restrictionsFilename = genInfo.GetIntermediateFileName(RESTRICTIONS_FILENAME);
      string const roadAccessFilename = genInfo.GetIntermediateFileName(ROAD_ACCESS_FILENAME);

      routing::BuildRoutingIndex(datFile, country, *countryParentGetter);
      routing::BuildRoadRestrictions(path, datFile, country, restrictionsFilename,
                                     osmToFeatureFilename, *countryParentGetter);
      routing::BuildRoadAccessInfo(datFile, roadAccessFilename, osmToFeatureFilename);
    }

    if (FLAGS_make_city_roads)
    {
      CHECK(!FLAGS_cities_boundaries_data.empty(), ());
      LOG(LINFO, ("Generating cities boundaries roads for", datFile));
      auto const boundariesPath =
          genInfo.GetIntermediateFileName(ROUTING_CITY_BOUNDARIES_DUMP_FILENAME);
      if (!routing::BuildCityRoads(datFile, boundariesPath))
        LOG(LCRITICAL, ("Generating city roads error."));
    }

    if (FLAGS_generate_maxspeed)
    {
      LOG(LINFO, ("Generating maxspeeds section for", datFile));
      string const maxspeedsFilename = genInfo.GetIntermediateFileName(MAXSPEEDS_FILENAME);
      routing::BuildMaxspeedsSection(datFile, osmToFeatureFilename, maxspeedsFilename);
    }

    if (FLAGS_make_cross_mwm || FLAGS_make_transit_cross_mwm)
    {
      if (!countryParentGetter)
      {
        // All the mwms should use proper VehicleModels.
        LOG(LCRITICAL,
            ("Countries file is needed. Please set countries file name (countries.txt). "
             "File must be located in data directory."));
        return EXIT_FAILURE;
      }

      if (FLAGS_make_cross_mwm)
      {
        routing::BuildRoutingCrossMwmSection(path, datFile, country, genInfo.m_intermediateDir,
                                             *countryParentGetter, osmToFeatureFilename,
                                             FLAGS_disable_cross_mwm_progress);
      }

      if (FLAGS_make_transit_cross_mwm)
        routing::BuildTransitCrossMwmSection(path, datFile, country, *countryParentGetter);
    }

    if (!FLAGS_ugc_data.empty())
    {
      if (!BuildUgcMwmSection(FLAGS_ugc_data, datFile, osmToFeatureFilename))
        LOG(LCRITICAL, ("Error generating UGC mwm section."));

      if (!BuildRatingsMwmSection(FLAGS_ugc_data, datFile, osmToFeatureFilename))
        LOG(LCRITICAL, ("Error generating ratings mwm section."));
    }

    if (!FLAGS_wikipedia_pages.empty())
    {
      if (!FLAGS_idToWikidata.empty())
        BuildDescriptionsSection(FLAGS_wikipedia_pages, datFile, FLAGS_idToWikidata);
      else
        BuildDescriptionsSection(FLAGS_wikipedia_pages, datFile);
    }

    if (FLAGS_generate_isolines_info)
      BuildIsolinesInfoSection(FLAGS_isolines_path, country, datFile);

    if (FLAGS_generate_popular_places)
    {
      if (!BuildPopularPlacesMwmSection(genInfo.m_popularPlacesFilename, datFile,
                                        osmToFeatureFilename))
      {
        LOG(LCRITICAL, ("Error generating popular places mwm section."));
      }
    }

    if (FLAGS_generate_traffic_keys)
    {
      if (!traffic::GenerateTrafficKeysFromDataFile(datFile))
        LOG(LCRITICAL, ("Error generating traffic keys."));
    }
  }

  string const datFile = base::JoinPath(path, FLAGS_output + DATA_FILE_EXTENSION);

  if (FLAGS_calc_statistics)
  {
    LOG(LINFO, ("Calculating statistics for", datFile));

    stats::FileContainerStatistic(datFile);
    stats::FileContainerStatistic(datFile + ROUTING_FILE_EXTENSION);

    stats::MapInfo info;
    stats::CalcStatistic(datFile, info);
    stats::PrintStatistic(info);
  }

  if (FLAGS_type_statistics)
  {
    LOG(LINFO, ("Calculating type statistics for", datFile));

    stats::MapInfo info;
    stats::CalcStatistic(datFile, info);
    stats::PrintTypeStatistic(info);
  }

  if (FLAGS_dump_types)
    feature::DumpTypes(datFile);

  if (FLAGS_dump_prefixes)
    feature::DumpPrefixes(datFile);

  if (FLAGS_dump_search_tokens)
    feature::DumpSearchTokens(datFile, 100 /* maxTokensToShow */);

  if (FLAGS_dump_feature_names != "")
    feature::DumpFeatureNames(datFile, FLAGS_dump_feature_names);

  if (FLAGS_unpack_mwm)
    UnpackMwm(datFile);

  if (!FLAGS_delete_section.empty())
    DeleteSection(datFile, FLAGS_delete_section);

  if (FLAGS_generate_packed_borders)
    borders::GeneratePackedBorders(path);

  if (!FLAGS_unpack_borders.empty())
    borders::UnpackBorders(path, FLAGS_unpack_borders);

  if (FLAGS_check_mwm)
    check_model::ReadFeatures(datFile);

  return EXIT_SUCCESS;
});
