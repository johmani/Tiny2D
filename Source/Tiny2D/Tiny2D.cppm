//	TODO :
//		- multi view support

export module Tiny2D;
import Math;
import nvrhi;
import std;

#if defined(TINY2D_BUILD_SHAREDLIB)
#   if defined(_MSC_VER)
#       define TINY2D_API __declspec(dllexport)
#   elif defined(__GNUC__)
#       define HYDRA_API __attribute__((visibility("default")))
#   else
#       define TINY2D_API
#       pragma warning "Unknown dynamic link import/export semantics."
#   endif
#else
#   if defined(_MSC_VER)
#       define TINY2D_API __declspec(dllimport)
#   else
#       define TINY2D_API
#   endif
#endif

export namespace Tiny2D {

	using std::uint32_t;
	using std::uint8_t;

	struct Stats
	{
		uint32_t LineCount = 0;
		uint32_t quadCount = 0;
		uint32_t boxCount = 0;
	};

	struct LineDesc
	{
		Math::float3 from = { 0.0f,0.0f ,0.0f };
		Math::float3 to = { 0.0f,0.0f ,0.0f };
		Math::float4 fromColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		Math::float4 toColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		float thickness = 1.0f;
	};

	struct QuadDesc
	{
		Math::float3 position = { 0.0f,0.0f ,0.0f };
		Math::quat rotation = { 1.0f, 0.0f,0.0f ,0.0f };
		Math::float3 scale    = { 1.0f, 1.0f, 1.0f };
		Math::float2 minUV = { 0.0f,0.0f };
		Math::float2 maxUV = { 1.0f,1.0f };
		Math::float4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		nvrhi::ITexture* texture = nullptr;
	};
		
	struct CircleDesc
	{
		Math::float3 position = { 0.0f, 0.0f, 0.0f };
		Math::quat rotation = { 1.0f, 0.0f, 0.0f ,0.0f };
		float radius = 1.0f;
		Math::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float thickness = 0.02f;
		float smoothness = 0.005f;
	};

	struct WireBoxDesc
	{
		Math::float3 position = { 0.0f,0.0f ,0.0f };
		Math::quat rotation = { 1.0f, 0.0f,0.0f ,0.0f };
		Math::float3 scale = { 1.0f, 1.0f, 1.0f };
		Math::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float thickness = 1.0f;
	};

	struct AABBDesc
	{
		Math::float3 min = { -1.0f, -1.0f, -1.0f };
		Math::float3 max = { 1.0f, 1.0f, 1.0f };
		Math::float4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float thickness = 1.0f;
	};

	struct BoxDesc
	{
		Math::float3 position = { 0.0f,0.0f ,0.0f };
		Math::quat rotation = { 1.0f, 0.0f,0.0f ,0.0f };
		Math::float3 scale = { 1.0f, 1.0f, 1.0f };
		Math::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	struct TextDesc
	{
		std::string_view text;
		Math::float3 position = { 0.0f,0.0f ,0.0f };
		Math::quat rotation = { 1.0f, 0.0f,0.0f ,0.0f };
		Math::float3 scale = { 1.0f, 1.0f, 1.0f };
		Math::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		float kerningOffset = 0.0f;
	};

	struct RenderTargets
	{
		nvrhi::TextureHandle resolvedColor;
		nvrhi::TextureHandle color;
		nvrhi::TextureHandle depth;
	};

	struct ViewDesc
	{
		Math::float4x4 viewProj;
		Math::int2 viewSize = { 1920, 1080 };
		nvrhi::Format renderTargetColorFormat = nvrhi::Format::RGBA8_UNORM;
		uint8_t sampleCount = 4;
	};

	TINY2D_API void Init(nvrhi::IDevice* device);
	TINY2D_API void Shutdown();

	TINY2D_API void BeginScene(nvrhi::CommandListHandle commandList, const ViewDesc& desc);
	TINY2D_API void EndScene();
	TINY2D_API const RenderTargets& GetRenderTargets();
	TINY2D_API const Stats& GetStats();

	TINY2D_API void DrawLine(const LineDesc& desc);
	TINY2D_API void DrawLineList(std::span<Math::float3> view, const Math::float3& color = { 1.0f, 1.0f, 1.0f }, float thickness = 1.0f);
	TINY2D_API void DrawLineList(Math::float3* points, uint32_t size, const Math::float3& color = { 1.0f, 1.0f, 1.0f }, float thickness = 1.0f);
	TINY2D_API void DrawLineStrip(std::span<Math::float3> view, const Math::float3& color = { 1.0f, 1.0f, 1.0f }, float thickness = 1.0f);
	TINY2D_API void DrawLineStrip(Math::float3* points, uint32_t size, const Math::float3& color = { 1.0f, 1.0f, 1.0f }, float thickness = 1.0f);
	TINY2D_API void DrawCircle(const CircleDesc& desc);
	TINY2D_API void DrawQuad(const QuadDesc& desc);
	TINY2D_API void DrawText(const TextDesc& desc);
	TINY2D_API void DrawWireBox(const WireBoxDesc& desc = WireBoxDesc());
	TINY2D_API void DrawAABB(const AABBDesc& desc);
	TINY2D_API void DrawBox(const BoxDesc& desc);
};
