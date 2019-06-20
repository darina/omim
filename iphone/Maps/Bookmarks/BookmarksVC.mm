#import "BookmarksVC.h"
#import "BookmarksSection.h"
#import "CircleView.h"
#import "ColorPickerView.h"
#import "MWMBookmarksManager.h"
#import "MWMCommon.h"
#import "MWMLocationHelpers.h"
#import "MWMLocationObserver.h"
#import "MWMSearchManager.h"
#import "MWMCategoryInfoCell.h"
#import "SwiftBridge.h"

#include "Framework.h"

#include "map/bookmarks_search_params.hpp"

#include "geometry/mercator.hpp"

#include "coding/zip_creator.hpp"
#include "coding/internal/file_data.hpp"

#include <iterator>
#include <string>
#include <vector>

using namespace std;

@interface BookmarksVC() <UITableViewDataSource,
                          UITableViewDelegate,
                          UISearchBarDelegate,
                          MWMBookmarksObserver,
                          MWMLocationObserver,
                          BookmarksSectionDelegate,
                          TracksSectionDelegate,
                          MWMCategoryInfoCellDelegate,
                          BookmarksSharingViewControllerDelegate,
                          CategorySettingsViewControllerDelegate>
{
  NSMutableArray<id<TableSectionDelegate>> * m_sectionsCollection;
  BookmarkManager::SortedBlocksCollection m_sortedBlocks;
  
  search::BookmarksSearchParams m_searchParams;
  search::BookmarksSearchParams::Results m_searchResults;
}

@property(nonatomic) NSUInteger lastSearchId;
@property(nonatomic) BOOL infoExpanded;
@property(weak, nonatomic) IBOutlet UIView * statusBarBackground;
@property(weak, nonatomic) IBOutlet UISearchBar * searchBar;
@property(weak, nonatomic) IBOutlet UITableView * tableView;
@property(weak, nonatomic) IBOutlet UIToolbar * myCategoryToolbar;
@property(weak, nonatomic) IBOutlet UIToolbar * downloadedCategoryToolbar;
@property(weak, nonatomic) IBOutlet UIBarButtonItem * viewOnMapItem;
@property(weak, nonatomic) IBOutlet UIBarButtonItem * sortItem;
@property(weak, nonatomic) IBOutlet UIBarButtonItem * moreItem;

@end

@implementation BookmarksVC

- (instancetype)initWithCategory:(MWMMarkGroupID)index
{
  self = [super init];
  if (self)
  {
    m_categoryId = index;
    m_sectionsCollection = @[].mutableCopy;
    [self calculateSections];
  }
  return self;
}

- (BOOL)isSearchMode
{
  return self.searchBar.text && self.searchBar.text.length != 0;
}

