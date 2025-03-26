#include "tiny2D.h"

//#pragma pack_matrix(row_major)

struct ViewParms
{
    float4x4 viewProjMatrix;
    float2 viewSize;
};

ConstantBuffer<ViewParms> viewParms : register(b0);

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

struct Vertex
{
    float3 position;
    float3 normal;
    float2 uv;
};

static const Vertex s_BoxVertices[36] =
{
    // Front Face
    { float3(-0.5, -0.5,  0.5), float3(0,  0,  1), float2(0, 1) },
    { float3(0.5, -0.5,  0.5),  float3(0,  0,  1), float2(1, 1) },
    { float3(0.5,  0.5,  0.5),  float3(0,  0,  1), float2(1, 0) },
    { float3(-0.5, -0.5,  0.5), float3(0,  0,  1), float2(0, 1) },
    { float3(0.5,  0.5,  0.5),  float3(0,  0,  1), float2(1, 0) },
    { float3(-0.5,  0.5,  0.5), float3(0,  0,  1), float2(0, 0) },

    // Back Face
    { float3(0.5, -0.5, -0.5),  float3(0,  0, -1), float2(0, 1) },
    { float3(-0.5, -0.5, -0.5), float3(0,  0, -1), float2(1, 1) },
    { float3(-0.5,  0.5, -0.5), float3(0,  0, -1), float2(1, 0) },
    { float3(0.5, -0.5, -0.5),  float3(0,  0, -1), float2(0, 1) },
    { float3(-0.5,  0.5, -0.5), float3(0,  0, -1), float2(1, 0) },
    { float3(0.5,  0.5, -0.5),  float3(0,  0, -1), float2(0, 0) },

    // Left Face
    { float3(-0.5, -0.5, -0.5), float3(-1,  0,  0), float2(0, 1) },
    { float3(-0.5, -0.5,  0.5), float3(-1,  0,  0), float2(1, 1) },
    { float3(-0.5,  0.5,  0.5), float3(-1,  0,  0), float2(1, 0) },
    { float3(-0.5, -0.5, -0.5), float3(-1,  0,  0), float2(0, 1) },
    { float3(-0.5,  0.5,  0.5), float3(-1,  0,  0), float2(1, 0) },
    { float3(-0.5,  0.5, -0.5), float3(-1,  0,  0), float2(0, 0) },

    // Right Face
    { float3(0.5, -0.5,  0.5),  float3(1,  0,  0), float2(0, 1) },
    { float3(0.5, -0.5, -0.5),  float3(1,  0,  0), float2(1, 1) },
    { float3(0.5,  0.5, -0.5),  float3(1,  0,  0), float2(1, 0) },
    { float3(0.5, -0.5,  0.5),  float3(1,  0,  0), float2(0, 1) },
    { float3(0.5,  0.5, -0.5),  float3(1,  0,  0), float2(1, 0) },
    { float3(0.5,  0.5,  0.5),  float3(1,  0,  0), float2(0, 0) },

    // Top Face
    { float3(-0.5,  0.5,  0.5), float3(0,  1,  0), float2(0, 1) },
    { float3(0.5,  0.5,  0.5),  float3(0,  1,  0), float2(1, 1) },
    { float3(0.5,  0.5, -0.5),  float3(0,  1,  0), float2(1, 0) },
    { float3(-0.5,  0.5,  0.5), float3(0,  1,  0), float2(0, 1) },
    { float3(0.5,  0.5, -0.5),  float3(0,  1,  0), float2(1, 0) },
    { float3(-0.5,  0.5, -0.5), float3(0,  1,  0), float2(0, 0) },

    // Bottom Face
    { float3(-0.5, -0.5, -0.5), float3(0, -1,  0), float2(0, 1) },
    { float3(0.5, -0.5, -0.5),  float3(0, -1,  0), float2(1, 1) },
    { float3(0.5, -0.5,  0.5),  float3(0, -1,  0), float2(1, 0) },
    { float3(-0.5, -0.5, -0.5), float3(0, -1,  0), float2(0, 1) },
    { float3(0.5, -0.5,  0.5),  float3(0, -1,  0), float2(1, 0) },
    { float3(-0.5, -0.5,  0.5), float3(0, -1,  0), float2(0, 0) }
};

VertexOutput main_vs(
    in float3 position : POSITION,
    in float4 rotation : ROTATION,
    in float3 scale : SCALE,
    in float4 color : COLOR,
    uint vertexID : SV_VertexID
)
{
    float4x4 transform = ConstructTransformMatrix(position, rotation, scale);

    VertexOutput output;
    output.position = mul(mul(viewParms.viewProjMatrix, transform), float4(s_BoxVertices[vertexID].position, 1.0f));
    output.color = color;

    return output;
}

void main_ps(
    in VertexOutput input,
    out float4 color : SV_Target0
)
{
    color = input.color;
}
