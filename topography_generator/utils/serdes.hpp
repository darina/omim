#pragma once

#include "topography_generator/utils/contours.hpp"

#include "coding/geometry_coding.hpp"
#include "coding/reader.hpp"

#include "geometry/mercator.hpp"

namespace topography_generator
{
template <typename ValueType>
class SerializerContours
{
public:
  explicit SerializerContours(Contours<ValueType> && contours): m_contours(std::move(contours)) {}

  template <typename Sink>
  void Serialize(Sink & sink)
  {
    WriteToSink(sink, m_contours.m_minValue);
    WriteToSink(sink, m_contours.m_maxValue);
    WriteToSink(sink, m_contours.m_valueStep);

    WriteToSink(sink, static_cast<uint32_t>(m_contours.m_contours.size()));
    for (auto const & contours : m_contours.m_contours)
    {
      SerializeContours(sink, contours);
    }
  }
private:
  template <typename Sink>
  void SerializeContours(Sink & sink, std::vector<topography_generator::Contour> const & contours)
  {
    WriteToSink(sink, static_cast<uint32_t>(contours.size()));
    for (auto const & contour : contours)
    {
      SerializeContour(sink, contour);
    }
  }

  template <typename Sink>
  void SerializeContour(Sink & sink, topography_generator::Contour const & contour)
  {
    WriteToSink(sink, static_cast<uint32_t>(contour.size()));
    sink.Write(contour.data(), contour.size() * sizeof(contour[0]));
    /*
    serial::GeometryCodingParams codingParams(kFeatureSorterPointCoordBits, 0);
    codingParams.SetBasePoint(mercator::FromLatLon(contour[0]));
    std::vector<m2::PointD> toSave;
    toSave.reserve(contour.size() - 1);
    for (size_t i = 1; i < contour.size(); ++i)
    {
      toSave.push_back(mercator::FromLatLon(contour[i]));
    }
    serial::SaveInnerPath(toSave, codingParams, sink);
    */
  }

  Contours<ValueType> m_contours;
};

template <typename ValueType>
class DeserializerContours
{
public:
  template <typename Reader>
  void Deserialize(Reader & reader, Contours<ValueType> & contours)
  {
    NonOwningReaderSource source(reader);
    contours.m_minValue = ReadPrimitiveFromSource<ValueType>(source);
    contours.m_maxValue = ReadPrimitiveFromSource<ValueType>(source);
    contours.m_valueStep = ReadPrimitiveFromSource<ValueType>(source);

    size_t const levelsCount = ReadPrimitiveFromSource<uint32_t>(source);
    contours.m_contours.resize(levelsCount);
    for (auto & levelContours : contours.m_contours)
      DeserializeContours(source, levelContours);
  }

private:
  void DeserializeContours(NonOwningReaderSource & source,
                            std::vector<topography_generator::Contour> & contours)
  {
    size_t const contoursCount = ReadPrimitiveFromSource<uint32_t>(source);
    contours.resize(contoursCount);
    for (auto & contour : contours)
      DeserializeContour(source, contour);
  }

  void DeserializeContour(NonOwningReaderSource & source,
                          topography_generator::Contour & contour)
  {
    size_t const pointsCount = ReadPrimitiveFromSource<uint32_t>(source);
    contour.resize(pointsCount);
    source.Read(contour.data(), pointsCount * sizeof(contour[0]));
  }
};
}  // namespace topography_generator
