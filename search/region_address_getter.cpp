#include "search/region_address_getter.hpp"

namespace search
{
ReverseGeocoder::RegionAddress RegionAddressGetter::GetNearbyRegionAddress(m2::PointD const & center)
{
  return m_reverseGeocoder.GetNearbyRegionAddress(center, m_infoGetter, m_cityFinder);
}

std::string RegionAddressGetter::GetLocalizedRegionAdress(ReverseGeocoder::RegionAddress const & addr) const
{
  return m_reverseGeocoder.GetLocalizedRegionAdress(addr, m_nameGetter);
}
}  // namespace search
