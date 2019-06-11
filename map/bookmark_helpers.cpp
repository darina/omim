#include "map/bookmark_helpers.hpp"
#include "map/user.hpp"

#include "kml/serdes.hpp"
#include "kml/serdes_binary.hpp"

#include "indexer/classificator.hpp"
#include "indexer/feature_utils.hpp"

#include "platform/localization.hpp"
#include "platform/platform.hpp"
#include "platform/preferred_languages.hpp"

#include "coding/file_reader.hpp"
#include "coding/file_writer.hpp"
#include "coding/sha1.hpp"
#include "coding/zip_reader.hpp"

#include "base/file_name_utils.hpp"
#include "base/scope_guard.hpp"
#include "base/string_utils.hpp"

#include <map>
#include <sstream>

namespace
{
std::map<std::string, kml::BookmarkType> const kFeatureTypeToBookmarkType = {
  {"amenity-bbq", kml::BookmarkType::Food},
  {"tourism-picnic_site", kml::BookmarkType::Food},
  {"leisure-picnic_table", kml::BookmarkType::Food},
  {"amenity-cafe", kml::BookmarkType::Food},
  {"amenity-restaurant", kml::BookmarkType::Food},
  {"amenity-fast_food", kml::BookmarkType::Food},
  {"amenity-food_court", kml::BookmarkType::Food},
  {"amenity-bar", kml::BookmarkType::Food},
  {"amenity-pub", kml::BookmarkType::Food},
  {"amenity-biergarten", kml::BookmarkType::Food},

  {"waterway-waterfall", kml::BookmarkType::Sights},
  {"historic-tomb", kml::BookmarkType::Sights},
  {"historic-boundary_stone", kml::BookmarkType::Sights},
  {"historic-ship", kml::BookmarkType::Sights},
  {"historic-archaeological_site", kml::BookmarkType::Sights},
  {"historic-monument", kml::BookmarkType::Sights},
  {"historic-memorial", kml::BookmarkType::Sights},
  {"amenity-place_of_worship", kml::BookmarkType::Sights},
  {"tourism-attraction", kml::BookmarkType::Sights},
  {"tourism-theme_park", kml::BookmarkType::Sights},
  {"tourism-viewpoint", kml::BookmarkType::Sights},
  {"historic-fort", kml::BookmarkType::Sights},
  {"historic-castle", kml::BookmarkType::Sights},
  {"tourism-artwork", kml::BookmarkType::Sights},
  {"historic-ruins", kml::BookmarkType::Sights},
  {"historic-wayside_shrine", kml::BookmarkType::Sights},
  {"historic-wayside_cross", kml::BookmarkType::Sights},

  {"tourism-gallery", kml::BookmarkType::Museum},
  {"tourism-museum", kml::BookmarkType::Museum},
  {"amenity-arts_centre", kml::BookmarkType::Museum},

  {"sport", kml::BookmarkType::Entertainment},
  {"sport-multi", kml::BookmarkType::Entertainment},
  {"leisure-playground", kml::BookmarkType::Entertainment},
  {"leisure-water_park", kml::BookmarkType::Entertainment},
  {"amenity-casino", kml::BookmarkType::Entertainment},
  {"sport-archery", kml::BookmarkType::Entertainment},
  {"sport-shooting", kml::BookmarkType::Entertainment},
  {"sport-australian_football", kml::BookmarkType::Entertainment},
  {"sport-bowls", kml::BookmarkType::Entertainment},
  {"sport-curling", kml::BookmarkType::Entertainment},
  {"sport-cricket", kml::BookmarkType::Entertainment},
  {"sport-baseball", kml::BookmarkType::Entertainment},
  {"sport-basketball", kml::BookmarkType::Entertainment},
  {"sport-american_football", kml::BookmarkType::Entertainment},
  {"sport-athletics", kml::BookmarkType::Entertainment},
  {"sport-golf", kml::BookmarkType::Entertainment},
  {"sport-gymnastics", kml::BookmarkType::Entertainment},
  {"sport-tennis", kml::BookmarkType::Entertainment},
  {"sport-skiing", kml::BookmarkType::Entertainment},
  {"sport-soccer", kml::BookmarkType::Entertainment},
  {"amenity-nightclub", kml::BookmarkType::Entertainment},
  {"amenity-cinema", kml::BookmarkType::Entertainment},
  {"amenity-theatre", kml::BookmarkType::Entertainment},
  {"leisure-stadium", kml::BookmarkType::Entertainment},

  {"boundary-national_park", kml::BookmarkType::Park},
  {"leisure-nature_reserve", kml::BookmarkType::Park},
  {"landuse-forest", kml::BookmarkType::Park},

  {"natural-beach", kml::BookmarkType::Swim},
  {"sport-diving", kml::BookmarkType::Swim},
  {"sport-scuba_diving", kml::BookmarkType::Swim},
  {"sport-swimming", kml::BookmarkType::Swim},
  {"leisure-swimming_pool", kml::BookmarkType::Swim},

  {"natural-cave_entrance", kml::BookmarkType::Mountain},
  {"natural-peak", kml::BookmarkType::Mountain},
  {"natural-volcano", kml::BookmarkType::Mountain},
  {"natural-rock", kml::BookmarkType::Mountain},
  {"natural-bare_rock", kml::BookmarkType::Mountain},

  {"amenity-veterinary", kml::BookmarkType::Animals},
  {"leisure-dog_park", kml::BookmarkType::Animals},
  {"tourism-zoo", kml::BookmarkType::Animals},

  {"tourism-apartment", kml::BookmarkType::Hotel},
  {"tourism-chalet", kml::BookmarkType::Hotel},
  {"tourism-guest_house", kml::BookmarkType::Hotel},
  {"tourism-alpine_hut", kml::BookmarkType::Hotel},
  {"tourism-wilderness_hut", kml::BookmarkType::Hotel},
  {"tourism-hostel", kml::BookmarkType::Hotel},
  {"tourism-motel", kml::BookmarkType::Hotel},
  {"tourism-resort", kml::BookmarkType::Hotel},
  {"tourism-hotel", kml::BookmarkType::Hotel},
  {"sponsored-booking", kml::BookmarkType::Hotel},

  {"amenity-kindergarten", kml::BookmarkType::Building},
  {"amenity-school", kml::BookmarkType::Building},
  {"office", kml::BookmarkType::Building},
  {"amenity-library", kml::BookmarkType::Building},
  {"amenity-courthouse", kml::BookmarkType::Building},
  {"amenity-college", kml::BookmarkType::Building},
  {"amenity-police", kml::BookmarkType::Building},
  {"amenity-prison", kml::BookmarkType::Building},
  {"amenity-embassy", kml::BookmarkType::Building},
  {"office-lawyer", kml::BookmarkType::Building},
  {"building-train_station", kml::BookmarkType::Building},
  {"building-university", kml::BookmarkType::Building},


  {"amenity-atm", kml::BookmarkType::Exchange},
  {"amenity-bureau_de_change", kml::BookmarkType::Exchange},
  {"amenity-bank", kml::BookmarkType::Exchange},

  {"amenity-vending_machine", kml::BookmarkType::Shop},
  {"shop", kml::BookmarkType::Shop},
  {"amenity-marketplace", kml::BookmarkType::Shop},
  {"craft", kml::BookmarkType::Shop},
  {"shop-pawnbroker", kml::BookmarkType::Shop},
  {"shop-supermarket", kml::BookmarkType::Shop},
  {"shop-car_repair", kml::BookmarkType::Shop},
  {"shop-mall", kml::BookmarkType::Shop},
  {"highway-services", kml::BookmarkType::Shop},
  {"shop-stationery", kml::BookmarkType::Shop},
  {"shop-variety_store", kml::BookmarkType::Shop},
  {"shop-alcohol", kml::BookmarkType::Shop},
  {"shop-wine", kml::BookmarkType::Shop},
  {"shop-books", kml::BookmarkType::Shop},
  {"shop-bookmaker", kml::BookmarkType::Shop},
  {"shop-bakery", kml::BookmarkType::Shop},
  {"shop-beauty", kml::BookmarkType::Shop},
  {"shop-cosmetics", kml::BookmarkType::Shop},
  {"shop-beverages", kml::BookmarkType::Shop},
  {"shop-bicycle", kml::BookmarkType::Shop},
  {"shop-butcher", kml::BookmarkType::Shop},
  {"shop-car", kml::BookmarkType::Shop},
  {"shop-motorcycle", kml::BookmarkType::Shop},
  {"shop-car_parts", kml::BookmarkType::Shop},
  {"shop-car_repair", kml::BookmarkType::Shop},
  {"shop-tyres", kml::BookmarkType::Shop},
  {"shop-chemist", kml::BookmarkType::Shop},
  {"shop-clothes", kml::BookmarkType::Shop},
  {"shop-computer", kml::BookmarkType::Shop},
  {"shop-tattoo", kml::BookmarkType::Shop},
  {"shop-erotic", kml::BookmarkType::Shop},
  {"shop-funeral_directors", kml::BookmarkType::Shop},
  {"shop-confectionery", kml::BookmarkType::Shop},
  {"shop-chocolate", kml::BookmarkType::Shop},
  {"amenity-ice_cream", kml::BookmarkType::Shop},
  {"shop-convenience", kml::BookmarkType::Shop},
  {"shop-copyshop", kml::BookmarkType::Shop},
  {"shop-photo", kml::BookmarkType::Shop},
  {"shop-pet", kml::BookmarkType::Shop},
  {"shop-department_store", kml::BookmarkType::Shop},
  {"shop-doityourself", kml::BookmarkType::Shop},
  {"shop-electronics", kml::BookmarkType::Shop},
  {"shop-florist", kml::BookmarkType::Shop},
  {"shop-furniture", kml::BookmarkType::Shop},
  {"shop-garden_centre", kml::BookmarkType::Shop},
  {"shop-gift", kml::BookmarkType::Shop},
  {"shop-music", kml::BookmarkType::Shop},
  {"shop-video", kml::BookmarkType::Shop},
  {"shop-musical_instrument", kml::BookmarkType::Shop},
  {"shop-greengrocer", kml::BookmarkType::Shop},
  {"shop-hairdresser", kml::BookmarkType::Shop},
  {"shop-hardware", kml::BookmarkType::Shop},
  {"shop-jewelry", kml::BookmarkType::Shop},
  {"shop-kiosk", kml::BookmarkType::Shop},
  {"shop-laundry", kml::BookmarkType::Shop},
  {"shop-dry_cleaning", kml::BookmarkType::Shop},
  {"shop-mobile_phone", kml::BookmarkType::Shop},
  {"shop-optician", kml::BookmarkType::Shop},
  {"shop-outdoor", kml::BookmarkType::Shop},
  {"shop-seafood", kml::BookmarkType::Shop},
  {"shop-shoes", kml::BookmarkType::Shop},
  {"shop-sports", kml::BookmarkType::Shop},
  {"shop-ticket", kml::BookmarkType::Shop},
  {"shop-toys", kml::BookmarkType::Shop},
  {"shop-fabric", kml::BookmarkType::Shop},
  {"shop-alcohol", kml::BookmarkType::Shop},

  {"amenity-place_of_worship-christian", kml::BookmarkType::Christianity},
  {"landuse-cemetery-christian", kml::BookmarkType::Christianity},
  {"amenity-grave_yard-christian", kml::BookmarkType::Christianity},

  {"amenity-place_of_worship-jewish", kml::BookmarkType::Judaism},

  {"amenity-place_of_worship-buddhist", kml::BookmarkType::Buddhism},

  {"amenity-place_of_worship-muslim", kml::BookmarkType::Islam},

  {"amenity-parking", kml::BookmarkType::Parking},
  {"vending-parking_tickets", kml::BookmarkType::Parking},
  {"tourism-caravan_site", kml::BookmarkType::Parking},
  {"amenity-bicycle_parking", kml::BookmarkType::Parking},
  {"amenity-taxi", kml::BookmarkType::Parking},
  {"amenity-car_sharing", kml::BookmarkType::Parking},
  {"tourism-camp_site", kml::BookmarkType::Parking},
  {"amenity-motorcycle_parking", kml::BookmarkType::Parking},

  {"amenity-fuel", kml::BookmarkType::Gas},
  {"amenity-charging_station", kml::BookmarkType::Gas},

  {"amenity-fountain", kml::BookmarkType::Water},
  {"natural-spring", kml::BookmarkType::Water},
  {"amenity-drinking_water", kml::BookmarkType::Water},
  {"man_made-water_tap", kml::BookmarkType::Water},
  {"amenity-water_point", kml::BookmarkType::Water},

  {"amenity-dentist", kml::BookmarkType::Medicine},
  {"amenity-clinic", kml::BookmarkType::Medicine},
  {"amenity-doctors", kml::BookmarkType::Medicine},
  {"amenity-hospital", kml::BookmarkType::Medicine},
  {"amenity-pharmacy", kml::BookmarkType::Medicine},
  {"amenity-clinic", kml::BookmarkType::Medicine},
  {"amenity-childcare", kml::BookmarkType::Medicine},
  {"emergency-defibrillator", kml::BookmarkType::Medicine}
};

void ValidateKmlData(std::unique_ptr<kml::FileData> & data)
{
  if (!data)
    return;

  for (auto & t : data->m_tracksData)
  {
    if (t.m_layers.empty())
      t.m_layers.emplace_back(kml::KmlParser::GetDefaultTrackLayer());
  }
}
}  // namespace

