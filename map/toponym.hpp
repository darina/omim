#pragma once

#include "indexer/feature_decl.hpp"
#include "indexer/mwm_set.hpp"

#include "geometry/point2d.hpp"

struct Toponym
{
  bool IsValid() const { return m_mwmId.IsAlive() || m_featureId.IsValid(); }

  bool operator==(Toponym const & rhs) const { return m_mwmId == rhs.m_mwmId && m_featureId == rhs.m_featureId; }
  bool operator!=(Toponym const & rhs) const { return !(*this == rhs); }
  bool operator<(Toponym const & rhs) const
  {
    if (m_mwmId < rhs.m_mwmId)
      return true;
    return m_featureId < rhs.m_featureId;
  }

  MwmSet::MwmId m_mwmId;
  FeatureID m_featureId;
};

Toponym GetToponym(m2::PointD const & pos);
std::string GetLocalizedToponymName(Toponym const & toponym);