- (BOOL)isSortMode
{
  return ![self isSearchMode] && !m_sortedBlocks.empty();
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  return [m_sectionsCollection count];
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
  return [m_sectionsCollection[section] numberOfRows];
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
  return [m_sectionsCollection[section] title];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell * cell = [m_sectionsCollection[indexPath.section] tableView:tableView cellForRow:indexPath.row];
  
  cell.backgroundColor = [UIColor white];
  cell.textLabel.textColor = [UIColor blackPrimaryText];
  cell.detailTextLabel.textColor = [UIColor blackSecondaryText];
  return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  // Remove cell selection
  [self.tableView deselectRowAtIndexPath:indexPath animated:YES];
  
  auto const close = [m_sectionsCollection[indexPath.section] didSelectRow:indexPath.row];
  
  [self.searchBar resignFirstResponder];
  
  if (close)
    [self.navigationController popToRootViewControllerAnimated:NO];
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath
{
  return [m_sectionsCollection[indexPath.section] canEdit];
}

- (void)tableView:(UITableView *)tableView willBeginEditingRowAtIndexPath:(NSIndexPath *)indexPath
{
  self.editing = YES;
}

- (void)tableView:(UITableView *)tableView didEndEditingRowAtIndexPath:(NSIndexPath *)indexPath
{
  self.editing = NO;
}

- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle forRowAtIndexPath:(NSIndexPath *)indexPath
{
  if (![m_sectionsCollection[indexPath.section] canEdit])
    return;

  if (editingStyle == UITableViewCellEditingStyleDelete)
    [m_sectionsCollection[indexPath.section] deleteRow:indexPath.row];
  
  auto const previousNumberOfSections = [m_sectionsCollection count];
  [self calculateSections];

  //We can delete the row with animation, if number of sections stay the same.
  if (previousNumberOfSections == [m_sectionsCollection count])
    [self.tableView deleteRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationFade];
  else
    [self.tableView reloadData];

  // TODO(@darina): check
  auto const & bm = GetFramework().GetBookmarkManager();
  if (bm.GetUserMarkIds(m_categoryId).size() + bm.GetTrackIds(m_categoryId).size() == 0)
  {
    self.navigationItem.rightBarButtonItem = nil;
    [self setEditing:NO animated:YES];
  }
}

- (void)tableView:(UITableView *)tableView willDisplayHeaderView:(UIView *)view forSection:(NSInteger)section
{
  auto header = (UITableViewHeaderFooterView *)view;
  header.textLabel.textColor = [UIColor blackSecondaryText];
  header.textLabel.font = [UIFont medium14];
}

#pragma mark - BookmarksSectionDelegate

- (NSInteger)numberOfBookmarksInSection:(BookmarksSection *)bookmarkSection
{
  if ([self isSearchMode])
  {
    return m_searchResults.size();
  }
  
  if ([self isSortMode])
  {
    CHECK(bookmarkSection.blockIndex != nil, ());
    NSInteger index = bookmarkSection.blockIndex.integerValue;
    return m_sortedBlocks[index].m_markIds.size();
  }
  auto const & bm = GetFramework().GetBookmarkManager();
  return bm.GetUserMarkIds(m_categoryId).size();
}

- (NSString *)titleOfBookmarksSection:(BookmarksSection *)bookmarkSection
{
  if ([self isSearchMode])
    return nil;
  
  if ([self isSortMode])
  {
    CHECK(bookmarkSection.blockIndex != nil, ());
    NSInteger index = bookmarkSection.blockIndex.integerValue;
    return @(m_sortedBlocks[index].m_blockName.c_str());
  }
  
  return L(@"bookmarks");
}

- (BOOL)canEditBookmarksSection:(BookmarksSection *)bookmarkSection
{
  return [[MWMBookmarksManager sharedManager] isCategoryEditable:m_categoryId];
}

- (kml::MarkId)bookmarkSection:(BookmarksSection *)bookmarkSection getBookmarkIdByRow:(NSInteger)row
{
  if ([self isSearchMode])
  {
    CHECK_LESS(row, m_searchResults.size(), ());
    return m_searchResults[row];
  }
  
  if ([self isSortMode])
  {
    CHECK(bookmarkSection.blockIndex != nil, ());
    NSInteger index = bookmarkSection.blockIndex.integerValue;
    return m_sortedBlocks[index].m_markIds[row];
  }
  
  auto const & bm = GetFramework().GetBookmarkManager();
  auto const & bookmarkIds = bm.GetUserMarkIds(m_categoryId);
  ASSERT_LESS(row, bookmarkIds.size(), ());
  auto it = bookmarkIds.begin();
  advance(it, row);
  return *it;
}

- (void)bookmarkSection:(BookmarksSection *)bookmarkSection onDeleteBookmarkInRow:(NSInteger)row
{
  if ([self isSearchMode])
  {
    CHECK_LESS(row, m_searchResults.size(), ());
    m_searchResults.erase(m_searchResults.begin() + row);
    m_sortedBlocks.clear();
    return;
  }
  
  if ([self isSortMode])
  {
    CHECK(bookmarkSection.blockIndex != nil, ());
    NSInteger index = bookmarkSection.blockIndex.integerValue;
    auto & marks = m_sortedBlocks[index].m_markIds;
    marks.erase(marks.begin() + row);
    if (marks.empty())
      m_sortedBlocks.erase(m_sortedBlocks.begin() + index);
  }
}

#pragma mark - TracksSectionDelegate

- (NSInteger)numberOfTracksInSection:(TracksSection *)tracksSection
{
  CHECK(![self isSearchMode], ());
  
  if ([self isSortMode])
  {
    CHECK(tracksSection.blockIndex != nil, ());
    NSInteger index = tracksSection.blockIndex.integerValue;
    return m_sortedBlocks[index].m_trackIds.size();
  }
  
  auto const & bm = GetFramework().GetBookmarkManager();
  return bm.GetTrackIds(m_categoryId).size();
}

- (NSString *)titleOfTracksSection:(TracksSection *)tracksSection
{
  CHECK(![self isSearchMode], ());
  
  if ([self isSortMode])
  {
    CHECK(tracksSection.blockIndex != nil, ());
    NSInteger index = tracksSection.blockIndex.integerValue;
    return @(m_sortedBlocks[index].m_blockName.c_str());
  }
  
  return L(@"tracks_title");
}

- (BOOL)canEditTracksSection:(TracksSection *)tracksSection
{
  CHECK(![self isSearchMode], ());
  
  if ([self isSortMode])
    return false;
  
  return [[MWMBookmarksManager sharedManager] isCategoryEditable:m_categoryId];
}

- (kml::TrackId)tracksSection:(TracksSection *)tracksSection getTrackIdByRow:(NSInteger)row
{
  CHECK(![self isSearchMode], ());
  
  if ([self isSortMode])
  {
    CHECK(tracksSection.blockIndex != nil, ());
    NSInteger index = tracksSection.blockIndex.integerValue;
    return m_sortedBlocks[index].m_trackIds[row];
  }
  
  auto const & bm = GetFramework().GetBookmarkManager();
  auto const & trackIds = bm.GetTrackIds(m_categoryId);
  ASSERT_LESS(row, trackIds.size(), ());
  auto it = trackIds.begin();
  advance(it, row);
  return *it;
}

- (void)tracksSection:(TracksSection *)tracksSection onDeleteTrackInRow:(NSInteger)row
{
  CHECK(![self isSearchMode], ());
  
  if ([self isSortMode])
  {
    CHECK(tracksSection.blockIndex != nil, ());
    NSInteger index = tracksSection.blockIndex.integerValue;
    auto & tracks = m_sortedBlocks[index].m_trackIds;
    tracks.erase(tracks.begin() + row);
    if (tracks.empty())
      m_sortedBlocks.erase(m_sortedBlocks.begin() + index);
  }
}

#pragma mark - InfoSectionDelegate

- (UITableViewCell *)infoCellForTableView: (UITableView *)tableView
{
  UITableViewCell * cell = [tableView dequeueReusableCellWithCellClass:MWMCategoryInfoCell.class];
  CHECK(cell, ("Invalid category info cell."));
  
  auto & f = GetFramework();
  auto & bm = f.GetBookmarkManager();
  bool const categoryExists = bm.HasBmCategory(m_categoryId);
  CHECK(categoryExists, ("Nonexistent category"));
  
  auto infoCell = (MWMCategoryInfoCell *)cell;
  auto const & categoryData = bm.GetCategoryData(m_categoryId);
  [infoCell updateWithCategoryData:categoryData delegate:self];
  infoCell.expanded = self.infoExpanded;
  
  return cell;
}

#pragma mark - MWMCategoryInfoCellDelegate

- (void)categoryInfoCellDidPressMore:(MWMCategoryInfoCell *)cell
{
  [self.tableView beginUpdates];
  cell.expanded = YES;
  [self.tableView endUpdates];
  self.infoExpanded = YES;
}

#pragma mark - MWMLocationObserver

- (void)onLocationUpdate:(location::GpsInfo const &)info
{
  [self.tableView.visibleCells enumerateObjectsUsingBlock:^(UITableViewCell * cell, NSUInteger idx, BOOL * stop)
  {
    auto indexPath = [self.tableView indexPathForCell:cell];
    
    // TODO(@darina): Is it fast?
    if ([self->m_sectionsCollection[indexPath.section] respondsToSelector:@selector(updateCell:forRow:withNewLocation:)])
      [self->m_sectionsCollection[indexPath.section] updateCell:cell forRow:indexPath.row withNewLocation:info];
  }];
}

//*********** End of Location manager callbacks ********************
//******************************************************************

- (UIStatusBarStyle)preferredStatusBarStyle
{
  setStatusBarBackgroundColor(UIColor.clearColor);
  return UIStatusBarStyleLightContent;
}

- (void)viewDidLoad
{
  [super viewDidLoad];
  
  
  UIColor * searchBarColor = [UIColor primary];
  self.searchBar.delegate = self;
  self.statusBarBackground.backgroundColor = self.searchBar.barTintColor = searchBarColor;
  self.searchBar.backgroundImage = [UIImage imageWithColor:searchBarColor];
  self.searchBar.placeholder = L(@"search");
  //UITextField * textFiled = [self.searchBar valueForKey:@"searchField"];
  //UILabel * placeholder = [textFiled valueForKey:@"_placeholderLabel"];
  //placeholder.textColor = [UIColor blackHintText];
 /*
  [self.searchController.searchBar setTranslatesAutoresizingMaskIntoConstraints:NO];
 
  auto searchBar = self.searchController.searchBar;
  auto searchView = self.searchView;
  NSDictionary * views = NSDictionaryOfVariableBindings(searchBar, searchView);
  
  NSArray * horizontalConstraints = [NSLayoutConstraint constraintsWithVisualFormat:@"H:|[searchBar]|" options:0 metrics:nil views:views];
  NSArray * verticalConstraints = [NSLayoutConstraint constraintsWithVisualFormat:@"V:|[searchBar]|" options:0 metrics:nil views:views];
  NSArray * equalWidthConstraints = [NSLayoutConstraint constraintsWithVisualFormat:@"[searchBar(==searchView)]" options:0 metrics:nil views:views];
  [self.searchView addConstraints:horizontalConstraints];
  [self.searchView addConstraints:verticalConstraints];
  [self.searchView addConstraints:equalWidthConstraints];
*/
  //[self.searchController.searchBar sizeToFit];
  
  self.tableView.estimatedRowHeight = 44;
  [self.tableView registerWithCellClass:MWMCategoryInfoCell.class];
  self.tableView.separatorColor = [UIColor blackDividers];

  auto regularTitleAttributes = @{ NSFontAttributeName: [UIFont regular16],
                                   NSForegroundColorAttributeName: [UIColor linkBlue] };
  auto moreTitleAttributes = @{ NSFontAttributeName: [UIFont medium16],
                                   NSForegroundColorAttributeName: [UIColor linkBlue] };

  [self.moreItem setTitleTextAttributes:moreTitleAttributes forState:UIControlStateNormal];
  [self.sortItem setTitleTextAttributes:regularTitleAttributes forState:UIControlStateNormal];
  [self.viewOnMapItem setTitleTextAttributes:regularTitleAttributes forState:UIControlStateNormal];

  self.moreItem.title = L(@"placepage_more_button");
  // TODO(@darina) Use strings
  self.sortItem.title = @"Sort";// L(@"sharing_options");
  self.viewOnMapItem.title = L(@"search_show_on_map");


  //UITextField * textField = [self.searchBar valueForKey:@"searchField"];
  //textField.backgroundColor = [UIColor white];
  
  self.myCategoryToolbar.barTintColor = [UIColor white];
  self.downloadedCategoryToolbar.barTintColor = [UIColor white];
}

- (void)viewWillAppear:(BOOL)animated
{
  [MWMLocationManager addObserver:self];

  // Display Edit button only if table is not empty
  if ([[MWMBookmarksManager sharedManager] isCategoryEditable:m_categoryId])
  {
    self.myCategoryToolbar.hidden = NO;
    self.downloadedCategoryToolbar.hidden = YES;
    if ([[MWMBookmarksManager sharedManager] isCategoryNotEmpty:m_categoryId])
    {
      self.navigationItem.rightBarButtonItem = self.editButtonItem;
      self.sortItem.enabled = YES;
    }
    else
    {
      self.sortItem.enabled = NO;
    }
  }
  else
  {
    self.myCategoryToolbar.hidden = YES;
    self.downloadedCategoryToolbar.hidden = NO;
  }

  [super viewWillAppear:animated];

  auto const & bm = GetFramework().GetBookmarkManager();
  self.title = @(bm.GetCategoryName(m_categoryId).c_str());
}

- (void)viewWillDisappear:(BOOL)animated
{
  [MWMLocationManager removeObserver:self];

  // Save possibly edited set name
  [super viewWillDisappear:animated];
}

- (void)viewDidAppear:(BOOL)animated
{
  // Disable all notifications in BM on appearance of this view.
  // It allows to significantly improve performance in case of bookmarks
  // modification. All notifications will be sent on controller's disappearance.
  [[MWMBookmarksManager sharedManager] setNotificationsEnabled: NO];
  
  [super viewDidAppear:animated];
}

- (void)viewDidDisappear:(BOOL)animated
{
  // Allow to send all notifications in BM.
  [[MWMBookmarksManager sharedManager] setNotificationsEnabled: YES];
  
  [super viewDidDisappear:animated];
}

- (void)setEditing:(BOOL)editing animated:(BOOL)animated
{
  [super setEditing:editing animated:animated];
  [self.tableView setEditing:editing animated:animated];
}

- (NSString *)categoryFileName
{
  return @(GetFramework().GetBookmarkManager().GetCategoryFileName(m_categoryId).c_str());
}

- (void)calculateSections
{
  [m_sectionsCollection removeAllObjects];
  
  if ([self isSearchMode])
  {
    [m_sectionsCollection addObject:[[BookmarksSection alloc] initWithDelegate:self]];
    return;
  }
  
  if ([self isSortMode])
  {
    NSInteger blockIndex = 0;
    for (auto const & block : m_sortedBlocks)
    {
      if (!block.m_markIds.empty())
        [m_sectionsCollection addObject:[[BookmarksSection alloc] initWithBlockIndex:@(blockIndex++) delegate:self]];
    }
    return;
  }
  
  auto const & bm = GetFramework().GetBookmarkManager();
  if (bm.IsCategoryFromCatalog(m_categoryId))
  {
    [m_sectionsCollection addObject:[[InfoSection alloc] initWithDelegate:self]];
  }
  
  if (bm.GetTrackIds(m_categoryId).size() > 0)
  {
    [m_sectionsCollection addObject:[[TracksSection alloc] initWithDelegate:self]];
  }
  
  if (bm.GetUserMarkIds(m_categoryId).size() > 0)
    [m_sectionsCollection addObject:[[BookmarksSection alloc] initWithDelegate:self]];
}

- (IBAction)onMore:(UIBarButtonItem *)sender
{
  auto actionSheet = [UIAlertController alertControllerWithTitle:nil
                                                         message:nil
                                                  preferredStyle:UIAlertControllerStyleActionSheet];
  
  [actionSheet addAction:[UIAlertAction actionWithTitle:L(@"sharing_options")
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction * _Nonnull action)
                          {
                            [self shareCategory];
                            [Statistics logEvent:kStatBookmarksListItemSettings withParameters:@{kStatOption : kStatSharingOptions}];
                          }]];
  
  [actionSheet addAction:[UIAlertAction actionWithTitle:L(@"search_show_on_map")
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction * _Nonnull action)
                          {
                            [self viewOnMap];
                            [Statistics logEvent:kStatBookmarksListItemMoreClick withParameters:@{kStatOption : kStatViewOnMap}];
                          }]];

  [actionSheet addAction:[UIAlertAction actionWithTitle:L(@"list_settings")
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction * _Nonnull action)
                          {
                            [self openCategorySettings];
                            [Statistics logEvent:kStatBookmarksListItemMoreClick withParameters:@{kStatOption : kStatSettings}];
                          }]];

  [actionSheet addAction:[UIAlertAction actionWithTitle:L(@"export_file")
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction * _Nonnull action)
                          {
                            [self exportFile];
                            [Statistics logEvent:kStatBookmarksListItemMoreClick withParameters:@{kStatOption : kStatSendAsFile}];
                          }]];

  auto deleteAction = [UIAlertAction actionWithTitle:L(@"delete_list")
                                               style:UIAlertActionStyleDestructive
                                             handler:^(UIAlertAction * _Nonnull action)
                       {
                         [[MWMBookmarksManager sharedManager] deleteCategory:self->m_categoryId];
                         [self.delegate bookmarksVCdidDeleteCategory:self];
                         [Statistics logEvent:kStatBookmarksListItemMoreClick withParameters:@{kStatOption : kStatDelete}];
                       }];
  deleteAction.enabled = [[MWMBookmarksManager sharedManager] userCategories].count > 1;
  [actionSheet addAction:deleteAction];

  [actionSheet addAction:[UIAlertAction actionWithTitle:L(@"cancel")
                                                  style:UIAlertActionStyleCancel
                                                handler:nil]];

  actionSheet.popoverPresentationController.barButtonItem = self.moreItem;
  [self presentViewController:actionSheet animated:YES completion:^{
    actionSheet.popoverPresentationController.passthroughViews = nil;
  }];
  [Statistics logEvent:kStatBookmarksListItemSettings withParameters:@{kStatOption : kStatMore}];
}