std::string const kKmzExtension = ".kmz";
std::string const kKmlExtension = ".kml";
std::string const kKmbExtension = ".kmb";

std::unique_ptr<kml::FileData> LoadKmlFile(std::string const & file, KmlFileType fileType)
{
  std::unique_ptr<kml::FileData> kmlData;
  try
  {
    kmlData = LoadKmlData(FileReader(file), fileType);
  }
  catch (std::exception const & e)
  {
    LOG(LWARNING, ("KML", fileType, "loading failure:", e.what()));
    kmlData.reset();
  }
  if (kmlData == nullptr)
    LOG(LWARNING, ("Loading bookmarks failed, file", file));
  return kmlData;
}

std::unique_ptr<kml::FileData> LoadKmzFile(std::string const & file, std::string & kmlHash)
{
  std::string unarchievedPath;
  try
  {
    ZipFileReader::FileList files;
    ZipFileReader::FilesList(file, files);
    if (files.empty())
      return nullptr;

    auto fileName = files.front().first;
    for (auto const & f : files)
    {
      if (strings::MakeLowerCase(base::GetFileExtension(f.first)) == kKmlExtension)
      {
        fileName = f.first;
        break;
      }
    }
    unarchievedPath = file + ".raw";
    ZipFileReader::UnzipFile(file, fileName, unarchievedPath);
  }
  catch (ZipFileReader::OpenException const & ex)
  {
    LOG(LWARNING, ("Could not open zip file", ex.what()));
    return nullptr;
  }
  catch (std::exception const & ex)
  {
    LOG(LWARNING, ("Unexpected exception on openning zip file", ex.what()));
    return nullptr;
  }

  if (!GetPlatform().IsFileExistsByFullPath(unarchievedPath))
    return nullptr;

  SCOPE_GUARD(fileGuard, std::bind(&FileWriter::DeleteFileX, unarchievedPath));

  kmlHash = coding::SHA1::CalculateBase64(unarchievedPath);
  return LoadKmlFile(unarchievedPath, KmlFileType::Text);
}

