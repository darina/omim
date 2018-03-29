#include "testing/testing.hpp"

#include "drape_frontend/visual_params.hpp"

#include "indexer/data_header.hpp"

#include "map/framework.hpp"

#include "search/result.hpp"

#include "platform/platform.hpp"
#include "platform/preferred_languages.hpp"

#include "coding/internal/file_data.hpp"

#include <array>
#include <fstream>
#include <memory>
#include <set>
#include <vector>

using namespace std;

namespace
{
using Runner = Platform::ThreadRunner;

static FrameworkParams const kFrameworkParams(false /* m_enableLocalAds */, false /* m_enableDiffs */);

char const * kmlString =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.2\">"
    "<Document>"
      "<name>MapName</name>"
      "<description><![CDATA[MapDescription]]></description>"
      "<visibility>0</visibility>"
      "<Style id=\"placemark-blue\">"
        "<IconStyle>"
          "<Icon>"
            "<href>http://www.mapswithme.com/placemarks/placemark-blue.png</href>"
          "</Icon>"
        "</IconStyle>"
      "</Style>"
      "<Style id=\"placemark-brown\">"
        "<IconStyle>"
          "<Icon>"
            "<href>http://www.mapswithme.com/placemarks/placemark-brown.png</href>"
          "</Icon>"
        "</IconStyle>"
      "</Style>"
      "<Style id=\"placemark-green\">"
        "<IconStyle>"
          "<Icon>"
            "<href>http://www.mapswithme.com/placemarks/placemark-green.png</href>"
          "</Icon>"
        "</IconStyle>"
      "</Style>"
      "<Style id=\"placemark-orange\">"
        "<IconStyle>"
          "<Icon>"
            "<href>http://www.mapswithme.com/placemarks/placemark-orange.png</href>"
          "</Icon>"
        "</IconStyle>"
      "</Style>"
      "<Style id=\"placemark-pink\">"
        "<IconStyle>"
          "<Icon>"
            "<href>http://www.mapswithme.com/placemarks/placemark-pink.png</href>"
          "</Icon>"
        "</IconStyle>"
      "</Style>"
      "<Style id=\"placemark-purple\">"
        "<IconStyle>"
          "<Icon>"
            "<href>http://www.mapswithme.com/placemarks/placemark-purple.png</href>"
          "</Icon>"
        "</IconStyle>"
      "</Style>"
      "<Style id=\"placemark-red\">"
        "<IconStyle>"
          "<Icon>"
            "<href>http://www.mapswithme.com/placemarks/placemark-red.png</href>"
          "</Icon>"
        "</IconStyle>"
      "</Style>"
      "<Placemark>"
        "<name>Nebraska</name>"
        "<description><![CDATA[]]></description>"
        "<styleUrl>#placemark-red</styleUrl>"
        "<Point>"
          "<coordinates>-99.901810,41.492538,0.000000</coordinates>"
        "</Point>"
      "</Placemark>"
      "<Placemark>"
        "<name>Monongahela National Forest</name>"
        "<description><![CDATA[Huttonsville, WV 26273<br>]]></description>"
        "<styleUrl>#placemark-pink</styleUrl>"
        "<TimeStamp>"
          "<when>1986-08-12T07:10:43Z</when>"
        "</TimeStamp>"
        "<Point>"
          "<coordinates>-79.829674,38.627785,0.000000</coordinates>"
        "</Point>"
      "</Placemark>"
      "<Placemark>"
        "<name>From: Минск, Минская область, Беларусь</name>"
        "<description><![CDATA[]]></description>"
        "<styleUrl>#placemark-blue</styleUrl>"
        "<TimeStamp>"
          "<when>1998-03-03T03:04:48+01:30</when>"
        "</TimeStamp>"
        "<Point>"
          "<coordinates>27.566765,53.900047,0</coordinates>"
        "</Point>"
      "</Placemark>"
      "<Placemark>"
        "<name><![CDATA[<MWM & Sons>]]></name>"
        "<description><![CDATA[Amps & <brackets>]]></description>"
        "<styleUrl>#placemark-green</styleUrl>"
        "<TimeStamp>"
          "<when>2048 bytes in two kilobytes - some invalid timestamp</when>"
        "</TimeStamp>"
        "<Point>"
          "<coordinates>27.551532,53.89306</coordinates>"
        "</Point>"
      "</Placemark>"
    "</Document>"
    "</kml>";

BookmarkManager::Callbacks const bmCallbacks(
  []()
  {
    static StringsBundle dummyBundle;
    return dummyBundle;
  },
  static_cast<BookmarkManager::Callbacks::CreatedBookmarksCallback>(nullptr),
  static_cast<BookmarkManager::Callbacks::UpdatedBookmarksCallback>(nullptr),
  static_cast<BookmarkManager::Callbacks::DeletedBookmarksCallback>(nullptr));

void CheckBookmarks(BookmarkManager const & bmManager, df::MarkGroupID groupId)
{
  auto const & markIds = bmManager.GetUserMarkIds(groupId);
  TEST_EQUAL(markIds.size(), 4, ());

  auto it = markIds.rbegin();
  Bookmark const * bm = bmManager.GetBookmark(*it++);
  TEST_EQUAL(bm->GetName(), "Nebraska", ());
  TEST_EQUAL(bm->GetColor(), kml::PredefinedColor::Red, ());
  TEST_EQUAL(bm->GetDescription(), "", ());
  TEST_EQUAL(kml::ToSecondsSinceEpoch(bm->GetTimeStamp()), 0, ());

  bm = bmManager.GetBookmark(*it++);
  TEST_EQUAL(bm->GetName(), "Monongahela National Forest", ());
  TEST_EQUAL(bm->GetColor(), kml::PredefinedColor::Pink, ());
  TEST_EQUAL(bm->GetDescription(), "Huttonsville, WV 26273<br>", ());
  TEST_EQUAL(kml::ToSecondsSinceEpoch(bm->GetTimeStamp()), 524214643, ());

  bm = bmManager.GetBookmark(*it++);
  m2::PointD org = bm->GetPivot();
  TEST_ALMOST_EQUAL_ULPS(MercatorBounds::XToLon(org.x), 27.566765, ());
  TEST_ALMOST_EQUAL_ULPS(MercatorBounds::YToLat(org.y), 53.900047, ());
  TEST_EQUAL(bm->GetName(), "From: Минск, Минская область, Беларусь", ());
  TEST_EQUAL(bm->GetColor(), kml::PredefinedColor::Blue, ());
  TEST_EQUAL(bm->GetDescription(), "", ());
  TEST_EQUAL(kml::ToSecondsSinceEpoch(bm->GetTimeStamp()), 888888888, ());

  bm = bmManager.GetBookmark(*it++);
  org = bm->GetPivot();
  TEST_ALMOST_EQUAL_ULPS(MercatorBounds::XToLon(org.x), 27.551532, ());
  TEST_ALMOST_EQUAL_ULPS(MercatorBounds::YToLat(org.y), 53.89306, ());
  TEST_EQUAL(bm->GetName(), "<MWM & Sons>", ());
  TEST_EQUAL(bm->GetDescription(), "Amps & <brackets>", ());
  TEST_EQUAL(kml::ToSecondsSinceEpoch(bm->GetTimeStamp()), 0, ());
}
}  // namespace