- (void)sort: (BookmarkManager::SortingType)type
{
  auto const & bm = GetFramework().GetBookmarkManager();
  
  bool hasMyPosition = false;
  m2::PointD myPosition = m2::PointD::Zero();
  
  if (type == BookmarkManager::SortingType::ByDistance)
  {
    CLLocation * lastLocation = [MWMLocationManager lastLocation];
    if (!lastLocation)
      return;
    hasMyPosition = true;
    myPosition = MercatorBounds::FromLatLon(lastLocation.coordinate.latitude, lastLocation.coordinate.longitude);
  }
  
  m_sortedBlocks = bm.GetSortedBookmarkIds(m_categoryId, type, hasMyPosition, myPosition);
  [self calculateSections];
  [self.tableView reloadData];
}

- (IBAction)onSort:(UIBarButtonItem *)sender
{
  auto actionSheet = [UIAlertController alertControllerWithTitle:nil
                                                         message:nil
                                                  preferredStyle:UIAlertControllerStyleActionSheet];
  
  auto const & bm = GetFramework().GetBookmarkManager();
  
  CLLocation * lastLocation = [MWMLocationManager lastLocation];
  bool const hasMyPosition = lastLocation != nil;
  m2::PointD myPosition = m2::PointD::Zero();
  if (hasMyPosition)
    myPosition = MercatorBounds::FromLatLon(lastLocation.coordinate.latitude, lastLocation.coordinate.longitude);

  
  auto const sortingTypes = bm.GetAvailableSortingTypes(m_categoryId, hasMyPosition, myPosition);
  
  for (auto type : sortingTypes)
  {
    NSString * typeStr = @(DebugPrint(type).c_str());
    [actionSheet addAction:[UIAlertAction actionWithTitle:typeStr
                                                    style:UIAlertActionStyleDefault
                                                  handler:^(UIAlertAction * _Nonnull action)
                            {
                              [self sort:type];
                            }]];
  }
  
  // TODO(@darina) Use key from strings!
  [actionSheet addAction:[UIAlertAction actionWithTitle:L(@"By default")
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction * _Nonnull action)
                          {
                            self->m_sortedBlocks.clear();
                            [self calculateSections];
                            [self.tableView reloadData];
                          }]];
  
  [actionSheet addAction:[UIAlertAction actionWithTitle:L(@"cancel")
                                                  style:UIAlertActionStyleCancel
                                                handler:nil]];
  
  actionSheet.popoverPresentationController.barButtonItem = self.sortItem;
  [self presentViewController:actionSheet animated:YES completion:^{
    actionSheet.popoverPresentationController.passthroughViews = nil;
  }];
}

