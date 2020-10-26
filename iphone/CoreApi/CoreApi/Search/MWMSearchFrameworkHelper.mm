#import "MWMSearchFrameworkHelper.h"
#import "CoreBanner+Core.h"

#include "partners_api/ads/ads_engine.hpp"
#include "partners_api/megafon_countries.hpp"

#include "platform/preferred_languages.hpp"

#include "Framework.h"

@implementation MWMSearchFrameworkHelper

- (NSArray<NSString *> *)searchCategories
{
  NSMutableArray * result = [NSMutableArray array];
  auto const & categories = GetFramework().GetDisplayedCategories().GetKeys();
  for (auto const & item : categories)
  {
    [result addObject:@(item.c_str())];
  }
  return [result copy];
}

- (BOOL)hasMegafonCategoryBanner
{
  auto & f = GetFramework();
  auto const & purchase = f.GetPurchase();
  if (purchase && purchase->IsSubscriptionActive(SubscriptionType::RemoveAds))
    return NO;
  
  auto const position = f.GetCurrentPosition();
  if (!position)
    return NO;

  if (GetPlatform().ConnectionStatus() == Platform::EConnectionType::CONNECTION_NONE)
    return NO;

  auto const latLon = mercator::ToLatLon(*position);
  return ads::HasMegafonCategoryBanner(f.GetStorage(), f.GetTopmostCountries(latLon),
                                       languages::GetCurrentNorm());
}

- (NSURL *)megafonBannerUrl
{
  auto urlStr = ads::GetMegafonCategoryBannerUrl();
  return [NSURL URLWithString:@(urlStr.c_str())];
}

- (id<MWMBanner>)searchCategoryBanner
{
  if (GetPlatform().ConnectionStatus() == Platform::EConnectionType::CONNECTION_NONE)
    return nil;
    
  auto const & f = GetFramework();
  auto const pos = f.GetCurrentPosition();
  auto const banners = f.GetAdsEngine().GetSearchCategoryBanners(pos);

  if (banners.empty())
    return nil;
    
  return [[CoreBanner alloc] initWithAdBanner:banners.front()];
}

- (BOOL)isSearchHistoryEmpty
{
  return GetFramework().GetSearchAPI().GetLastSearchQueries().empty();
}

- (NSArray<NSString *> *)lastSearchQueries
{
  NSMutableArray * result = [NSMutableArray array];
  auto const & queries = GetFramework().GetSearchAPI().GetLastSearchQueries();
  for (auto const & item : queries)
  {
    [result addObject:@(item.second.c_str())];
  }
  return [result copy];
}

- (void)clearSearchHistory
{
  GetFramework().GetSearchAPI().ClearSearchHistory();
}

@end