UNIT_CLASS_TEST(Runner, Bookmarks_ImportKML)
{
  BookmarkManager bmManager((BookmarkManager::Callbacks(bmCallbacks)));
  BookmarkManager::KMLDataCollection kmlDataCollection;

  kmlDataCollection.emplace_back(""/* filePath */,
                                 LoadKMLData(MemReader(kmlString, strlen(kmlString)), false /* useBinary */));
  TEST(kmlDataCollection.back().second, ());
  bmManager.CreateCategories(std::move(kmlDataCollection), false /* autoSave */);
  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 1, ());

  auto const groupId = bmManager.GetBmGroupsIdList().front();
  CheckBookmarks(bmManager, groupId);

  // Name should be overridden from the KML
  TEST_EQUAL(bmManager.GetCategoryName(groupId), "MapName", ());
  TEST_EQUAL(bmManager.IsVisible(groupId), false, ());
}

UNIT_CLASS_TEST(Runner, Bookmarks_ExportKML)
{
  std::string const fileName = GetPlatform().SettingsDir() + "UnitTestBookmarks.kml";

  BookmarkManager bmManager((BookmarkManager::Callbacks(bmCallbacks)));
  BookmarkManager::KMLDataCollection kmlDataCollection;

  kmlDataCollection.emplace_back(""/* filePath */,
                                 LoadKMLData(MemReader(kmlString, strlen(kmlString)), false /* useBinary */));
  bmManager.CreateCategories(std::move(kmlDataCollection), false /* autoSave */);
  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 1, ());

  auto const groupId1 = bmManager.GetBmGroupsIdList().front();
  CheckBookmarks(bmManager, groupId1);

  TEST_EQUAL(bmManager.IsVisible(groupId1), false, ());

  // Change visibility
  bmManager.GetEditSession().SetIsVisible(groupId1, true);
  TEST_EQUAL(bmManager.IsVisible(groupId1), true, ());

  {
    LOG(LWARNING, ("SaveToKML (", fileName, ")"));
    FileWriter writer(fileName);
    bmManager.SaveToKML(groupId1, writer, false /* useBinary */);
  }

  bmManager.GetEditSession().ClearGroup(groupId1);
  TEST_EQUAL(bmManager.GetUserMarkIds(groupId1).size(), 0, ());

  bmManager.GetEditSession().DeleteBmCategory(groupId1);
  TEST_EQUAL(bmManager.HasBmCategory(groupId1), false, ());
  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 0, ());

  kmlDataCollection.clear();
  kmlDataCollection.emplace_back("", LoadKMLData(FileReader(fileName), false /* useBinary */));
  TEST(kmlDataCollection.back().second, ());

  bmManager.CreateCategories(std::move(kmlDataCollection), false /* autoSave */);
  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 1, ());

  auto const groupId2 = bmManager.GetBmGroupsIdList().front();
  CheckBookmarks(bmManager, groupId2);
  TEST_EQUAL(bmManager.IsVisible(groupId2), true, ());

  bmManager.GetEditSession().DeleteBmCategory(groupId2);
  TEST_EQUAL(bmManager.HasBmCategory(groupId2), false, ());

  LOG(LWARNING, ("LoadKMLFile(", fileName, ")"));
  kmlDataCollection.clear();
  kmlDataCollection.emplace_back("", LoadKMLFile(fileName, false /* useBinary */));
  TEST(kmlDataCollection.back().second, ());

  bmManager.CreateCategories(std::move(kmlDataCollection), false /* autoSave */);
  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 1, ());

  auto const groupId3 = bmManager.GetBmGroupsIdList().front();
  CheckBookmarks(bmManager, groupId3);

  TEST(bmManager.SaveBookmarkCategoryToFile(groupId3), ());
  // old file shouldn't be deleted if we save bookmarks with new category name
  uint64_t dummy;
  TEST(my::GetFileSize(fileName, dummy), ());

  TEST(my::DeleteFileX(fileName), ());
}

