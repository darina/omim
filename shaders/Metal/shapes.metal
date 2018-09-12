#include <metal_stdlib>
#include <simd/simd.h>
#include "shaders_lib.h"

using namespace metal;

typedef struct
{
  float4x4 u_modelView;
  float4x4 u_projection;
  float4x4 u_pivotTransform;
  packed_float3 u_position;
  float u_accuracy;
  float u_zScale;
  float u_opacity;
  float u_azimut;
} Uniforms_T;

typedef struct
{
  float2 a_normal [[attribute(0)]];
  float2 a_colorTexCoords [[attribute(1)]];
} Vertex_T;

typedef struct
{
  float4 position [[position]];
  half4 color;
} Fragment_T;

vertex Fragment_T vsAccuracy(const Vertex_T in [[stage_in]],
                             constant Uniforms_T & uniforms [[buffer(1)]],
                             texture2d<half> u_colorTex [[texture(0)]],
                             sampler u_colorTexSampler [[sampler(0)]])
{
  float3 uPosition = uniforms.u_position;
  float4 position = float4(uPosition.xy, 0.0, 1.0) * uniforms.u_modelView;
  float4 normal = float4(0.0, 0.0, 0.0, 0.0);
  if (dot(in.a_normal, in.a_normal) != 0)
    normal = float4(normalize(in.a_normal) * uniforms.u_accuracy, 0.0, 0.0);
  position = (position + normal) * uniforms.u_projection;
 
  Fragment_T out;
  out.position = applyPivotTransform(position, uniforms.u_pivotTransform, uPosition.z * uniforms.u_zScale);
  half4 color = u_colorTex.sample(u_colorTexSampler, in.a_colorTexCoords);
  color.a *= uniforms.u_opacity;
  out.color = color;
  return out;
}

vertex Fragment_T vsMyPosition(const Vertex_T in [[stage_in]],
                               constant Uniforms_T & uniforms [[buffer(1)]],
                               texture2d<half> u_colorTex [[texture(0)]],
                               sampler u_colorTexSampler [[sampler(0)]])
{
  float sinV = sin(uniforms.u_azimut);
  float cosV = cos(uniforms.u_azimut);
  
  float4x4 rotation;
  rotation[0] = float4(cosV, sinV, 0.0, 0.0);
  rotation[1] = float4(-sinV, cosV, 0.0, 0.0);
  rotation[2] = float4(0.0, 0.0, 1.0, 0.0);
  rotation[3] = float4(0.0, 0.0, 0.0, 1.0);
  
  float4 pos = float4(uniforms.u_position, 1.0) * uniforms.u_modelView;
  float4 normal = float4(in.a_normal, 0, 0);
  float4 shiftedPos = normal * rotation + pos;

  Fragment_T out;
  out.position = applyPivotTransform(shiftedPos * uniforms.u_projection, uniforms.u_pivotTransform, 0.0);
  half4 color = u_colorTex.sample(u_colorTexSampler, in.a_colorTexCoords);
  color.a *= uniforms.u_opacity;
  out.color = color;
  return out;
}

fragment half4 fsTexturingShapes(const Fragment_T in [[stage_in]])
{
  return in.color;
}