- (IBAction)onViewOnMap:(UIBarButtonItem *)sender
{
  [self viewOnMap];
}

- (void)openCategorySettings
{
  auto storyboard = [UIStoryboard instance:MWMStoryboardCategorySettings];
  auto settingsController = (CategorySettingsViewController *)[storyboard instantiateInitialViewController];
  settingsController.delegate = self;
  settingsController.category = [[MWMBookmarksManager sharedManager] categoryWithId:m_categoryId];
  [self.navigationController pushViewController:settingsController animated:YES];
}

- (void)exportFile
{
  [[MWMBookmarksManager sharedManager] addObserver:self];
  [[MWMBookmarksManager sharedManager] shareCategory:m_categoryId];
}

- (void)shareCategory
{
  auto storyboard = [UIStoryboard instance:MWMStoryboardSharing];
  auto shareController = (BookmarksSharingViewController *)[storyboard instantiateInitialViewController];
  shareController.delegate = self;
  shareController.category = [[MWMBookmarksManager sharedManager] categoryWithId:m_categoryId];
  [self.navigationController pushViewController:shareController animated:YES];
}

- (void)viewOnMap
{
  [self.navigationController popToRootViewControllerAnimated:YES];
  GetFramework().ShowBookmarkCategory(m_categoryId);
}