namespace
{
  void DeleteCategoryFiles(vector<string> const & arrFiles)
  {
    string const path = GetPlatform().SettingsDir();
    for (auto const & fileName : arrFiles)
      FileWriter::DeleteFileX(path + fileName + BOOKMARKS_FILE_EXTENSION);
  }

  UserMark const * GetMark(Framework & fm, m2::PointD const & pt)
  {
    m2::AnyRectD rect;
    fm.GetTouchRect(fm.GtoP(pt), 20, rect);

    return fm.GetBookmarkManager().FindNearestUserMark(rect);
  }

  Bookmark const * GetBookmark(Framework & fm, m2::PointD const & pt)
  {
    auto const * mark = GetMark(fm, pt);
    ASSERT(mark != NULL, ());
    ASSERT(mark->GetMarkType() == UserMark::BOOKMARK, ());
    return static_cast<Bookmark const *>(mark);
  }

  Bookmark const * GetBookmarkPxPoint(Framework & fm, m2::PointD const & pt)
  {
    return GetBookmark(fm, fm.PtoG(pt));
  }

  bool IsValidBookmark(Framework & fm, m2::PointD const & pt)
  {
    auto const * mark = GetMark(fm, pt);
    return (mark != nullptr) && (mark->GetMarkType() == UserMark::BOOKMARK);
  }
}  // namespace

UNIT_TEST(Bookmarks_Timestamp)
{
  Framework fm(kFrameworkParams);
  df::VisualParams::Init(1.0, 1024);

  m2::PointD const orgPoint(10, 10);

  vector<string> const arrCat = {"cat", "cat1"};

  BookmarkManager & bmManager = fm.GetBookmarkManager();

  kml::BookmarkData b1;
  kml::SetDefaultStr(b1.m_name, "name");
  b1.m_point = orgPoint;
  auto cat1 = bmManager.CreateBookmarkCategory(arrCat[0], false /* autoSave */);

  Bookmark const * pBm1 = bmManager.GetEditSession().CreateBookmark(b1, cat1);
  TEST_NOT_EQUAL(kml::ToSecondsSinceEpoch(pBm1->GetTimeStamp()), 0, ());

  kml::BookmarkData b2;
  kml::SetDefaultStr(b2.m_name, "newName");
  b2.m_point = orgPoint;
  Bookmark const * pBm2 = bmManager.GetEditSession().CreateBookmark(b2, cat1);

  auto cat2 = bmManager.CreateBookmarkCategory(arrCat[0], false /* autoSave */);
  Bookmark const * pBm3 = bmManager.GetEditSession().CreateBookmark(b2, cat2);

  // Check bookmarks order here. First added should be in the bottom of the list.
  auto const firstId = * bmManager.GetUserMarkIds(cat1).rbegin();
  TEST_EQUAL(firstId, pBm1->GetId(), ());

  Bookmark const * pBm01 = bmManager.GetBookmark(pBm1->GetId());

  TEST_EQUAL(pBm01->GetName(), "name", ());

  Bookmark const * pBm02 = bmManager.GetBookmark(pBm2->GetId());

  TEST_EQUAL(pBm02->GetName(), "newName", ());

  Bookmark const * pBm03 = bmManager.GetBookmark(pBm3->GetId());

  TEST_EQUAL(pBm03->GetName(), "newName", ());

  TEST_EQUAL(bmManager.GetUserMarkIds(cat1).size(), 2, ());
  TEST_EQUAL(bmManager.GetUserMarkIds(cat2).size(), 1, ());

  DeleteCategoryFiles(arrCat);
}

