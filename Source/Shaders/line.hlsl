#pragma pack_matrix(row_major)

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
    float thickness : THICKNESS;
};

struct GeoOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VertexOutput main_vs(
	in float3 position : POSITION,
	in float4 color : COLOR,
	in float thickness : THICKNESS
)
{
    VertexOutput output;

	output.position = mul(float4(position, 1), viewParms.viewProjMatrix);
	output.color = color;
	output.thickness = thickness;

	return output;
}

[maxvertexcount(6)]
void main_gs(
    line VertexOutput input[2],
    inout TriangleStream<GeoOutput> outputStream
)
{
    GeoOutput output[4];

    float4 p0 = input[0].position;
    float4 p1 = input[1].position;

    float2 screenP0 = (p0.xy / p0.w) * 0.5 + 0.5;
    screenP0 *= viewParms.viewSize.xy;

    float2 screenP1 = (p1.xy / p1.w) * 0.5 + 0.5;
    screenP1 *= viewParms.viewSize.xy;

    float2 screenDir = screenP1 - screenP0;
    float2 perpDir = normalize(float2(-screenDir.y, screenDir.x));

    float2 pixelToClip = float2(2.0 / viewParms.viewSize.x, 2.0 / viewParms.viewSize.y);
    float2 offset = perpDir * input[0].thickness * pixelToClip;

    output[0].position = float4(p0.xy + offset * p0.w, p0.zw);
    output[1].position = float4(p0.xy - offset * p0.w, p0.zw);
    output[2].position = float4(p1.xy + offset * p1.w, p1.zw);
    output[3].position = float4(p1.xy - offset * p1.w, p1.zw);

    output[0].color = input[0].color;
    output[1].color = input[0].color;
    output[2].color = input[1].color;
    output[3].color = input[1].color;

    outputStream.Append(output[0]);
    outputStream.Append(output[1]);
    outputStream.Append(output[2]);

    outputStream.Append(output[2]);
    outputStream.Append(output[1]);
    outputStream.Append(output[3]);

    outputStream.RestartStrip();
}

void main_ps(
    in GeoOutput input,
    out float4 color : SV_Target0
)
{
    color = input.color;
}
