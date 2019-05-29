#import "MWMTypes.h"

#include "platform/location.hpp"

#include "kml/type_utils.hpp"

@protocol TableSectionDelegate <NSObject>
- (NSInteger)numberOfRows;
- (NSString *)title;
- (BOOL)canEdit;
- (UITableViewCell *)tableView: (UITableView *)tableView cellForRow: (NSInteger)row;
- (BOOL)didSelectRow: (NSInteger)row;
- (void)deleteRow: (NSInteger)row;

@optional
-(void)updateCell: (UITableViewCell *)cell forRow:(NSInteger)row withNewLocation: (location::GpsInfo const &)gpsInfo;
@end

@protocol BookmarksSectionDelegate <NSObject>
- (NSInteger)numberOfBookmarks;
- (NSString *)titleOfBookmarksSection;
- (BOOL)canEditBookmarksSection;
- (kml::MarkId)getBookmarkIdByRow:(NSInteger)row;
@end

@protocol TracksSectionDelegate <NSObject>
- (NSInteger)numberOfTracks;
- (NSString *)titleOfTracksSection;
- (BOOL)canEditTracksSection;
- (kml::MarkId)getTrackIdByRow: (NSInteger)row;
@end

@protocol InfoSectionDelegate <NSObject>
- (UITableViewCell *)infoCellForTableView: (UITableView *)tableView;
@end

@interface BookmarksSection : NSObject <TableSectionDelegate>

@property (weak, nonatomic) id<BookmarksSectionDelegate> delegate;

- (instancetype)initWithDelegate: (id<BookmarksSectionDelegate>)delegate;

@end

@interface TracksSection : NSObject <TableSectionDelegate>

@property (weak, nonatomic) id<TracksSectionDelegate> delegate;

-(instancetype)initWithDelegate: (id<TracksSectionDelegate>)delegate;

@end

@interface InfoSection : NSObject <TableSectionDelegate>

@property (weak, nonatomic) id<InfoSectionDelegate> delegate;

-(instancetype)initWithDelegate: (id<InfoSectionDelegate>)delegate;

@end