UNIT_TEST(Bookmarks_Getting)
{
  Framework fm(kFrameworkParams);
  df::VisualParams::Init(1.0, 1024);
  fm.OnSize(800, 400);
  fm.ShowRect(m2::RectD(0, 0, 80, 40));

  // This is not correct because Framework::OnSize doesn't work until SetRenderPolicy is called.
  //TEST(m2::AlmostEqualULPs(m2::PointD(400, 200), pixC), (pixC));

  BookmarkManager & bmManager = fm.GetBookmarkManager();

  vector<string> const arrCat = {"cat1", "cat2", "cat3"};

  auto const cat1 = bmManager.CreateBookmarkCategory(arrCat[0], false /* autoSave */);
  auto const cat2 = bmManager.CreateBookmarkCategory(arrCat[1], false /* autoSave */);
  auto const cat3 = bmManager.CreateBookmarkCategory(arrCat[2], false /* autoSave */);

  kml::BookmarkData bm;
  kml::SetDefaultStr(bm.m_name, "1");
  bm.m_point = m2::PointD(38, 20);
  auto const * pBm1 = bmManager.GetEditSession().CreateBookmark(bm, cat1);

  kml::SetDefaultStr(bm.m_name, "2");
  bm.m_point = m2::PointD(41, 20);
  auto const * pBm2 = bmManager.GetEditSession().CreateBookmark(bm, cat2);

  kml::SetDefaultStr(bm.m_name, "3");
  bm.m_point = m2::PointD(41, 40);
  auto const * pBm3 = bmManager.GetEditSession().CreateBookmark(bm, cat3);

  TEST_NOT_EQUAL(pBm1->GetGroupId(), pBm2->GetGroupId(), ());
  TEST_NOT_EQUAL(pBm1->GetGroupId(), pBm3->GetGroupId(), ());
  TEST_NOT_EQUAL(pBm1->GetGroupId(), pBm3->GetGroupId(), ());

  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 3, ());

  TEST(IsValidBookmark(fm, m2::PointD(40, 20)), ());
  Bookmark const * mark = GetBookmark(fm, m2::PointD(40, 20));
  TEST_EQUAL(bmManager.GetCategoryName(mark->GetGroupId()), "cat2", ());

  TEST(!IsValidBookmark(fm, m2::PointD(0, 0)), ());
  TEST(!IsValidBookmark(fm, m2::PointD(800, 400)), ());

  TEST(IsValidBookmark(fm, m2::PointD(41, 40)), ());
  mark = GetBookmark(fm, m2::PointD(41, 40));
  TEST_EQUAL(bmManager.GetCategoryName(mark->GetGroupId()), "cat3", ());

  kml::SetDefaultStr(bm.m_name, "4");
  bm.m_point = m2::PointD(41, 40);
  bm.m_color.m_predefinedColor = kml::PredefinedColor::Blue;
  auto const * pBm4 = bmManager.GetEditSession().CreateBookmark(bm, cat3);

  TEST_EQUAL(pBm3->GetGroupId(), pBm4->GetGroupId(), ());

  mark = GetBookmark(fm, m2::PointD(41, 40));

  // Should find last added valid result, there two results with the
  // same coordinates 3 and 4, but 4 was added later.
  TEST_EQUAL(mark->GetName(), "4", ());
  TEST_EQUAL(mark->GetColor(), kml::PredefinedColor::Blue, ());

  TEST_EQUAL(bmManager.GetUserMarkIds(mark->GetGroupId()).size(), 2, ());
  bmManager.GetEditSession().DeleteBookmark(mark->GetId());
  TEST_EQUAL(bmManager.GetUserMarkIds(cat3).size(), 1, ());

  DeleteCategoryFiles(arrCat);
}

namespace
{
  struct POIInfo
  {
    char const * m_name;
    char const * m_street;
    char const * m_house;
    char const * m_type;
  };

  void CheckPlace(Framework const & fm, double lat, double lon, POIInfo const & poi)
  {
    search::AddressInfo const info = fm.GetAddressInfoAtPoint(MercatorBounds::FromLatLon(lat, lon));

    TEST_EQUAL(info.m_street, poi.m_street, ());
    TEST_EQUAL(info.m_house, poi.m_house, ());
    // TODO(AlexZ): AddressInfo should contain addresses only. Refactor.
    //TEST_EQUAL(info.m_name, poi.m_name, ());
    //TEST_EQUAL(info.m_types.size(), 1, ());
    //TEST_EQUAL(info.GetBestType(), poi.m_type, ());
  }
}

UNIT_TEST(Bookmarks_AddressInfo)
{
  // Maps added in constructor (we need minsk-pass.mwm only)
  Framework fm(kFrameworkParams);
  fm.DeregisterAllMaps();
  fm.RegisterMap(platform::LocalCountryFile::MakeForTesting("minsk-pass"));
  fm.OnSize(800, 600);

  // Our code always uses "default" street name for addresses.
  CheckPlace(fm, 53.8964918, 27.555559, { "Планета Pizza", "улица Карла Маркса", "10", "Cafe" });
  CheckPlace(fm, 53.8964365, 27.5554007, { "Нц Шашек И Шахмат", "улица Карла Маркса", "10", "Hotel" });
}

UNIT_TEST(Bookmarks_IllegalFileName)
{
  vector<string> const arrIllegal = {"?", "?|", "\"x", "|x:", "x<>y", "xy*"};
  vector<string> const arrLegal =   {"",  "",   "x",   "x",   "xy",   "xy"};

  for (size_t i = 0; i < arrIllegal.size(); ++i)
  {
    string const name = BookmarkManager::RemoveInvalidSymbols(arrIllegal[i]);

    if (arrLegal[i].empty())
    {
      TEST_EQUAL("Bookmarks", name, ());
    }
    else
    {
      TEST_EQUAL(arrLegal[i], name, ());
    }
  }
}

