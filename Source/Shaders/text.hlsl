#include "tiny2D.h"

//#pragma pack_matrix(row_major)

struct ViewParms
{
	float4x4 viewProjMatrix;
	float2 viewSize;
};

ConstantBuffer<ViewParms> viewParms : register(b0);

VK_BINDING(0, 1) Texture2D t_BindlessTextures[] : register(t0, space1);

struct Vertex
{
	float3 position;
};

static const Vertex s_QuadVertices[6] = {
	{ float3(-0.5f, -0.5f, 0.0f) },
	{ float3(0.5f, -0.5f, 0.0f) },
	{ float3(0.5f,  0.5f, 0.0f) },
	{ float3(-0.5f,  0.5f, 0.0f) },
	{ float3(-0.5f, -0.5f, 0.0f) },
	{ float3(0.5f,  0.5f, 0.0f) }
};

struct VertexOutput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
	float2 uv : UV;
	uint   textureID : TEXTUREID;
};

VertexOutput main_vs(
	in float3 position : POSITION,
	in float4 rotation : ROTATION,
	in float3 scale : SCALE,
	in float4 uv : UV,
	in float4 color : COLOR,
	in uint   textureID : TEXTUREID,
	uint   vertexID : SV_VertexID
)
{
	float4x4 transform = ConstructTransformMatrix(position, rotation, scale);

	VertexOutput output;

	output.position = mul(mul(viewParms.viewProjMatrix, transform), float4(s_QuadVertices[vertexID].position,1.0));
	output.color = color;
	output.textureID = textureID;

	switch (vertexID)					   //   0, 4         1
	{									   //	uv.xy        uv.zy
	case 0: output.uv = uv.xy; break;	   //		x-------x
	case 1: output.uv = uv.zy; break;	   //		|       |
	case 2: output.uv = uv.zw; break;	   //		|       |
	case 3: output.uv = uv.xw; break;	   //		x-------x
	case 4: output.uv = uv.xy; break;	   //	uv.xw		uv.zw
	case 5: output.uv = uv.zw; break;	   //	3   		2, 5
	}									   

	return output;
}

SamplerState s_Sampler : register(s0);

float screenPxRange(float2 uv)
{
	const float pxRange = 2.0;
	float2 unitRange = float2(pxRange, pxRange) / float2(512, 512);
	float2 screenTexSize = 1.0 / fwidth(uv);
	return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

float median(float r, float g, float b) 
{
	return max(min(r, g), min(max(r, g), b));
}

void main_ps(
	in VertexOutput input,
	out float4 color : SV_Target0
)
{
	Texture2D fontAtlas = t_BindlessTextures[input.textureID];

	float4 texColor = input.color * fontAtlas.Sample(s_Sampler, input.uv);
	float3 msd = fontAtlas.Sample(s_Sampler, input.uv).rgb;
	float sd = median(msd.r, msd.g, msd.b);
	float screenPxDistance = screenPxRange(input.uv) * (sd - 0.5);
	float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
	float4 bgColor = float4(0.0, 0.0, 0.0, 0.0);
	color = lerp(bgColor, input.color, opacity);
}