std::unique_ptr<kml::FileData> LoadKmlData(Reader const & reader, KmlFileType fileType)
{
  auto data = std::make_unique<kml::FileData>();
  try
  {
    if (fileType == KmlFileType::Binary)
    {
      kml::binary::DeserializerKml des(*data);
      des.Deserialize(reader);
    }
    else if (fileType == KmlFileType::Text)
    {
      kml::DeserializerKml des(*data);
      des.Deserialize(reader);
    }
    else
    {
      CHECK(false, ("Not supported KmlFileType"));
    }
    ValidateKmlData(data);
  }
  catch (Reader::Exception const & e)
  {
    LOG(LWARNING, ("KML", fileType, "reading failure:", e.what()));
    return nullptr;
  }
  catch (kml::binary::DeserializerKml::DeserializeException const & e)
  {
    LOG(LWARNING, ("KML", fileType, "deserialization failure:", e.what()));
    return nullptr;
  }
  catch (kml::DeserializerKml::DeserializeException const & e)
  {
    LOG(LWARNING, ("KML", fileType, "deserialization failure:", e.what()));
    return nullptr;
  }
  catch (std::exception const & e)
  {
    LOG(LWARNING, ("KML", fileType, "loading failure:", e.what()));
    return nullptr;
  }
  return data;
}