UNIT_TEST(Bookmarks_UniqueFileName)
{
  string const BASE = "SomeUniqueFileName";
  string const FILEBASE = "./" + BASE;
  string const FILENAME = FILEBASE + BOOKMARKS_FILE_EXTENSION;

  {
    FileWriter file(FILENAME);
    file.Write(FILENAME.data(), FILENAME.size());
  }

  string gen = BookmarkManager::GenerateUniqueFileName(".", BASE, BOOKMARKS_FILE_EXTENSION);
  TEST_NOT_EQUAL(gen, FILENAME, ());
  TEST_EQUAL(gen, FILEBASE + "1.kml", ());

  string const FILENAME1 = gen;
  {
    FileWriter file(FILENAME1);
    file.Write(FILENAME1.data(), FILENAME1.size());
  }
  gen = BookmarkManager::GenerateUniqueFileName(".", BASE, BOOKMARKS_FILE_EXTENSION);
  TEST_NOT_EQUAL(gen, FILENAME, ());
  TEST_NOT_EQUAL(gen, FILENAME1, ());
  TEST_EQUAL(gen, FILEBASE + "2.kml", ());

  FileWriter::DeleteFileX(FILENAME);
  FileWriter::DeleteFileX(FILENAME1);

  gen = BookmarkManager::GenerateUniqueFileName(".", BASE, BOOKMARKS_FILE_EXTENSION);
  TEST_EQUAL(gen, FILENAME, ());
}

UNIT_TEST(Bookmarks_AddingMoving)
{
  Framework fm(kFrameworkParams);
  fm.OnSize(800, 400);
  fm.ShowRect(m2::RectD(0, 0, 80, 40));

  m2::PointD const globalPoint = m2::PointD(40, 20);
  m2::PointD const pixelPoint = fm.GtoP(globalPoint);

  BookmarkManager & bmManager = fm.GetBookmarkManager();

  vector<string> const arrCat = {"cat1", "cat2"};
  auto const cat1 = bmManager.CreateBookmarkCategory(arrCat[0], false /* autoSave */);
  auto const cat2 = bmManager.CreateBookmarkCategory(arrCat[1], false /* autoSave */);

  kml::BookmarkData bm;
  kml::SetDefaultStr(bm.m_name, "name");
  bm.m_point = globalPoint;
  auto const * pBm1 = bmManager.GetEditSession().CreateBookmark(bm, cat1);
  Bookmark const * mark = GetBookmarkPxPoint(fm, pixelPoint);
  TEST_EQUAL(bmManager.GetCategoryName(mark->GetGroupId()), "cat1", ());

  kml::SetDefaultStr(bm.m_name, "name2");
  bm.m_color.m_predefinedColor = kml::PredefinedColor::Blue;
  auto const * pBm11 = bmManager.GetEditSession().CreateBookmark(bm, cat1);
  TEST_EQUAL(pBm1->GetGroupId(), pBm11->GetGroupId(), ());
  mark = GetBookmarkPxPoint(fm, pixelPoint);
  TEST_EQUAL(bmManager.GetCategoryName(mark->GetGroupId()), "cat1", ());
  TEST_EQUAL(mark->GetName(), "name2", ());
  TEST_EQUAL(mark->GetColor(), kml::PredefinedColor::Blue, ());

  // Edit name, type and category of bookmark
  kml::SetDefaultStr(bm.m_name, "name3");
  bm.m_color.m_predefinedColor = kml::PredefinedColor::Green;
  auto const * pBm2 = bmManager.GetEditSession().CreateBookmark(bm, cat2);
  TEST_NOT_EQUAL(pBm1->GetGroupId(), pBm2->GetGroupId(), ());
  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 2, ());
  mark = GetBookmarkPxPoint(fm, pixelPoint);
  TEST_EQUAL(bmManager.GetCategoryName(mark->GetGroupId()), "cat1", ());
  TEST_EQUAL(bmManager.GetUserMarkIds(cat1).size(), 2,
             ("Bookmark wasn't moved from one category to another"));
  TEST_EQUAL(mark->GetName(), "name2", ());
  TEST_EQUAL(mark->GetColor(), kml::PredefinedColor::Blue, ());

  DeleteCategoryFiles(arrCat);
}

namespace
{
char const * kmlString2 =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.1\">"
    "<Document>"
     "<name>busparkplatz</name>"
     "<Folder>"
      "<name>Waypoint</name>"
      "<Style id=\"poiIcon37\">"
       "<IconStyle>"
       "<scale>1</scale>"
       "<Icon><href>http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png</href></Icon>"
       "<hotSpot x=\"0.5\" y=\"0\" xunits=\"fraction\" yunits=\"fraction\"/>"
       "</IconStyle>"
      "</Style>"
      "<Placemark>"
       "<name>[P] Silvrettastrae[Bieler Hhe]</name>"
       "<description></description>"
       "<styleUrl>#poiIcon37</styleUrl>"
       "<Point>"
        "<coordinates>10.09237,46.91741,0</coordinates>"
       "</Point>"
      "</Placemark>"
    "</Folder>"
   "</Document>"
   "</kml>";
}