#pragma mark - MWMBookmarksObserver

- (void)onBookmarksCategoryFilePrepared:(MWMBookmarksShareStatus)status
{
  switch (status)
  {
    case MWMBookmarksShareStatusSuccess:
    {
      auto shareController =
      [MWMActivityViewController shareControllerForURL:[MWMBookmarksManager sharedManager].shareCategoryURL
                                               message:L(@"share_bookmarks_email_body")
                                     completionHandler:^(UIActivityType  _Nullable activityType,
                                                         BOOL completed,
                                                         NSArray * _Nullable returnedItems,
                                                         NSError * _Nullable activityError) {
                                       [[MWMBookmarksManager sharedManager] finishShareCategory];
                                     }];
      [shareController presentInParentViewController:self anchorView:self.view];
      break;
    }
    case MWMBookmarksShareStatusEmptyCategory:
      [[MWMAlertViewController activeAlertController] presentInfoAlert:L(@"bookmarks_error_title_share_empty")
                                                                  text:L(@"bookmarks_error_message_share_empty")];
      break;
    case MWMBookmarksShareStatusArchiveError:
    case MWMBookmarksShareStatusFileError:
      [[MWMAlertViewController activeAlertController] presentInfoAlert:L(@"dialog_routing_system_error")
                                                                  text:L(@"bookmarks_error_message_share_general")];
      break;
  }
  [[MWMBookmarksManager sharedManager] removeObserver:self];
}