bool SaveKmlFile(kml::FileData & kmlData, std::string const & file, KmlFileType fileType)
{
  bool success;
  try
  {
    FileWriter writer(file);
    success = SaveKmlData(kmlData, writer, fileType);
  }
  catch (std::exception const & e)
  {
    LOG(LWARNING, ("KML", fileType, "saving failure:", e.what()));
    success = false;
  }
  if (!success)
    LOG(LWARNING, ("Saving bookmarks failed, file", file));
  return success;
}

bool SaveKmlData(kml::FileData & kmlData, Writer & writer, KmlFileType fileType)
{
  try
  {
    if (fileType == KmlFileType::Binary)
    {
      kml::binary::SerializerKml ser(kmlData);
      ser.Serialize(writer);
    }
    else if (fileType == KmlFileType::Text)
    {
      kml::SerializerKml ser(kmlData);
      ser.Serialize(writer);
    }
    else
    {
      CHECK(false, ("Not supported KmlFileType"));
    }
  }
  catch (Writer::Exception const & e)
  {
    LOG(LWARNING, ("KML", fileType, "writing failure:", e.what()));
    return false;
  }
  catch (std::exception const & e)
  {
    LOG(LWARNING, ("KML", fileType, "serialization failure:", e.what()));
    return false;
  }
  return true;
}