UNIT_CLASS_TEST(Runner, Bookmarks_InnerFolder)
{
  BookmarkManager bmManager((BookmarkManager::Callbacks(bmCallbacks)));
  BookmarkManager::KMLDataCollection kmlDataCollection;

  kmlDataCollection.emplace_back("" /* filePath */,
                                 LoadKMLData(MemReader(kmlString2, strlen(kmlString2)), false /* useBinary */));
  bmManager.CreateCategories(std::move(kmlDataCollection), false /* autoSave */);
  auto const & groupIds = bmManager.GetBmGroupsIdList();
  TEST_EQUAL(groupIds.size(), 1, ());
  TEST_EQUAL(bmManager.GetUserMarkIds(groupIds.front()).size(), 1, ());
}

UNIT_CLASS_TEST(Runner, BookmarkCategory_EmptyName)
{
  BookmarkManager bmManager((BookmarkManager::Callbacks(bmCallbacks)));
  auto const catId = bmManager.CreateBookmarkCategory("", false /* autoSave */);
  kml::BookmarkData bm;
  bm.m_point = m2::PointD(0, 0);
  bmManager.GetEditSession().CreateBookmark(bm, catId);

  TEST(bmManager.SaveBookmarkCategoryToFile(catId), ());

  bmManager.GetEditSession().SetCategoryName(catId, "xxx");

  TEST(bmManager.SaveBookmarkCategoryToFile(catId), ());

  vector<string> const arrFiles = {"Bookmarks", "xxx"};
  DeleteCategoryFiles(arrFiles);
}

namespace
{
// Do check for "bad" names without CDATA markers.
char const * kmlString3 =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.1\">"
    "<Document>"
     "<name>3663 and M <![CDATA[&]]> J Seafood Branches</name>"
     "<visibility>1</visibility>"
      "<Placemark>"
       "<name>![X1]{X2}(X3)</name>"
       "<Point>"
        "<coordinates>50, 50</coordinates>"
       "</Point>"
      "</Placemark>"
   "</Document>"
   "</kml>";

  bool EqualBookmarks(Bookmark const & b1, Bookmark const & b2)
  {
    if (b1.GetName() != b2.GetName())
      return false;
    if (b1.GetDescription() != b2.GetDescription())
      return false;
    if (b1.GetColor() != b2.GetColor())
      return false;
    if (b1.GetScale() != b2.GetScale())
      return false;
    if (!m2::AlmostEqualULPs(b1.GetPivot(), b2.GetPivot()))
      return false;

    // do not check timestamp
    return true;
  }
}

UNIT_CLASS_TEST(Runner, Bookmarks_SpecialXMLNames)
{
  BookmarkManager bmManager((BookmarkManager::Callbacks(bmCallbacks)));
  BookmarkManager::KMLDataCollection kmlDataCollection;
  kmlDataCollection.emplace_back("" /* filePath */,
                                 LoadKMLData(MemReader(kmlString3, strlen(kmlString3)), false /* useBinary */));
  bmManager.CreateCategories(std::move(kmlDataCollection), false /* autoSave */);

  auto const & groupIds = bmManager.GetBmGroupsIdList();
  TEST_EQUAL(groupIds.size(), 1, ());
  auto const catId = groupIds.front();
  auto const expectedName = "3663 and M & J Seafood Branches";
  TEST_EQUAL(bmManager.GetUserMarkIds(catId).size(), 1, ());
  TEST(bmManager.SaveBookmarkCategoryToFile(catId), ());

  TEST_EQUAL(bmManager.GetCategoryName(catId), expectedName, ());
  // change category name to avoid merging it with the second one
  auto const fileName = bmManager.GetCategoryFileName(catId);
  auto editSession = bmManager.GetEditSession();
  editSession.SetCategoryName(catId, "test");

  kmlDataCollection.clear();
  kmlDataCollection.emplace_back(fileName, LoadKMLFile(fileName, BookmarkManager::IsMigrated()));
  bmManager.CreateCategories(std::move(kmlDataCollection), false /* autoSave */);
  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 2, ());
  auto const catId2 = bmManager.GetBmGroupsIdList().back();

  TEST_EQUAL(bmManager.GetUserMarkIds(catId2).size(), 1, ());
  TEST_EQUAL(bmManager.GetCategoryName(catId2), expectedName, ());
  TEST_EQUAL(bmManager.GetCategoryFileName(catId2), fileName, ());

  auto const bmId1 = *bmManager.GetUserMarkIds(catId).begin();
  auto const * bm1 = bmManager.GetBookmark(bmId1);
  auto const bmId2 = *bmManager.GetUserMarkIds(catId2).begin();
  auto const * bm2 = bmManager.GetBookmark(bmId2);
  TEST(EqualBookmarks(*bm1, *bm2), ());
  TEST_EQUAL(bm1->GetName(), "![X1]{X2}(X3)", ());

  TEST(my::DeleteFileX(fileName), ());
}

