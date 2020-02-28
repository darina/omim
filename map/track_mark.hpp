#pragma once

#include "map/user_mark.hpp"

class TrackInfoMark : public UserMark
{
public:
  explicit TrackInfoMark(m2::PointD const & ptOrg);

  void SetOffset(m2::PointF const & offset);
  void SetPosition(m2::PointD const & ptOrg);
  void SetIsVisible(bool isVisible);

  void SetTrackId(kml::TrackId trackId);
  kml::TrackId GetTrackId() const { return m_trackId; }

  drape_ptr<SymbolNameZoomInfo> GetSymbolNames() const override;
  drape_ptr<SymbolOffsets> GetSymbolOffsets() const override;
  dp::Anchor GetAnchor() const override { return dp::Bottom; }
  bool IsVisible() const override { return m_isVisible; }

private:
  m2::PointF m_offset;
  bool m_isVisible = false;
  kml::TrackId m_trackId = kml::kInvalidTrackId;
};

class TrackSelectionMark : public UserMark
{
public:
  explicit TrackSelectionMark(m2::PointD const & ptOrg);

  void SetPosition(m2::PointD const & ptOrg);

  void SetTrackId(kml::TrackId trackId);
  kml::TrackId GetTrackId() const { return m_trackId; }

private:
  uint32_t m_distance = 0;
  kml::TrackId m_trackId = kml::kInvalidTrackId;
};
