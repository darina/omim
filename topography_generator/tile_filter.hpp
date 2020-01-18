#pragma once

#include "topography_generator/utils/values_provider.hpp"

namespace topography_generator
{
template <typename ValueType>
void GetExtendedTile(ms::LatLon const & leftBottom, size_t stepsInDegree,
                     size_t tileSize, size_t tileSizeExtension,
                     ValuesProvider<ValueType> & valuesProvider,
                     std::vector<ValueType> & extTileValues)
{
  size_t const extendedTileSize = tileSize + 2 * tileSizeExtension;
  extTileValues.resize(extendedTileSize * extendedTileSize);

  double const step = 1.0 / stepsInDegree;
  double const offset = step * tileSizeExtension;

  // Store values from North to South.
  ms::LatLon startPos = ms::LatLon(leftBottom.m_lat + 1.0 + offset,
                                   leftBottom.m_lon - offset);
  for (size_t i = 0; i < extendedTileSize; ++i)
  {
    for (size_t j = 0; j < extendedTileSize; ++j)
    {
      auto const pos = ms::LatLon(startPos.m_lat - i * step,
                                  startPos.m_lon + j * step);
      extTileValues[i * extendedTileSize + j] = valuesProvider.GetValue(pos);
    }
  }
}



template <typename ValueType>
void ProcessWithLinearKernel(std::vector<double> const & kernel, size_t tileSize, size_t tileOffset,
                             std::vector<ValueType> const & srcValues,
                             std::vector<ValueType> & dstValues)
{
  auto const kernelSize = kernel.size();
  auto const kernelRadius = kernel.size() / 2;
  CHECK_LESS_OR_EQUAL(kernelRadius, tileOffset, ());
  CHECK_GREATER(tileSize, tileOffset * 2, ());

  std::vector<ValueType> tempValues(tileSize, 0);

  for (size_t i = tileOffset; i < tileSize - tileOffset; ++i)
  {
    for (size_t j = tileOffset; j < tileSize - tileOffset; ++j)
    {
      for (size_t k = 0; k < kernelSize; ++k)
      {
        size_t const srcIndex = i * tileSize + j - kernelRadius + k;
        tempValues[j] += kernel[k] * srcValues[srcIndex];
      }
    }
    for (size_t j = tileOffset; j < tileSize - tileOffset; ++j)
    {
      dstValues[i * tileSize + j] = tempValues[j];
    }
  }

  for (size_t j = tileOffset; j < tileSize - tileOffset; ++j)
  {
    for (size_t i = tileOffset; i < tileSize - tileOffset; ++i)
    {
      for (size_t k = 0; k < kernelSize; ++k)
      {
        size_t const srcIndex = (i - kernelRadius + k) * tileSize + j;
        tempValues[i] += kernel[k] * dstValues[srcIndex];
      }
    }
    for (size_t i = tileOffset; i < tileSize - tileOffset; ++i)
    {
      dstValues[i * tileSize + j] = tempValues[i];
    }
  }
}

template <typename ValueType>
void ProcessWithSquareKernel(std::vector<double> const & kernel, size_t kernelSize,
                             size_t tileSize, size_t tileOffset,
                             std::vector<ValueType> const & srcValues,
                             std::vector<ValueType> & dstValues)
{
  CHECK_EQUAL(kernelSize * kernelSize, kernel.size(), ());
  size_t const kernelRadius = kernelSize / 2;
  CHECK_LESS_OR_EQUAL(kernelRadius, tileOffset, ());
  CHECK_GREATER(tileSize, tileOffset * 2, ());

  for (size_t i = tileOffset; i < tileSize - tileOffset; ++i)
  {
    for (size_t j = tileOffset; j < tileSize - tileOffset; ++j)
    {
      size_t const dstIndex = i * tileSize + j;
      dstValues[dstIndex] = 0;
      for (size_t ki = 0; ki < kernelSize; ++ki)
      {
        for (size_t kj = 0; kj < kernelSize; ++kj)
        {
          size_t const srcIndex = (i - kernelRadius + ki) * tileSize + j - kernelRadius + kj;
          dstValues[dstIndex] += kernel[ki * kernelSize + kj] * srcValues[srcIndex];
        }
      }
    }
  }
}

template <typename ValueType>
class Filter
{
public:
  virtual ~Filter() = default;

  virtual size_t GetKernelRadius() const = 0;
  virtual void Process(size_t tileSize, size_t tileOffset,
               std::vector<ValueType> const & srcValues,
               std::vector<ValueType> & dstValues) const = 0;
};

template <typename ValueType>
class MedianFilter : public Filter<ValueType>
{
public:
  explicit MedianFilter(size_t kernelRadius)
    : m_kernelRadius(kernelRadius)
  {}