UNIT_CLASS_TEST(Runner, TrackParsingTest_1)
{
  string const kmlFile = GetPlatform().TestsDataPathForFile("kml-with-track-kml.test");
  BookmarkManager bmManager((BookmarkManager::Callbacks(bmCallbacks)));
  BookmarkManager::KMLDataCollection kmlDataCollection;
  kmlDataCollection.emplace_back(kmlFile, LoadKMLFile(kmlFile, false /* useBinary */));
  TEST(kmlDataCollection.back().second, ("KML can't be loaded"));

  bmManager.CreateCategories(std::move(kmlDataCollection), false /* autoSave */);
  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 1, ());

  auto catId = bmManager.GetBmGroupsIdList().front();
  TEST_EQUAL(bmManager.GetTrackIds(catId).size(), 4, ());

  array<string, 4> const names = {{"Option1", "Pakkred1", "Pakkred2", "Pakkred3"}};
  array<dp::Color, 4> const col = {{dp::Color(230, 0, 0, 255),
                                    dp::Color(171, 230, 0, 255),
                                    dp::Color(0, 230, 117, 255),
                                    dp::Color(0, 59, 230, 255)}};
  array<double, 4> const length = {{3525.46839061, 27174.11393166, 27046.0456586, 23967.35765800}};

  size_t i = 0;
  for (auto trackId : bmManager.GetTrackIds(catId))
  {
    auto const * track = bmManager.GetTrack(trackId);
    TEST_EQUAL(names[i], track->GetName(), ());
    TEST(fabs(track->GetLengthMeters() - length[i]) < 1.0E-6, (track->GetLengthMeters(), length[i]));
    TEST_GREATER(track->GetLayerCount(), 0, ());
    TEST_EQUAL(col[i], track->GetColor(0), ());
    ++i;
  }
}

UNIT_CLASS_TEST(Runner, TrackParsingTest_2)
{
  string const kmlFile = GetPlatform().TestsDataPathForFile("kml-with-track-from-google-earth.test");
  BookmarkManager bmManager((BookmarkManager::Callbacks(bmCallbacks)));
  BookmarkManager::KMLDataCollection kmlDataCollection;
  kmlDataCollection.emplace_back(kmlFile, LoadKMLFile(kmlFile, false /* useBinary */));

  TEST(kmlDataCollection.back().second, ("KML can't be loaded"));
  bmManager.CreateCategories(std::move(kmlDataCollection), false /* autoSave */);
  TEST_EQUAL(bmManager.GetBmGroupsIdList().size(), 1, ());

  auto catId = bmManager.GetBmGroupsIdList().front();
  TEST_EQUAL(bmManager.GetTrackIds(catId).size(), 1, ());
  auto const trackId = *bmManager.GetTrackIds(catId).begin();
  auto const * track = bmManager.GetTrack(trackId);
  TEST_EQUAL(track->GetName(), "XY", ());
  TEST_GREATER(track->GetLayerCount(), 0, ());
  TEST_EQUAL(track->GetColor(0), dp::Color(57, 255, 32, 255), ());
}

UNIT_CLASS_TEST(Runner, Bookmarks_Listeners)
{
  set<df::MarkID> createdMarksResult;
  set<df::MarkID> updatedMarksResult;
  set<df::MarkID> deletedMarksResult;
  set<df::MarkID> createdMarks;
  set<df::MarkID> updatedMarks;
  set<df::MarkID> deletedMarks;

  auto const checkNotifications = [&]()
  {
    TEST_EQUAL(createdMarks, createdMarksResult, ());
    TEST_EQUAL(updatedMarks, updatedMarksResult, ());
    TEST_EQUAL(deletedMarks, deletedMarksResult, ());

    createdMarksResult.clear();
    updatedMarksResult.clear();
    deletedMarksResult.clear();
    createdMarks.clear();
    updatedMarks.clear();
    deletedMarks.clear();
  };

  auto const onCreate = [&createdMarksResult](std::vector<std::pair<df::MarkID, kml::BookmarkData>> const &marks)
  {
    for (auto const & markPair : marks)
      createdMarksResult.insert(markPair.first);
  };
  auto const onUpdate = [&updatedMarksResult](std::vector<std::pair<df::MarkID, kml::BookmarkData>> const &marks)
  {
    for (auto const & markPair : marks)
      updatedMarksResult.insert(markPair.first);
  };
  auto const onDelete = [&deletedMarksResult](std::vector<df::MarkID> const &marks)
  {
    deletedMarksResult.insert(marks.begin(), marks.end());
  };

  BookmarkManager::Callbacks callbacks(
    []()
    {
      static StringsBundle dummyBundle;
      return dummyBundle;
    },
    onCreate,
    onUpdate,
    onDelete);

  BookmarkManager bmManager(std::move(callbacks));

  auto const catId = bmManager.CreateBookmarkCategory("Default", false /* autoSave */);

  {
    auto editSession = bmManager.GetEditSession();
    kml::BookmarkData data;
    kml::SetDefaultStr(data.m_name, "name 0");
    data.m_point = m2::PointD(0.0, 0.0);
    auto * bookmark0 = editSession.CreateBookmark(data);
    editSession.AttachBookmark(bookmark0->GetId(), catId);
    createdMarks.insert(bookmark0->GetId());

    kml::SetDefaultStr(data.m_name, "name 1");
    auto * bookmark1 = editSession.CreateBookmark(data);
    editSession.AttachBookmark(bookmark1->GetId(), catId);
    createdMarks.insert(bookmark1->GetId());

    createdMarks.erase(bookmark1->GetId());
    editSession.DeleteBookmark(bookmark1->GetId());
  }
  checkNotifications();

  auto const markId0 = *bmManager.GetUserMarkIds(catId).begin();
  bmManager.GetEditSession().GetBookmarkForEdit(markId0)->SetName("name 3");
  updatedMarks.insert(markId0);

  checkNotifications();

  {
    auto editSession = bmManager.GetEditSession();
    editSession.GetBookmarkForEdit(markId0)->SetName("name 4");
    editSession.DeleteBookmark(markId0);
    deletedMarks.insert(markId0);

    kml::BookmarkData data;
    kml::SetDefaultStr(data.m_name, "name 5");
    data.m_point = m2::PointD(0.0, 0.0);
    auto * bookmark1 = editSession.CreateBookmark(data);
    createdMarks.insert(bookmark1->GetId());
  }
  checkNotifications();
}

