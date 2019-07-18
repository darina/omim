#pragma once

#include "search/city_finder.hpp"
#include "search/reverse_geocoder.hpp"

#include "storage/country_info_getter.hpp"
#include "storage/country_name_getter.hpp"

namespace search
{
class RegionAddressGetter
{
public:
  RegionAddressGetter(DataSource const & dataSource, storage::CountryInfoGetter const & infoGetter,
                      storage::CountryNameGetter const & nameGetter, CityFinder & cityFinder)
    : m_reverseGeocoder(dataSource)
    , m_infoGetter(infoGetter)
    , m_nameGetter(nameGetter)
    , m_cityFinder(cityFinder)
  {}

  ReverseGeocoder::RegionAddress GetNearbyRegionAddress(m2::PointD const & center);
  std::string GetLocalizedRegionAdress(ReverseGeocoder::RegionAddress const & addr) const;

private:
  ReverseGeocoder m_reverseGeocoder;
  storage::CountryInfoGetter const & m_infoGetter;
  storage::CountryNameGetter const & m_nameGetter;
  CityFinder & m_cityFinder;
};
}  // namespace search