#pragma mark - BookmarksSharingViewControllerDelegate

- (void)didShareCategory
{
  [self.tableView reloadData];
}

#pragma mark - CategorySettingsViewControllerDelegate

- (void)categorySettingsController:(CategorySettingsViewController *)viewController didDelete:(MWMMarkGroupID)categoryId
{
  [self.delegate bookmarksVCdidDeleteCategory:self];
}

- (void)categorySettingsController:(CategorySettingsViewController *)viewController didEndEditing:(MWMMarkGroupID)categoryId
{
  [self.navigationController popViewControllerAnimated:YES];
  [self.delegate bookmarksVCdidUpdateCategory:self];
  [self.tableView reloadData];
}

#pragma mark - UISearchBarDelegate

- (BOOL)searchBarShouldBeginEditing:(UISearchBar *)searchBar
{
  [self.searchBar setShowsCancelButton:YES animated:YES];
  [self.navigationController setNavigationBarHidden:YES animated:YES];
  self.tableView.contentInset = self.tableView.scrollIndicatorInsets = {};
  return YES;
}

- (BOOL)searchBarShouldEndEditing:(UISearchBar *)searchBar
{
  [self.searchBar setShowsCancelButton:NO animated:YES];
  [self.navigationController setNavigationBarHidden:NO animated:YES];
  self.tableView.contentInset = self.tableView.scrollIndicatorInsets = {};
  return YES;
}

