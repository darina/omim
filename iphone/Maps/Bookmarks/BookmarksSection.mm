#import "BookmarksSection.h"
#import "CircleView.h"
#import "ColorPickerView.h"
#import "MWMBookmarksManager.h"
#import "MWMCategoryInfoCell.h"
#import "MWMLocationHelpers.h"
#import "MWMSearchManager.h"
#include "Framework.h"

#include "geometry/distance_on_sphere.hpp"

namespace
{
CGFloat const kPinDiameter = 18.0f;
}  // namespace

@implementation BookmarksSection

- (instancetype)initWithDelegate: (id<BookmarksSectionDelegate>)delegate
{
  self = [super init];
  if (self)
  {
    _delegate = delegate;
  }
  return self;
}

- (NSInteger)numberOfRows
{
  return [self.delegate numberOfBookmarks];
}

- (NSString *)title
{
  return [self.delegate titleOfBookmarksSection];
}

- (BOOL)canEdit
{
  return [self.delegate canEditBookmarksSection];
}

- (UITableViewCell *)tableView: (UITableView *)tableView cellForRow: (NSInteger)row
{
  UITableViewCell * cell = [tableView dequeueReusableCellWithIdentifier:@"BookmarksVCBookmarkItemCell"];
  if (!cell)
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:@"BookmarksVCBookmarkItemCell"];
  CHECK(cell, ("Invalid bookmark cell."));
  
  auto & f = GetFramework();
  auto const & bm = f.GetBookmarkManager();
  
  kml::MarkId const bmId = [self.delegate getBookmarkIdByRow:row];
  Bookmark const * bookmark = bm.GetBookmark(bmId);
  cell.textLabel.text = @(bookmark->GetPreferredName().c_str());
  cell.imageView.image = [CircleView createCircleImageWith:kPinDiameter
                                                  andColor:[ColorPickerView getUIColor:bookmark->GetColor()]];
  
  CLLocation * lastLocation = [MWMLocationManager lastLocation];
  if (lastLocation)
  {
    double north = location_helpers::headingToNorthRad([MWMLocationManager lastHeading]);
    string distance;
    double azimut = -1.0;
    f.GetDistanceAndAzimut(bookmark->GetPivot(), lastLocation.coordinate.latitude,
                           lastLocation.coordinate.longitude, north, distance, azimut);
    
    cell.detailTextLabel.text = @(distance.c_str());
  }
  else
  {
    cell.detailTextLabel.text = nil;
  }
  return cell;
}

- (void)updateCell: (UITableViewCell *)cell forRow:(NSInteger)row withNewLocation: (location::GpsInfo const &)info
{
  auto & f = GetFramework();
  auto const & bm = f.GetBookmarkManager();
  
  kml::MarkId const bmId = [self.delegate getBookmarkIdByRow:row];
  Bookmark const * bookmark = bm.GetBookmark(bmId);
  if (!bookmark)
    return;
  
  m2::PointD const center = bookmark->GetPivot();
  double const metres = ms::DistanceOnEarth(info.m_latitude, info.m_longitude,
                                            MercatorBounds::YToLat(center.y), MercatorBounds::XToLon(center.x));
  cell.detailTextLabel.text = location_helpers::formattedDistance(metres);
}

- (BOOL)didSelectRow: (NSInteger)row
{
  kml::MarkId const bmId = [self.delegate getBookmarkIdByRow:row];
  [Statistics logEvent:kStatEventName(kStatBookmarks, kStatShowOnMap)];
  // Same as "Close".
  [MWMSearchManager manager].state = MWMSearchManagerStateHidden;
  auto & f = GetFramework();
  f.ShowBookmark(bmId);
  return YES;
}

- (void)deleteRow: (NSInteger)row
{
  kml::MarkId const bmId = [self.delegate getBookmarkIdByRow:row];
  [[MWMBookmarksManager sharedManager] deleteBookmark:bmId];
}

@end

////////////////////////////////////////////////////////

@implementation TracksSection

- (instancetype)initWithDelegate: (id<TracksSectionDelegate>)delegate
{
  self = [super init];
  if (self)
  {
    _delegate = delegate;
  }
  return self;
}

- (NSInteger)numberOfRows
{
  return [self.delegate numberOfTracks];
}

- (NSString *)title
{
  return [self.delegate titleOfTracksSection];
}

- (BOOL)canEdit
{
  return [self.delegate canEditTracksSection];
}

- (UITableViewCell *)tableView: (UITableView *)tableView cellForRow: (NSInteger)row
{
  UITableViewCell * cell = [tableView dequeueReusableCellWithIdentifier:@"TrackCell"];
  if (!cell)
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:@"TrackCell"];
  CHECK(cell, ("Invalid track cell."));
  
  auto & f = GetFramework();
  auto const & bm = f.GetBookmarkManager();
  
  kml::TrackId const trackId = [self.delegate getTrackIdByRow:row];
  Track const * track = bm.GetTrack(trackId);
  cell.textLabel.text = @(track->GetName().c_str());
  string dist;
  if (measurement_utils::FormatDistance(track->GetLengthMeters(), dist))
    //Change Length before release!!!
    cell.detailTextLabel.text = [NSString stringWithFormat:@"%@ %@", L(@"length"), @(dist.c_str())];
  else
    cell.detailTextLabel.text = nil;
  dp::Color const c = track->GetColor(0);
  cell.imageView.image = [CircleView createCircleImageWith:kPinDiameter
                                                  andColor:[UIColor colorWithRed:c.GetRed()/255.f
                                                                           green:c.GetGreen()/255.f
                                                                            blue:c.GetBlue()/255.f
                                                                           alpha:1.f]];
  return cell;
}

- (BOOL)didSelectRow: (NSInteger)row
{
  auto & f = GetFramework();
  kml::TrackId const trackId = [self.delegate getTrackIdByRow:row];
  f.ShowTrack(trackId);
  return YES;
}

- (void)deleteRow: (NSInteger)row
{
  auto & f = GetFramework();
  // TODO(@darina): [[MWMBookmarksManager sharedManager] deleteTrack:bmId];?
  auto & bm = f.GetBookmarkManager();
  kml::TrackId const trackId = [self.delegate getTrackIdByRow:row];
  bm.GetEditSession().DeleteTrack(trackId);
}

@end

////////////////////////////////////////////////////////

@implementation InfoSection

- (instancetype)initWithDelegate: (id<InfoSectionDelegate>)delegate
{
  self = [super init];
  if (self)
  {
    _delegate = delegate;
  }
  return self;
}

- (NSInteger)numberOfRows
{
  return 1;
}

- (NSString *)title
{
  return L(@"placepage_place_description");
}

- (BOOL)canEdit
{
  return NO;
}

- (UITableViewCell *)tableView: (UITableView *)tableView cellForRow: (NSInteger)row
{
  return [self.delegate infoCellForTableView:tableView];
}

- (BOOL)didSelectRow: (NSInteger)row
{
  return NO;
}

- (void)deleteRow: (NSInteger)row
{
  
}

@end