  size_t GetKernelRadius() const override { return m_kernelRadius; }

  void Process(size_t tileSize, size_t tileOffset,
               std::vector<ValueType> const & srcValues,
               std::vector<ValueType> & dstValues) const override
  {
    CHECK_LESS_OR_EQUAL(m_kernelRadius, tileOffset, ());
    CHECK_GREATER(tileSize, tileOffset * 2, ());

    size_t const kernelSize = m_kernelRadius * 2 + 1;
    std::vector<ValueType> kernel(kernelSize * kernelSize);
    for (size_t i = tileOffset; i < tileSize - tileOffset; ++i)
    {
      for (size_t j = tileOffset; j < tileSize - tileOffset; ++j)
      {
        size_t const startI = i - m_kernelRadius;
        size_t const startJ = j - m_kernelRadius;
        for (size_t ki = 0; ki < kernelSize; ++ki)
        {
          for (size_t kj = 0; kj < kernelSize; ++kj)
          {
            size_t const srcIndex = (startI + ki) * tileSize + startJ + kj;
            kernel[ki * kernelSize + kj] = srcValues[srcIndex];
          }
        }
        std::sort(kernel.begin(), kernel.end());
        dstValues[i * tileSize + j] = kernel[m_kernelRadius];
      }
    }
  }

private:
  size_t m_kernelRadius;
};

template <typename ValueType>
class GaussianFilter : public Filter<ValueType>
{
public:
  GaussianFilter(double standardDeviation, double radiusFactor)
  {
    CalculateLinearKernel(standardDeviation, radiusFactor);
  }

  size_t GetKernelRadius() const override { return m_kernel.size() / 2; }

  void Process(size_t tileSize, size_t tileOffset,
               std::vector<ValueType> const & srcValues,
               std::vector<ValueType> & dstValues) const override
  {
    ProcessWithLinearKernel(m_kernel, tileSize, tileOffset, srcValues, dstValues);
  }

private:
  void CalculateLinearKernel(double standardDeviation, double radiusFactor)
  {
    auto const kernelRadius = static_cast<size_t>(ceil(radiusFactor * standardDeviation));
    auto const kernelSize = 2 * kernelRadius + 1;
    std::vector<double> linearKernel(kernelSize, 0);

    double sum = 1.0;
    linearKernel[kernelRadius] = 1.0;
    for (int i = 1; i <= kernelRadius; ++i)
    {
      double const val = exp(-i * i / (2 * standardDeviation * standardDeviation));
      linearKernel[kernelRadius - i] = linearKernel[kernelRadius + i] = val;
      sum += 2.0 * val;
    }
    for (auto & val : linearKernel)
      val /= sum;

    m_kernel.swap(linearKernel);
  }

  std::vector<double> m_kernel;
};

template <typename ValueType>
using FiltersSequence = std::vector<std::unique_ptr<Filter<ValueType>>>;

template <typename ValueType>
std::vector<ValueType> FilterTile(FiltersSequence<ValueType> const & filters,
                                  ms::LatLon const & leftBottom,
                                  size_t stepsInDegree, size_t tileSize,
                                  ValuesProvider<ValueType> & valuesProvider)
{
  size_t combinedOffset = 0;
  for (auto const & filter : filters)
    combinedOffset += filter->GetKernelRadius();

  std::vector<ValueType> extTileValues;
  GetExtendedTile(leftBottom, stepsInDegree, tileSize, combinedOffset, valuesProvider, extTileValues);

  std::vector<ValueType> extTileValues2(extTileValues.size());

  size_t const extTileSize = tileSize + 2 * combinedOffset;
  CHECK_EQUAL(extTileSize * extTileSize, extTileValues.size(), ());

  size_t currentOffset = 0;
  for (auto const & filter : filters)
  {
    currentOffset += filter->GetKernelRadius();
    filter->Process(extTileSize, currentOffset, extTileValues, extTileValues2);
    extTileValues.swap(extTileValues2);
  }

  std::vector<ValueType> result(tileSize * tileSize);
  for (size_t i = combinedOffset; i < extTileSize - combinedOffset; ++i)
  {
    for (size_t j = combinedOffset; j < extTileSize - combinedOffset; ++j)
    {
      size_t const dstIndex = (i - combinedOffset) * tileSize + j - combinedOffset;
      result[dstIndex] = extTileValues[i * extTileSize + j];
    }
  }
  return result;
}
}  // namespace topography_generator