void ResetIds(kml::FileData & kmlData)
{
  kmlData.m_categoryData.m_id = kml::kInvalidMarkGroupId;
  for (auto & bmData : kmlData.m_bookmarksData)
    bmData.m_id = kml::kInvalidMarkId;
  for (auto & trackData : kmlData.m_tracksData)
    trackData.m_id = kml::kInvalidTrackId;
}

kml::BookmarkType GetBookmarkTypeByFeatureType(uint32_t type)
{
  auto const typeStr = classif().GetReadableObjectName(type);
  
  static std::string const kDelim = "-";
  std::vector<std::string> v;
  strings::Tokenize(typeStr, kDelim.c_str(), [&v] (std::string const & s) {v.push_back(s);});
  for (size_t sz = v.size(); sz > 0; sz--)
  {
    std::stringstream ss;
    for (size_t i = 0; i < sz; i++)
    {
      ss << v[i];
      if (i + 1 < sz)
        ss << kDelim;
    }
    auto const itType = kFeatureTypeToBookmarkType.find(ss.str());
    if (itType != kFeatureTypeToBookmarkType.cend())
      return itType->second;
  }
  return kml::BookmarkType::None;
}

void SaveFeatureTypes(feature::TypesHolder const & types, kml::BookmarkData & bmData)
{
  auto const & c = classif();
  feature::TypesHolder copy(types);
  copy.SortBySpec();
  bmData.m_featureTypes.reserve(copy.Size());
  for (auto it = copy.begin(); it != copy.end(); ++it)
  {
    bmData.m_featureTypes.push_back(c.GetIndexForType(*it));
    if (bmData.m_icon == kml::BookmarkType::None)
      bmData.m_icon = GetBookmarkTypeByFeatureType(*it);
  }
}