UNIT_CLASS_TEST(Runner, Bookmarks_AutoSave)
{
  BookmarkManager bmManager((BookmarkManager::Callbacks(bmCallbacks)));
  kml::BookmarkData data;
  df::MarkID bmId0;
  auto const catId = bmManager.CreateBookmarkCategory("test");
  data.m_point = m2::PointD(0.0, 0.0);
  {
    kml::SetDefaultStr(data.m_name, "name 0");
    auto editSession = bmManager.GetEditSession();
    bmId0 = editSession.CreateBookmark(data)->GetId();
    editSession.AttachBookmark(bmId0, catId);
  }
  auto const fileName = bmManager.GetCategoryFileName(catId);

  auto kmlData = LoadKMLFile(fileName, BookmarkManager::IsMigrated());
  TEST(kmlData != nullptr, ());
  TEST_EQUAL(kmlData->m_bookmarksData.size(), 1, ());
  TEST_EQUAL(kml::GetDefaultStr(kmlData->m_bookmarksData.front().m_name), "name 0", ());

  {
    auto editSession = bmManager.GetEditSession();
    editSession.GetBookmarkForEdit(bmId0)->SetName("name 0 renamed");

    kml::SetDefaultStr(data.m_name, "name 1");
    auto bmId = editSession.CreateBookmark(data)->GetId();
    editSession.AttachBookmark(bmId, catId);

    kml::SetDefaultStr(data.m_name, "name 2");
    bmId = editSession.CreateBookmark(data)->GetId();
    editSession.AttachBookmark(bmId, catId);

    kml::SetDefaultStr(data.m_name, "name 3");
    bmId = editSession.CreateBookmark(data)->GetId();
    editSession.AttachBookmark(bmId, catId);

    kmlData = LoadKMLFile(fileName, BookmarkManager::IsMigrated());
    TEST(kmlData != nullptr, ());
    TEST_EQUAL(kmlData->m_bookmarksData.size(), 1, ());
    TEST_EQUAL(kml::GetDefaultStr(kmlData->m_bookmarksData.front().m_name), "name 0", ());
  }

  kmlData = LoadKMLFile(fileName, BookmarkManager::IsMigrated());
  TEST(kmlData != nullptr, ());
  TEST_EQUAL(kmlData->m_bookmarksData.size(), 4, ());
  TEST_EQUAL(kml::GetDefaultStr(kmlData->m_bookmarksData.front().m_name), "name 0 renamed", ());

  bmManager.GetEditSession().DeleteBookmark(bmId0);

  kmlData = LoadKMLFile(fileName, BookmarkManager::IsMigrated());
  TEST(kmlData != nullptr, ());
  TEST_EQUAL(kmlData->m_bookmarksData.size(), 3, ());
  TEST_EQUAL(kml::GetDefaultStr(kmlData->m_bookmarksData.front().m_name), "name 1", ());

  auto const catId2 = bmManager.CreateBookmarkCategory("test 2");
  auto const movedBmId = *bmManager.GetUserMarkIds(catId).begin();
  bmManager.GetEditSession().MoveBookmark(movedBmId, catId, catId2);

  kmlData = LoadKMLFile(fileName, BookmarkManager::IsMigrated());
  TEST(kmlData != nullptr, ());
  TEST_EQUAL(kmlData->m_bookmarksData.size(), 2, ());
  TEST_EQUAL(kml::GetDefaultStr(kmlData->m_bookmarksData.front().m_name), "name 1", ());

  auto const fileName2 = bmManager.GetCategoryFileName(catId2);
  auto kmlData2 = LoadKMLFile(fileName2, BookmarkManager::IsMigrated());
  TEST(kmlData2 != nullptr, ());
  TEST_EQUAL(kmlData2->m_bookmarksData.size(), 1, ());
  TEST_EQUAL(kml::GetDefaultStr(kmlData2->m_bookmarksData.front().m_name), "name 3", ());

  TEST(my::DeleteFileX(fileName), ());
  TEST(my::DeleteFileX(fileName2), ());
}