- (void)searchBarCancelButtonClicked:(UISearchBar *)searchBar
{
  self.searchBar.text = @"";
  [self.searchBar resignFirstResponder];
  
  [self calculateSections];
  [self.tableView reloadData];
}

- (void)searchBar:(UISearchBar *)searchBar textDidChange:(NSString *)searchText
{
  if (!searchText || searchText.length == 0)
  {
    GetFramework().CancelSearch(search::Mode::Bookmarks);
    m_searchResults.clear();
    [self calculateSections];
    [self.tableView reloadData];
    return;
  }
  
  m_searchParams.m_query = searchText.UTF8String;
  m_searchParams.m_categoryId = m_categoryId;
  
  auto const searchId = ++self.lastSearchId;
  __weak auto weakSelf = self;
  m_searchParams.m_onStarted = [] {};
  m_searchParams.m_onResults = [weakSelf, searchId](search::BookmarksSearchParams::Results const & results,
                                                    search::BookmarksSearchParams::Status status) {
    __strong auto self = weakSelf;
    if (!self || searchId != self.lastSearchId)
      return;
    
    if (status != search::BookmarksSearchParams::Status::Cancelled)
      self->m_searchResults = results;
    else
      self->m_searchResults.clear();
    
    [self calculateSections];
    [self.tableView reloadData];
  };
  GetFramework().SearchInBookmarks(m_searchParams);
}
@end
