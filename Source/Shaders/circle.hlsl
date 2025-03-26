//#pragma pack_matrix(row_major)

#include "tiny2D.h"

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
	float2 uv : TEXCOORD0;
	float  thickness : THICKNESS;
};

struct Vertex
{
	float3 position;
	float2 uv;
};

static const Vertex s_QuadVertices[6] = {
	{ float3(-0.5f, -0.5f, 0.0f) , float2(0.0f, 0.0f) },
	{ float3(0.5f, -0.5f, 0.0f) , float2(1.0f, 0.0f) },
	{ float3(0.5f,  0.5f, 0.0f) , float2(1.0f, 1.0f) },
	{ float3(-0.5f,  0.5f, 0.0f) , float2(0.0f, 1.0f) },
	{ float3(-0.5f, -0.5f, 0.0f) , float2(0.0f, 0.0f) },
	{ float3(0.5f,  0.5f, 0.0f) , float2(1.0f, 1.0f) }
};


VertexOutput main_vs(
	in float3 position : POSITION,
	in float  radius : RADIUS,
	in float4 rotation : ROTATION,
	in float4 color : COLOR,
	in float  thickness : THICKNESS,
	uint vertexID : SV_VertexID
)
{
	float4x4 transform = ConstructTransformMatrix(position, rotation, radius);
	
	VertexOutput output;
	output.position = mul(mul(viewParms.viewProjMatrix, transform), float4(s_QuadVertices[vertexID].position,1.0));
	output.color = color;
	output.uv = s_QuadVertices[vertexID].uv;
	output.thickness = thickness;

	return output;
}

void main_ps(
	in VertexOutput input,
	out float4 color : SV_Target0
)
{
	float2 uv = input.uv * 2.0f - 1.0f;
	float radius = length(uv);
	float signedDistance = radius - (1.0f - input.thickness);
	signedDistance = abs(signedDistance) - input.thickness;
	float2 gradient = float2(ddx(signedDistance), ddy(signedDistance));
	float alfa = 1 - (signedDistance / length(gradient));

	color = float4(input.color.rgb, alfa);
}