std::string GetPreferredBookmarkStr(kml::LocalizableString const & name)
{
  return kml::GetPreferredBookmarkStr(name, languages::GetCurrentNorm());
}

std::string GetPreferredBookmarkStr(kml::LocalizableString const & name, feature::RegionData const & regionData)
{
  return kml::GetPreferredBookmarkStr(name, regionData, languages::GetCurrentNorm());
}

std::string GetLocalizedFeatureType(std::vector<uint32_t> const & types)
{
  return kml::GetLocalizedFeatureType(types);
}

std::string GetLocalizedBookmarkType(kml::BookmarkType type)
{
  switch (type)
  {
  case kml::BookmarkType::None: return platform::GetLocalizedString("None");
  case kml::BookmarkType::Hotel: return platform::GetLocalizedString("Hotel");
  case kml::BookmarkType::Animals: return platform::GetLocalizedString("Animals");
  case kml::BookmarkType::Buddhism: return platform::GetLocalizedString("Buddhism");
  case kml::BookmarkType::Building: return platform::GetLocalizedString("Building");
  case kml::BookmarkType::Christianity: return platform::GetLocalizedString("Christianity");
  case kml::BookmarkType::Entertainment: return platform::GetLocalizedString("Entertainment");
  case kml::BookmarkType::Exchange: return platform::GetLocalizedString("Exchange");
  case kml::BookmarkType::Food: return platform::GetLocalizedString("Food");
  case kml::BookmarkType::Gas: return platform::GetLocalizedString("Gas");
  case kml::BookmarkType::Judaism: return platform::GetLocalizedString("Judaism");
  case kml::BookmarkType::Medicine: return platform::GetLocalizedString("Medicine");
  case kml::BookmarkType::Mountain: return platform::GetLocalizedString("Mountain");
  case kml::BookmarkType::Museum: return platform::GetLocalizedString("Museum");
  case kml::BookmarkType::Islam: return platform::GetLocalizedString("Islam");
  case kml::BookmarkType::Park: return platform::GetLocalizedString("Park");
  case kml::BookmarkType::Parking: return platform::GetLocalizedString("Parking");
  case kml::BookmarkType::Shop: return platform::GetLocalizedString("Shop");
  case kml::BookmarkType::Sights: return platform::GetLocalizedString("Sights");
  case kml::BookmarkType::Swim: return platform::GetLocalizedString("Swim");
  case kml::BookmarkType::Water: return platform::GetLocalizedString("Water");
  case kml::BookmarkType::Count: CHECK(false, ("Invalid bookmark type")); return "";
  }
  UNREACHABLE();
}

std::string GetPreferredBookmarkName(kml::BookmarkData const & bmData)
{
  return kml::GetPreferredBookmarkName(bmData, languages::GetCurrentOrig());
}

bool FromCatalog(kml::FileData const & kmlData)
{
  return FromCatalog(kmlData.m_categoryData, kmlData.m_serverId);
}

bool FromCatalog(kml::CategoryData const & categoryData, std::string const & serverId)
{
  return !serverId.empty() && categoryData.m_accessRules != kml::AccessRules::Local;
}

bool IsMyCategory(std::string const & userId, kml::CategoryData const & categoryData)
{
  return userId == categoryData.m_authorId;
}

bool IsMyCategory(User const & user, kml::CategoryData const & categoryData)
{
  return IsMyCategory(user.GetUserId(), categoryData);
}

void ExpandBookmarksRectForPreview(m2::RectD & rect)
{
  if (!rect.IsValid())
    return;

  double const kPaddingScale = 1.2;
  rect.Scale(kPaddingScale);
}
