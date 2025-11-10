
#undef NVRHI_HAS_D3D11
#include <Core/Core.h>
#undef INFINITE
#include "msdf-atlas-gen.h"

#include "Embeded/fonts/OpenSans-Regular.h"

#if NVRHI_HAS_D3D12

#include "Embeded/dxil/line_main_vs.bin.h"
#include "Embeded/dxil/line_main_ps.bin.h"
#include "Embeded/dxil/line_main_gs.bin.h"

#include "Embeded/dxil/sprite_main_vs.bin.h"
#include "Embeded/dxil/sprite_main_ps.bin.h"

#include "Embeded/dxil/circle_main_vs.bin.h"
#include "Embeded/dxil/circle_main_ps.bin.h"

#include "Embeded/dxil/text_main_ps.bin.h"
#include "Embeded/dxil/text_main_vs.bin.h"

#include "Embeded/dxil/box_main_ps.bin.h"
#include "Embeded/dxil/box_main_vs.bin.h"

#endif

#if NVRHI_HAS_VULKAN

#include "Embeded/spirv/line_main_vs.bin.h"
#include "Embeded/spirv/line_main_ps.bin.h"
#include "Embeded/spirv/line_main_gs.bin.h"

#include "Embeded/spirv/sprite_main_vs.bin.h"
#include "Embeded/spirv/sprite_main_ps.bin.h"

#include "Embeded/spirv/circle_main_vs.bin.h"
#include "Embeded/spirv/circle_main_ps.bin.h"

#include "Embeded/spirv/text_main_ps.bin.h"
#include "Embeded/spirv/text_main_vs.bin.h"

#include "Embeded/spirv/box_main_ps.bin.h"
#include "Embeded/spirv/box_main_vs.bin.h"

#endif

#include "Tiny2D/Tiny2D.h"

#ifdef offsetof
#undef offsetof
#endif
#define offsetof(s,m) ((::size_t)&reinterpret_cast<char const volatile&>((((s*)0)->m)))

#define RENDERING_COLOR 0x0000ffff

using namespace Core;

//////////////////////////////////////////////////////////////////////////
// Descriptor Table Manager
//////////////////////////////////////////////////////////////////////////

typedef int DescriptorIndex;

struct DescriptorTableManager
{
	nvrhi::DescriptorTableHandle descriptorTable;

	void Init(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout)
	{
		m_Device = device;

		descriptorTable = m_Device->createDescriptorTable(layout);

		size_t capacity = descriptorTable->getCapacity();
		m_AllocatedDescriptors.resize(capacity);
		m_Descriptors.resize(capacity);
		std::memset(m_Descriptors.data(), 0, sizeof(nvrhi::BindingSetItem) * capacity);
	}

	~DescriptorTableManager()
	{
		for (auto& descriptor : m_Descriptors)
		{
			if (descriptor.resourceHandle)
			{
				descriptor.resourceHandle->Release();
				descriptor.resourceHandle = nullptr;
			}
		}
	}

	DescriptorIndex CreateDescriptor(nvrhi::BindingSetItem item)
	{
		const auto& found = m_DescriptorIndexMap.find(item);
		if (found != m_DescriptorIndexMap.end())
			return found->second;

		uint32_t capacity = descriptorTable->getCapacity();
		bool foundFreeSlot = false;
		uint32_t index = 0;
		for (index = m_SearchStart; index < capacity; index++)
		{
			if (!m_AllocatedDescriptors[index])
			{
				foundFreeSlot = true;
				break;
			}
		}

		if (!foundFreeSlot)
		{
			uint32_t newCapacity = Math::max(64u, capacity * 2); // handle the initial case when capacity == 0
			m_Device->resizeDescriptorTable(descriptorTable, newCapacity);
			m_AllocatedDescriptors.resize(newCapacity);
			m_Descriptors.resize(newCapacity);

			// zero-fill the new descriptors
			std::memset(&m_Descriptors[capacity], 0, sizeof(nvrhi::BindingSetItem) * (newCapacity - capacity));

			index = capacity;
			capacity = newCapacity;
		}

		item.slot = index;
		m_SearchStart = index + 1;
		m_AllocatedDescriptors[index] = true;
		m_Descriptors[index] = item;
		m_DescriptorIndexMap[item] = index;
		m_Device->writeDescriptorTable(descriptorTable, item);

		if (item.resourceHandle)
			item.resourceHandle->AddRef();

		return index;
	}

	void ReleaseDescriptor(DescriptorIndex index)
	{
		nvrhi::BindingSetItem& descriptor = m_Descriptors[index];

		if (descriptor.resourceHandle)
			descriptor.resourceHandle->Release();

		// Erase the existing descriptor from the index map to prevent its "reuse" later
		const auto indexMapEntry = m_DescriptorIndexMap.find(m_Descriptors[index]);
		if (indexMapEntry != m_DescriptorIndexMap.end())
			m_DescriptorIndexMap.erase(indexMapEntry);

		descriptor = nvrhi::BindingSetItem::None(index);

		m_Device->writeDescriptorTable(descriptorTable, descriptor);

		m_AllocatedDescriptors[index] = false;
		m_SearchStart = Math::min(m_SearchStart, index);
	}

private:
	struct BindingSetItemHasher
	{
		size_t operator()(const nvrhi::BindingSetItem& item) const
		{
			size_t hash = 0;
			nvrhi::hash_combine(hash, item.resourceHandle);
			nvrhi::hash_combine(hash, item.type);
			nvrhi::hash_combine(hash, item.format);
			nvrhi::hash_combine(hash, item.dimension);
			nvrhi::hash_combine(hash, item.rawData[0]);
			nvrhi::hash_combine(hash, item.rawData[1]);
			return hash;
		}
	};

	struct BindingSetItemsEqual
	{
		bool operator()(const nvrhi::BindingSetItem& a, const nvrhi::BindingSetItem& b) const
		{
			return a.resourceHandle == b.resourceHandle
				&& a.type == b.type
				&& a.format == b.format
				&& a.dimension == b.dimension
				&& a.subresources == b.subresources;
		}
	};

private:
	nvrhi::IDevice* m_Device;
	std::vector<nvrhi::BindingSetItem> m_Descriptors;
	std::unordered_map<nvrhi::BindingSetItem, DescriptorIndex, BindingSetItemHasher, BindingSetItemsEqual> m_DescriptorIndexMap;
	std::vector<bool> m_AllocatedDescriptors;
	int m_SearchStart = 0;

};

//////////////////////////////////////////////////////////////////////////
// Framebuffer
//////////////////////////////////////////////////////////////////////////

struct Framebuffer
{
	nvrhi::FramebufferHandle framebufferHandle;
	nvrhi::TextureHandle color;
	nvrhi::TextureHandle depth;
	nvrhi::TextureHandle resolvedColor;
	nvrhi::TextureHandle entitiesID;
	
	operator bool() const { return  framebufferHandle; }
	operator nvrhi::IFramebuffer*() const { return framebufferHandle.Get(); }

	void Reset()
	{
		color.Reset();
		resolvedColor.Reset();
		depth.Reset();
		entitiesID.Reset();
		framebufferHandle.Reset();
	}

	void Init(nvrhi::IDevice* device, Math::int2 size, nvrhi::Format colorFormat, uint32_t sampleCount)
	{
		Reset();

		nvrhi::TextureDesc desc;

		{
			desc.width = size.x;
			desc.height = size.y;
			desc.initialState = nvrhi::ResourceStates::RenderTarget;
			desc.isRenderTarget = true;
			desc.useClearValue = true;
			desc.clearValue = nvrhi::Color(0.f);
			desc.sampleCount = sampleCount;
			desc.dimension = sampleCount > 1 ? nvrhi::TextureDimension::Texture2DMS : nvrhi::TextureDimension::Texture2D;
			desc.keepInitialState = true;
			desc.isTypeless = false;
			desc.mipLevels = 1;
			desc.isUAV = sampleCount == 1;
			desc.format = colorFormat;
			desc.debugName = "color";
			color = device->createTexture(desc);
			CORE_VERIFY(color);
		}

		{
			desc.format = nvrhi::Format::R32_UINT;
			desc.debugName = "entitiesID";
			entitiesID = device->createTexture(desc);
			CORE_VERIFY(entitiesID);
		}

		{
			const nvrhi::FormatSupport depthFeatures =
				nvrhi::FormatSupport::Texture |
				nvrhi::FormatSupport::DepthStencil |
				nvrhi::FormatSupport::ShaderLoad;

			const nvrhi::Format depthFormats[] = {
				nvrhi::Format::D24S8,
				nvrhi::Format::D32S8,
				nvrhi::Format::D32,
				nvrhi::Format::D16
			};

			desc.format = nvrhi::utils::ChooseFormat(device, depthFeatures, depthFormats, std::size(depthFormats));
			desc.isTypeless = true;
			desc.initialState = nvrhi::ResourceStates::DepthWrite;
			desc.clearValue = nvrhi::Color(1.0f);
			desc.debugName = "Depth";
			depth = device->createTexture(desc);
			CORE_VERIFY(depth);
		}

		if(desc.sampleCount > 1)
		{
			desc.sampleCount = 1;
			desc.initialState = nvrhi::ResourceStates::RenderTarget;
			desc.dimension = nvrhi::TextureDimension::Texture2D;
			desc.format = colorFormat;
			desc.isUAV = true;
			desc.isTypeless = false;
			desc.debugName = "ResolvedColor";
			resolvedColor = device->createTexture(desc);
			CORE_VERIFY(resolvedColor);
		}
		else
		{
			resolvedColor = color;
		}

		{
			nvrhi::FramebufferDesc fbDesc;
			fbDesc.addColorAttachment(color);
			fbDesc.addColorAttachment(entitiesID);
			fbDesc.setDepthAttachment(depth);
			framebufferHandle = device->createFramebuffer(fbDesc);
			CORE_VERIFY(framebufferHandle);
		}
	}

	void Clear(nvrhi::ICommandList* commandList)
	{
		const nvrhi::FormatInfo& depthFormatInfo = nvrhi::getFormatInfo(depth->getDesc().format);

		commandList->clearDepthStencilTexture(depth, nvrhi::AllSubresources, true, 1.0f, depthFormatInfo.hasStencil, 0);
		commandList->clearTextureFloat(color, nvrhi::AllSubresources, nvrhi::Color(0.f));
		commandList->clearTextureUInt(entitiesID, nvrhi::AllSubresources, ~0u);
	}
};

//////////////////////////////////////////////////////////////////////////
//  Font
//////////////////////////////////////////////////////////////////////////

struct Font
{
	std::vector<msdf_atlas::GlyphGeometry> glyphs;
	msdf_atlas::FontGeometry fontGeometry;
	nvrhi::TextureHandle atlasTexture;
};

Ref<Font> CreateFont(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, msdfgen::FontHandle* fontHandle)
{
	Ref<Font> font = CreateRef<Font>();

	struct CharsetRange { uint32_t Begin, End; };
	const CharsetRange charsetRanges[] = { { 0x0020, 0x00FF } };

	msdf_atlas::Charset charset;
	for (const CharsetRange& range : charsetRanges)
	{
		for (uint32_t c = range.Begin; c <= range.End; c++)
			charset.add(c);
	}

	double fontScale = 1.0;
	font->fontGeometry = msdf_atlas::FontGeometry(&font->glyphs);
	int glyphsLoaded = font->fontGeometry.loadCharset(fontHandle, fontScale, charset);

	double emSize = 40.0;
	msdf_atlas::TightAtlasPacker atlasPacker;
	atlasPacker.setPixelRange(2.0);
	atlasPacker.setMiterLimit(1.0);
	atlasPacker.setScale(emSize);
	int remaining = atlasPacker.pack(font->glyphs.data(), (int)font->glyphs.size());
	CORE_ASSERT(remaining == 0);

	int width, height;
	atlasPacker.getDimensions(width, height);
	emSize = atlasPacker.getScale();

	const double defaultAngleThreshold = 3.0;
	const uint64_t LCGMultiplier = 6364136223846793005ull;
	const uint64_t LCGIncrement = 1442695040888963407ull;
	const unsigned int THREAD_COUNT = std::thread::hardware_concurrency();

	uint64_t coloringSeed = 0;
	unsigned long long glyphSeed = coloringSeed;
	for (msdf_atlas::GlyphGeometry& glyph : font->glyphs)
	{
		glyphSeed *= LCGMultiplier;
		glyph.edgeColoring(msdfgen::edgeColoringInkTrap, defaultAngleThreshold, glyphSeed);
	}

	msdf_atlas::GeneratorAttributes attributes;
	attributes.config.overlapSupport = true;
	attributes.scanlinePass = true;

	msdf_atlas::ImmediateAtlasGenerator<float, 3, msdf_atlas::msdfGenerator, msdf_atlas::BitmapAtlasStorage<uint8_t, 3>> generator(width, height);
	generator.setAttributes(attributes);
	generator.setThreadCount(std::thread::hardware_concurrency());
	generator.generate(font->glyphs.data(), (int)font->glyphs.size());

	msdfgen::BitmapConstRef<uint8_t, 3> bitmap = (msdfgen::BitmapConstRef<uint8_t, 3>)generator.atlasStorage();
	std::vector<uint8_t> rgbaData(bitmap.width * bitmap.height * 4);

	for (size_t i = 0; i < bitmap.width * bitmap.height; ++i)
	{
		rgbaData[i * 4 + 0] = bitmap.pixels[i * 3 + 0];
		rgbaData[i * 4 + 1] = bitmap.pixels[i * 3 + 1];
		rgbaData[i * 4 + 2] = bitmap.pixels[i * 3 + 2];
		rgbaData[i * 4 + 3] = 255;
	}

	nvrhi::TextureDesc desc;
	desc.width = bitmap.width;
	desc.height = bitmap.height;
	desc.format = nvrhi::Format::RGBA8_UNORM;
	desc.debugName = "memory";
	nvrhi::TextureHandle texture = device->createTexture(desc);

	commandList->beginTrackingTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
	commandList->writeTexture(texture, 0, 0, (void*)rgbaData.data(), desc.width * 4);
	commandList->setPermanentTextureState(texture, nvrhi::ResourceStates::ShaderResource);
	commandList->commitBarriers();

	font->atlasTexture = texture;

	return font;
}

Ref<Font> LoadFont(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, std::filesystem::path& filePath)
{
	msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
	CORE_ASSERT(ft, "Failed to initialize FreeType");
	msdfgen::FontHandle* fontHandle = msdfgen::loadFont(ft, filePath.string().c_str());
	CORE_ASSERT(fontHandle, "Failed to load font data");

	Ref<Font> font = CreateFont(device, commandList, fontHandle);

	msdfgen::destroyFont(fontHandle);
	msdfgen::deinitializeFreetype(ft);

	return font;
}

Ref<Font> LoadFontFromMemory(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, uint8_t* bytes, int size)
{
	msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
	CORE_ASSERT(ft, "Failed to initialize FreeType");
	msdfgen::FontHandle* font = msdfgen::loadFontData(ft, bytes, size);
	CORE_ASSERT(font, "Failed to load font data");

	Ref<Font> fontAsset = CreateFont(device, commandList, font);

	msdfgen::destroyFont(font);
	msdfgen::deinitializeFreetype(ft);

	return fontAsset;
}

//////////////////////////////////////////////////////////////////////////
// Line Pass
//////////////////////////////////////////////////////////////////////////

struct LinePass
{
	struct LineVertex
	{
		Math::float3 position;
		Math::float4 color;
		float thickness;
	};

	nvrhi::IDevice* device;
	nvrhi::BufferHandle vertexBuffer;
	nvrhi::InputLayoutHandle inputLayout;
	nvrhi::GraphicsPipelineHandle pso;

	LineVertex* vertexBufferBase = nullptr;
	LineVertex* vertexBufferPtr = nullptr;
	uint32_t maxLinesCount = 1024;
	uint32_t vertexCount = 0;

	void Init(nvrhi::IDevice* pDevice, nvrhi::IShader* vertexShader)
	{
		device = pDevice;

		// vertex input
		{
			nvrhi::VertexAttributeDesc attributes[] = {
				{ "POSITION",   nvrhi::Format::RGB32_FLOAT,  1, 0, offsetof(LineVertex, position) , sizeof(LineVertex), false },
				{ "COLOR",      nvrhi::Format::RGBA32_FLOAT, 1, 1, offsetof(LineVertex, color)    , sizeof(LineVertex), false },
				{ "THICKNESS",  nvrhi::Format::R32_FLOAT,    1, 2, offsetof(LineVertex, thickness), sizeof(LineVertex), false },
			};
			inputLayout = device->createInputLayout(attributes, uint32_t(std::size(attributes)), vertexShader);
		}

		nvrhi::BufferDesc vertexBufferDesc;
		vertexBufferDesc.byteSize = sizeof(LineVertex) * maxLinesCount * 2;
		vertexBufferDesc.isVertexBuffer = true;
		vertexBufferDesc.debugName = "Line-VertexBuffer";
		vertexBufferDesc.initialState = nvrhi::ResourceStates::CopyDest;
		vertexBufferDesc.cpuAccess = nvrhi::CpuAccessMode::Write;
		vertexBuffer = device->createBuffer(vertexBufferDesc);
		vertexBufferBase = (LineVertex*)device->mapBuffer(vertexBuffer, nvrhi::CpuAccessMode::Write);
	}

	void ResizeBuffer(uint32_t size = 0)
	{
		auto prevMaxLinesCount = maxLinesCount;

		maxLinesCount = size ? size : maxLinesCount * 2;

		LOG_INFO("resize MaxLinesCount LinePass {} -> {}", prevMaxLinesCount, maxLinesCount);

		nvrhi::BufferDesc vertexBufferDesc;
		vertexBufferDesc.byteSize = sizeof(LineVertex) * maxLinesCount * 2;
		vertexBufferDesc.isVertexBuffer = true;
		vertexBufferDesc.debugName = "Line-VertexBuffer";
		vertexBufferDesc.initialState = nvrhi::ResourceStates::CopyDest;
		vertexBufferDesc.cpuAccess = nvrhi::CpuAccessMode::Write;

		auto newVertexBuffer = device->createBuffer(vertexBufferDesc);
		auto newMappedBuffer = (LineVertex*)device->mapBuffer(newVertexBuffer, nvrhi::CpuAccessMode::Write);

		CORE_ASSERT(newMappedBuffer);

		std::memcpy(newMappedBuffer, vertexBufferBase, vertexCount);
		device->unmapBuffer(vertexBuffer);

		vertexBufferBase = newMappedBuffer;
		vertexBufferPtr = vertexBufferBase + vertexCount;
		vertexBuffer = newVertexBuffer;
	}

	~LinePass()
	{
		device->unmapBuffer(vertexBuffer);
	}

	void Begin()
	{
		vertexBufferPtr = vertexBufferBase;
		vertexCount = 0;
	}

	void End(
		nvrhi::ICommandList* commandList, 
		nvrhi::IBindingLayout* viewBindingLayout, 
		nvrhi::IBindingSet* viewBindingSets,
		nvrhi::IFramebuffer* framebuffer,
		nvrhi::IShader* vs,
		nvrhi::IShader* ps,
		nvrhi::IShader* gs
	)
	{
		CORE_PROFILE_SCOPE_NC("Tiny2D::LinesPass::End", RENDERING_COLOR);

		CORE_ASSERT(commandList);
		CORE_ASSERT(framebuffer);

		Timer t;

		CORE_VERIFY(vertexCount <= maxLinesCount * 2);

		if (vertexCount <= 0)
			return;

		if (!pso)
		{
			nvrhi::GraphicsPipelineDesc psoDesc;
			psoDesc.VS = vs;
			psoDesc.PS = ps;
			psoDesc.GS = gs;
			psoDesc.inputLayout = inputLayout;
			psoDesc.bindingLayouts = { viewBindingLayout };
			psoDesc.primType = nvrhi::PrimitiveType::LineList;
			psoDesc.renderState = {
				.blendState = {
					.alphaToCoverageEnable = true,
				},
				.depthStencilState = {
					.depthTestEnable = true
				},
				.rasterState = {
					.cullMode = nvrhi::RasterCullMode::None,
					.frontCounterClockwise = true,
					.multisampleEnable = true,
					.antialiasedLineEnable = true,
				},
			};

			pso = device->createGraphicsPipeline(psoDesc, framebuffer);
			CORE_ASSERT(pso);
		}

		nvrhi::GraphicsState state;
		state.pipeline = pso;
		state.framebuffer = framebuffer;
		state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());

		commandList->beginMarker("Lines");
		{
			state.bindings = { viewBindingSets };
			state.vertexBuffers = {
				{ vertexBuffer, 0, 0 },
				{ vertexBuffer, 1, 0 },
				{ vertexBuffer, 2, 0 },
			};
			commandList->setGraphicsState(state);

			commandList->draw({ .vertexCount = uint32_t(vertexCount) });
		}
		commandList->endMarker();
	}
};

//////////////////////////////////////////////////////////////////////////
//  Mesh instancing
//////////////////////////////////////////////////////////////////////////

struct spriteAttributes
{
	Math::float3 position;
	Math::quat rotation;
	Math::float3 scale;
	glm::vec4 uv;
	Math::float4 color;
	uint32_t textureID;
	uint32_t id;

	static std::span<nvrhi::VertexAttributeDesc> GetVertexAttributeDesc()
	{
		static nvrhi::VertexAttributeDesc attributes[] = {
			{ "POSITION",  nvrhi::Format::RGB32_FLOAT,	1, 0, offsetof(spriteAttributes, position),	 sizeof(spriteAttributes), true },
			{ "ROTATION",  nvrhi::Format::RGBA32_FLOAT,	1, 1, offsetof(spriteAttributes, rotation),	 sizeof(spriteAttributes), true },
			{ "SCALE",	   nvrhi::Format::RGB32_FLOAT,	1, 2, offsetof(spriteAttributes, scale),	 sizeof(spriteAttributes), true },
			{ "UV",		   nvrhi::Format::RGBA32_FLOAT,	1, 3, offsetof(spriteAttributes, uv),		 sizeof(spriteAttributes), true },
			{ "COLOR",     nvrhi::Format::RGBA32_FLOAT,	1, 4, offsetof(spriteAttributes, color),	 sizeof(spriteAttributes), true },
			{ "TEXTUREID", nvrhi::Format::R32_SINT,		1, 5, offsetof(spriteAttributes, textureID), sizeof(spriteAttributes), true },
			{ "ENTITYID",  nvrhi::Format::R32_SINT,		1, 6, offsetof(spriteAttributes, id)       , sizeof(spriteAttributes), true },
		};

		return attributes;
	}

	static nvrhi::static_vector<nvrhi::VertexBufferBinding, nvrhi::c_MaxVertexAttributes> GetVertexBuffers(nvrhi::BufferHandle instanceBuffer)
	{
		return {
			{ instanceBuffer, 0, 0 },
			{ instanceBuffer, 1, 0 },
			{ instanceBuffer, 2, 0 },
			{ instanceBuffer, 3, 0 },
			{ instanceBuffer, 4, 0 },
			{ instanceBuffer, 5, 0 },
			{ instanceBuffer, 6, 0 }
		};
	}
};

struct CircleAttributes
{
	Math::float3 position;
	float radius;
	Math::quat rotation;
	Math::float4 color;
	float thickness;

	static std::span<nvrhi::VertexAttributeDesc> GetVertexAttributeDesc()
	{
		static nvrhi::VertexAttributeDesc attributes[] = {
			{ "POSITION",	 nvrhi::Format::RGB32_FLOAT,   1, 0, offsetof(CircleAttributes, position),	sizeof(CircleAttributes), true },
			{ "RADIUS",		 nvrhi::Format::R32_FLOAT,     1, 1, offsetof(CircleAttributes, radius),	sizeof(CircleAttributes), true },
			{ "ROTATION",	 nvrhi::Format::RGBA32_FLOAT,  1, 2, offsetof(CircleAttributes, rotation),	sizeof(CircleAttributes), true },
			{ "COLOR",       nvrhi::Format::RGBA32_FLOAT,  1, 3, offsetof(CircleAttributes, color),		sizeof(CircleAttributes), true },
			{ "THICKNESS",   nvrhi::Format::R32_FLOAT,     1, 4, offsetof(CircleAttributes, thickness), sizeof(CircleAttributes), true },
		};


		return attributes;
	}

	static nvrhi::static_vector<nvrhi::VertexBufferBinding, nvrhi::c_MaxVertexAttributes> GetVertexBuffers(nvrhi::BufferHandle instanceBuffer)
	{
		return {
			{ instanceBuffer, 0, 0 },
			{ instanceBuffer, 1, 0 },
			{ instanceBuffer, 2, 0 },
			{ instanceBuffer, 3, 0 },
			{ instanceBuffer, 4, 0 },
		};
	}
};

struct TextAttributes
{
	Math::float3 position;
	Math::quat rotation;
	Math::float3 scale;
	glm::vec4 uv;
	glm::vec4 color;
	uint32_t textureID;

	static std::span<nvrhi::VertexAttributeDesc> GetVertexAttributeDesc()
	{
		static nvrhi::VertexAttributeDesc attributes[] = {
			{ "POSITION",  nvrhi::Format::RGB32_FLOAT,   1, 0, offsetof(TextAttributes, position),  sizeof(TextAttributes), true },
			{ "ROTATION",  nvrhi::Format::RGBA32_FLOAT,	 1, 1, offsetof(TextAttributes, rotation),  sizeof(TextAttributes), true },
			{ "SCALE",	   nvrhi::Format::RGB32_FLOAT,	 1, 2, offsetof(TextAttributes, scale),	    sizeof(TextAttributes), true },
			{ "UV",		   nvrhi::Format::RGBA32_FLOAT,  1, 3, offsetof(TextAttributes, uv),		sizeof(TextAttributes), true },
			{ "COLOR",     nvrhi::Format::RGBA32_FLOAT,  1, 4, offsetof(TextAttributes, color),	    sizeof(TextAttributes), true },
			{ "TEXTUREID", nvrhi::Format::R32_UINT,      1, 5, offsetof(TextAttributes, textureID), sizeof(TextAttributes), true },
		};

		return attributes;
	}

	static nvrhi::static_vector<nvrhi::VertexBufferBinding, nvrhi::c_MaxVertexAttributes> GetVertexBuffers(nvrhi::BufferHandle instanceBuffer)
	{
		return {
			{ instanceBuffer, 0, 0 },
			{ instanceBuffer, 1, 0 },
			{ instanceBuffer, 2, 0 },
			{ instanceBuffer, 3, 0 },
			{ instanceBuffer, 4, 0 },
			{ instanceBuffer, 5, 0 },
		};
	}
};

struct BoxAttributes
{
	Math::float3 position;
	Math::quat rotation;
	Math::float3 scale;
	Math::float4 color;

	static std::span<nvrhi::VertexAttributeDesc> GetVertexAttributeDesc()
	{
		static nvrhi::VertexAttributeDesc attributes[] = {
			{ "POSITION", nvrhi::Format::RGB32_FLOAT,  1, 0, offsetof(BoxAttributes, position), sizeof(BoxAttributes), true  },
			{ "ROTATION", nvrhi::Format::RGBA32_FLOAT, 1, 1, offsetof(BoxAttributes, rotation), sizeof(BoxAttributes), true  },
			{ "SCALE",	  nvrhi::Format::RGB32_FLOAT,  1, 2, offsetof(BoxAttributes, scale),    sizeof(BoxAttributes), true  },
			{ "COLOR",    nvrhi::Format::RGBA32_FLOAT, 1, 3, offsetof(BoxAttributes, color),    sizeof(BoxAttributes), true  },
		};

		return attributes;
	}

	static nvrhi::static_vector<nvrhi::VertexBufferBinding, nvrhi::c_MaxVertexAttributes> GetVertexBuffers(nvrhi::BufferHandle instanceBuffer)
	{
		return {
			{ instanceBuffer, 0, 0 },
			{ instanceBuffer, 1, 0 },
			{ instanceBuffer, 2, 0 },
			{ instanceBuffer, 3, 0 },
		};
	}
};

template<typename T>
struct InstancedPass
{
	nvrhi::IDevice* device;
	nvrhi::BufferHandle instanceBuffer;
	nvrhi::InputLayoutHandle inputLayout;
	T* instanceDataBase = nullptr;
	T* instanceDataPtr = nullptr;
	uint32_t maxInstanceCount = 5000;
	uint32_t instanceCount = 0;
	nvrhi::GraphicsPipelineHandle pso;

	uint32_t vertexCount = 6;

	void Init(nvrhi::IDevice* pDevice, nvrhi::IShader* vertexShader)
	{
		CORE_PROFILE_SCOPE_NC("Tiny2D::InstancedPass::init", RENDERING_COLOR);

		device = pDevice;

		// vertex input
		{
			auto att = T::GetVertexAttributeDesc();
			inputLayout = device->createInputLayout(att.data(), (uint32_t)att.size(), vertexShader);
		}
		
		// vertex buffer
		{
			nvrhi::BufferDesc instanceBufferDesc;
			instanceBufferDesc.byteSize = sizeof(T) * maxInstanceCount;
			instanceBufferDesc.isVertexBuffer = true;
			instanceBufferDesc.debugName = typeid(T).name();
			instanceBufferDesc.initialState = nvrhi::ResourceStates::CopyDest;
			instanceBufferDesc.cpuAccess = nvrhi::CpuAccessMode::Write;
			instanceBuffer = device->createBuffer(instanceBufferDesc);
		}

		instanceDataBase = (T*)device->mapBuffer(instanceBuffer, nvrhi::CpuAccessMode::Write);
	}

	void ResizeBuffer(uint32_t size = 0)
	{
		auto prevMaxQuadCount = maxInstanceCount;

		maxInstanceCount = size ? size : maxInstanceCount * 2;

		LOG_INFO("resize maxInstanceCount {} {} -> {}", typeid(T).name(), prevMaxQuadCount, maxInstanceCount);

		nvrhi::BufferDesc instanceBufferDesc;
		instanceBufferDesc.byteSize = sizeof(T) * maxInstanceCount;
		instanceBufferDesc.isVertexBuffer = true;
		instanceBufferDesc.debugName = typeid(T).name();
		instanceBufferDesc.initialState = nvrhi::ResourceStates::CopyDest;
		instanceBufferDesc.cpuAccess = nvrhi::CpuAccessMode::Write;

		auto newBuffer = device->createBuffer(instanceBufferDesc);
		CORE_ASSERT(newBuffer);

		auto newMappedBuffer = (T*)device->mapBuffer(newBuffer, nvrhi::CpuAccessMode::Write);
		CORE_ASSERT(newMappedBuffer);

		std::memcpy(newMappedBuffer, instanceDataBase, instanceCount);
		device->unmapBuffer(instanceBuffer);

		instanceDataBase = newMappedBuffer;
		instanceDataPtr = instanceDataBase + instanceCount;
		instanceBuffer = newBuffer;
	}

	~InstancedPass()
	{
		device->unmapBuffer(instanceBuffer);
	}

	void Begin()
	{
		instanceDataPtr = instanceDataBase;
		instanceCount = 0;
	}

	void End(
		nvrhi::ICommandList* commandList,
		nvrhi::BindingLayoutVector bindingLayouts,
		nvrhi::BindingSetVector bindings,
		nvrhi::IFramebuffer* fb,
		nvrhi::IShader* vs,
		nvrhi::IShader* ps,
		nvrhi::IShader* gs = nullptr,
		uint32_t vertexCount = 6,
		nvrhi::PrimitiveType primType = nvrhi::PrimitiveType::TriangleList
	)
	{
		CORE_PROFILE_SCOPE_NC("Tiny2D::InstancedPass::Render", RENDERING_COLOR);

		if (!pso)
		{

			CORE_PROFILE_SCOPE_NC("Tiny2D::InstancedPass::CreatePipeline", RENDERING_COLOR);
			nvrhi::GraphicsPipelineDesc psoDesc;

			psoDesc.VS = vs;
			psoDesc.PS = ps;
			psoDesc.GS = gs ? gs : psoDesc.GS.Get();
			psoDesc.inputLayout = inputLayout;
			psoDesc.bindingLayouts = bindingLayouts;
			psoDesc.primType = primType;
			psoDesc.renderState = {
				.blendState = {
					.alphaToCoverageEnable = true,
				},
				.depthStencilState = {
					.depthTestEnable = true,
				},
				.rasterState = {
					.cullMode = nvrhi::RasterCullMode::None,
					.frontCounterClockwise = true,
					.multisampleEnable = true,
					.antialiasedLineEnable = true,
				},
			};

			for (uint32_t i = 0; i < fb->getDesc().colorAttachments.size(); i++)
			{
				nvrhi::Format format = fb->getDesc().colorAttachments[i].texture->getDesc().format;
				nvrhi::FormatInfo formatInfo = nvrhi::getFormatInfo(format);

				if (formatInfo.hasAlpha)
				{
					psoDesc.renderState.blendState.targets[i] = {
						.blendEnable = true,
						.srcBlend = nvrhi::BlendFactor::SrcAlpha,
						.destBlend = nvrhi::BlendFactor::InvSrcAlpha,
						.blendOp = nvrhi::BlendOp::Add,
						.srcBlendAlpha = nvrhi::BlendFactor::One,
						.destBlendAlpha = nvrhi::BlendFactor::One,
						.blendOpAlpha = nvrhi::BlendOp::Add,
						.colorWriteMask = nvrhi::ColorMask::All,
					};
				}
			}

			pso = device->createGraphicsPipeline(psoDesc, fb);
			CORE_ASSERT(pso);
		}

		// draw
		{
			CORE_PROFILE_SCOPE_NC("Tiny2D::InstancedPass::draw", RENDERING_COLOR);

			if (instanceCount <= 0)
				return;

			nvrhi::GraphicsState state;
			state.pipeline = pso;
			state.framebuffer = fb;
			state.viewport.addViewportAndScissorRect(fb->getFramebufferInfo().getViewport());

			commandList->beginMarker(typeid(T).name());
			{
				state.bindings = bindings;
				state.vertexBuffers = T::GetVertexBuffers(instanceBuffer);
				commandList->setGraphicsState(state);
				commandList->draw({ 
					.vertexCount = vertexCount, 
					.instanceCount = instanceCount 
				});
			}
			commandList->endMarker();
		}
	}
};

//////////////////////////////////////////////////////////////////////////
// Renderer
//////////////////////////////////////////////////////////////////////////

struct ViewBuffer
{
	Math::float4x4 ViewProjMatrix = Math::float4x4(1.0f);
	Math::float2 viewSize;
};

struct ViewData
{
	Framebuffer framebuffer;
	nvrhi::BindingSetHandle bindingSet;
	nvrhi::BufferHandle viewBuffer;

	LinePass line;
	InstancedPass<spriteAttributes> sprite;
	InstancedPass<CircleAttributes> circle;
	InstancedPass<TextAttributes> text;
	InstancedPass<BoxAttributes> box;
	Tiny2D::Stats stats;
};

struct RendererData
{
	nvrhi::IDevice* device;
	nvrhi::ICommandList* commandList;
	
	nvrhi::BindingLayoutHandle bindingLayout;
	nvrhi::BindingLayoutHandle bindlessLayout;
	nvrhi::SamplerHandle sampler;
	DescriptorTableManager descriptorTableManager;
	
	nvrhi::TextureHandle whiteTexture;

	nvrhi::ShaderHandle lineVertexShader;
	nvrhi::ShaderHandle linePixelShader;
	nvrhi::ShaderHandle lineGeoShader;

	nvrhi::ShaderHandle spriteVertexShader;
	nvrhi::ShaderHandle spritePixelShader;

	nvrhi::ShaderHandle circleVertexShader;
	nvrhi::ShaderHandle circlePixelShader;

	nvrhi::ShaderHandle textVertexShader;
	nvrhi::ShaderHandle textPixelShader;

	nvrhi::ShaderHandle boxVertexShader;
	nvrhi::ShaderHandle boxPixelShader;

	Ref<Font> defaultFont;
	ViewData* fd;
};

static RendererData* s_Data = nullptr;

void Tiny2D::Init(nvrhi::IDevice* device)
{
	CORE_PROFILE_SCOPE_NC("Tiny2D::Init", RENDERING_COLOR);

	CORE_VERIFY(device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D11,"[Tiny2D] : D3D11 is not supported");

	s_Data = new RendererData();
	s_Data->device = device;

	{
		nvrhi::ShaderDesc vsDesc;
		vsDesc.shaderType = nvrhi::ShaderType::Vertex;
		vsDesc.entryName = "main_vs";

		nvrhi::ShaderDesc psDesc;
		psDesc.shaderType = nvrhi::ShaderType::Pixel;
		psDesc.entryName = "main_ps";

		nvrhi::ShaderDesc gsDesc;
		gsDesc.entryName = "main_gs";
		gsDesc.shaderType = nvrhi::ShaderType::Geometry;

		{
			vsDesc.debugName = "line_vs";
			psDesc.debugName = "line_ps";
			gsDesc.debugName = "line_gs";
			s_Data->lineVertexShader = RHI::CreateStaticShader(device, STATIC_SHADER(line_main_vs), nullptr, vsDesc);
			s_Data->linePixelShader = RHI::CreateStaticShader(device, STATIC_SHADER(line_main_ps), nullptr, psDesc);
			s_Data->lineGeoShader = RHI::CreateStaticShader(device, STATIC_SHADER(line_main_gs), nullptr, gsDesc);
			CORE_ASSERT(s_Data->lineVertexShader);
			CORE_ASSERT(s_Data->linePixelShader);
			CORE_ASSERT(s_Data->lineGeoShader);
		}

		{
			vsDesc.debugName = "sprite_vs";
			psDesc.debugName = "sprite_ps";
			s_Data->spriteVertexShader = RHI::CreateStaticShader(device, STATIC_SHADER(sprite_main_vs), nullptr, vsDesc);
			s_Data->spritePixelShader = RHI::CreateStaticShader(device, STATIC_SHADER(sprite_main_ps), nullptr, psDesc);
			CORE_ASSERT(s_Data->spriteVertexShader);
			CORE_ASSERT(s_Data->spritePixelShader);
		}

		{
			vsDesc.debugName = "circle_vs";
			psDesc.debugName = "circle_ps";
			s_Data->circleVertexShader = RHI::CreateStaticShader(device, STATIC_SHADER(circle_main_vs), nullptr, vsDesc);
			s_Data->circlePixelShader = RHI::CreateStaticShader(device, STATIC_SHADER(circle_main_ps), nullptr, psDesc);
			CORE_ASSERT(s_Data->circleVertexShader);
			CORE_ASSERT(s_Data->circlePixelShader);
		}

		{
			vsDesc.debugName = "text_vs";
			psDesc.debugName = "text_ps";
			s_Data->textVertexShader = RHI::CreateStaticShader(device, STATIC_SHADER(text_main_vs), nullptr, vsDesc);
			s_Data->textPixelShader = RHI::CreateStaticShader(device, STATIC_SHADER(text_main_ps), nullptr, psDesc);
			CORE_ASSERT(s_Data->textVertexShader);
			CORE_ASSERT(s_Data->textPixelShader);
		}

		{
			vsDesc.debugName = "box_vs";
			psDesc.debugName = "box_ps";
			s_Data->boxVertexShader = RHI::CreateStaticShader(device, STATIC_SHADER(box_main_vs), nullptr, vsDesc);
			s_Data->boxPixelShader = RHI::CreateStaticShader(device, STATIC_SHADER(box_main_ps), nullptr, psDesc);
			CORE_ASSERT(s_Data->boxVertexShader);
			CORE_ASSERT(s_Data->boxPixelShader);
		}
	}

	{
		nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
		bindlessLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
		bindlessLayoutDesc.firstSlot = 0;
		bindlessLayoutDesc.maxCapacity = 1024;
		bindlessLayoutDesc.registerSpaces = {
			nvrhi::BindingLayoutItem::Texture_SRV(1)
		};
		s_Data->bindlessLayout = device->createBindlessLayout(bindlessLayoutDesc);
		s_Data->descriptorTableManager.Init(device, s_Data->bindlessLayout);
	}

	{
		auto desc = nvrhi::SamplerDesc()
			.setAllFilters(false)
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true)
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setMaxAnisotropy(16);
		s_Data->sampler = device->createSampler(desc);
		CORE_VERIFY(s_Data->sampler);
	}

	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::All;
		desc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::Sampler(0)
		};
		s_Data->bindingLayout = s_Data->device->createBindingLayout(desc);
		CORE_VERIFY(s_Data->bindingLayout);
	}

	{
		nvrhi::CommandListHandle cl = device->createCommandList();
		cl->open();

		{
			uint32_t whiteImage = 0xffffffff;
			nvrhi::TextureDesc textureDesc;
			textureDesc.format = nvrhi::Format::RGBA8_UNORM;
			textureDesc.width = 1;
			textureDesc.height = 1;
			textureDesc.mipLevels = 1;

			textureDesc.debugName = "whiteTexture";
			s_Data->whiteTexture = device->createTexture(textureDesc);

			cl->beginTrackingTextureState(s_Data->whiteTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
			cl->writeTexture(s_Data->whiteTexture, 0, 0, &whiteImage, 4);
			cl->setPermanentTextureState(s_Data->whiteTexture, nvrhi::ResourceStates::ShaderResource);
			cl->commitBarriers();

			auto ind = s_Data->descriptorTableManager.CreateDescriptor(nvrhi::BindingSetItem::Texture_SRV(0, s_Data->whiteTexture));
		}

		//auto filePath = Plugins::GetPlugin(Hash(std::string_view("Tiny2D")))->AssetsDirectory() / "Fonts" / "OpenSans-Bold.ttf";
		//s_Data->defaultFont = LoadFont(device, commandList, filePath);
		s_Data->defaultFont = LoadFontFromMemory(device, cl, OpenSans_Regular_ttf, OpenSans_Regular_ttf_len);

		cl->close();
		device->executeCommandList(cl);
	}
}

void Tiny2D::Shutdown()
{
	CORE_PROFILE_SCOPE_NC("Tiny2D::Shutdown", RENDERING_COLOR);

	s_Data->device->waitForIdle();
	delete s_Data;
}

void Tiny2D::BeginScene(Tiny2D::ViewHandle& viewHandle, nvrhi::ICommandList* commandList, const ViewDesc& desc)
{
	CORE_PROFILE_SCOPE_NC("Tiny2D::BeginScene", RENDERING_COLOR);

	s_Data->commandList = commandList;
	
	if(!viewHandle)
	{
		auto viewData = new ViewData();
		viewHandle = Core::Ref<ViewData>(viewData);

		viewData->line.Init(s_Data->device, s_Data->lineVertexShader);
		viewData->sprite.Init(s_Data->device, s_Data->spriteVertexShader);
		viewData->circle.Init(s_Data->device, s_Data->circleVertexShader);
		viewData->text.Init(s_Data->device, s_Data->textVertexShader);
		viewData->box.Init(s_Data->device, s_Data->boxVertexShader);
	}

	ViewData* viewData = (ViewData*)viewHandle.get();

	s_Data->fd = viewData;

	if (!viewData->viewBuffer)
	{
		viewData->viewBuffer = s_Data->device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ViewBuffer), "ViewBuffer", sizeof(ViewBuffer)));
		CORE_VERIFY(viewData->viewBuffer);
	}

	if (!viewData->framebuffer)
	{
		viewData->framebuffer.Init(s_Data->device, { desc.viewSize.x, desc.viewSize.y }, desc.renderTargetColorFormat, desc.sampleCount);
	}

	// resize
	const auto& rt = viewData->framebuffer.color->getDesc();
	if (rt.width != desc.viewSize.x || rt.height != desc.viewSize.y)
	{
		viewData->line.pso.Reset();
		viewData->sprite.pso.Reset();
		viewData->circle.pso.Reset();
		viewData->text.pso.Reset();
		viewData->box.pso.Reset();

		viewData->framebuffer.Init(s_Data->device, { desc.viewSize.x, desc.viewSize.y }, desc.renderTargetColorFormat, desc.sampleCount);
	}

	viewData->framebuffer.Clear(commandList);

	{
		ViewBuffer viewBuffer = {};
		viewBuffer.ViewProjMatrix = desc.viewProj;
		viewBuffer.viewSize = desc.viewSize;
		commandList->writeBuffer(viewData->viewBuffer, &viewBuffer, sizeof(ViewBuffer));
	}

	{
		viewData->stats.quadCount = viewData->sprite.instanceCount + viewData->circle.instanceCount + viewData->text.instanceCount;
		viewData->stats.boxCount = viewData->box.instanceCount;
		viewData->stats.LineCount = viewData->line.vertexCount / 2;

		viewData->line.Begin();
		viewData->sprite.Begin();
		viewData->circle.Begin();
		viewData->text.Begin();
		viewData->box.Begin();
	}
}

void Tiny2D::EndScene()
{
	CORE_PROFILE_SCOPE_NC("Tiny2D::EndScene", RENDERING_COLOR);
	BUILTIN_PROFILE(s_Data->device, s_Data->commandList, "Tiny2D");

	ViewData* viewData = s_Data->fd;

	if(!viewData->bindingSet)
	{
		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, viewData->viewBuffer),
			nvrhi::BindingSetItem::Sampler(0, s_Data->sampler)
		};

		viewData->bindingSet = s_Data->device->createBindingSet(desc, s_Data->bindingLayout);
		CORE_VERIFY(viewData->bindingSet);
	}

	viewData->line.End(
		s_Data->commandList,
		s_Data->bindingLayout,
		viewData->bindingSet,
		viewData->framebuffer,
		s_Data->lineVertexShader,
		s_Data->linePixelShader,
		s_Data->lineGeoShader
	);

	viewData->sprite.End(
		s_Data->commandList,
		{ s_Data->bindingLayout ,s_Data->bindlessLayout },
		{ viewData->bindingSet, s_Data->descriptorTableManager.descriptorTable.Get() },
		viewData->framebuffer,
		s_Data->spriteVertexShader, s_Data->spritePixelShader, nullptr
	);
	
	viewData->circle.End(
		s_Data->commandList,
		{ s_Data->bindingLayout },
		{ viewData->bindingSet },
		viewData->framebuffer,
		s_Data->circleVertexShader, s_Data->circlePixelShader, nullptr
	);
	
	viewData->text.End(
		s_Data->commandList,
		{ s_Data->bindingLayout ,s_Data->bindlessLayout },
		{ viewData->bindingSet, s_Data->descriptorTableManager.descriptorTable.Get() },
		viewData->framebuffer,
		s_Data->textVertexShader, s_Data->textPixelShader, nullptr
	);
	
	viewData->box.End(
		s_Data->commandList,
		{ s_Data->bindingLayout },
		{ viewData->bindingSet },
		viewData->framebuffer,
		s_Data->boxVertexShader, s_Data->boxPixelShader, nullptr,
		36
	);

	if (viewData->framebuffer.color->getDesc().sampleCount > 1)
	{
		auto subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
		s_Data->commandList->resolveTexture(
			viewData->framebuffer.resolvedColor,
			subresources,
			viewData->framebuffer.color,
			subresources
		);
	}

	s_Data->fd = nullptr;
	s_Data->commandList = nullptr;
}

nvrhi::ITexture* Tiny2D::GetColorTarget(ViewHandle viewHandle)
{
	ViewData* viewData = (ViewData*)viewHandle.get();

	return viewData->framebuffer.resolvedColor; 
}

nvrhi::ITexture* Tiny2D::GetDepthTarget(ViewHandle viewHandle)
{
	ViewData* viewData = (ViewData*)viewHandle.get();

	return viewData->framebuffer.depth; 
}


nvrhi::ITexture* Tiny2D::GetEntitiesIDTarget(ViewHandle viewHandle)
{
	ViewData* viewData = (ViewData*)viewHandle.get();

	return viewData->framebuffer.entitiesID;
}

const Tiny2D::Stats& Tiny2D::GetStats(ViewHandle viewHandle)
{
	ViewData* viewData = (ViewData*)viewHandle.get();
	
	return viewData->stats;
}

//////////////////////////////////////////////////////////////////////////
// Draw
//////////////////////////////////////////////////////////////////////////

static Math::float3 Transform(const Math::float3& v, Math::float4x4 wt)
{
	glm::vec4 t = wt * glm::vec4(v.x, v.y, v.z, 1.0f);
	return Math::float3{ t.x, t.y, t.z };
}

Math::float4x4 ConstructTransformMatrix(const Math::vec3& position, const Math::quat& rotation, const Math::vec3& scale)
{
	Math::float3x3 rotationMatrix = Math::float3x3(rotation);

	Math::float3x3 scaleMatrix = Math::float3x3(
		scale.x, 0.0f, 0.0f,
		0.0f, scale.y, 0.0f,
		0.0f, 0.0f, scale.z
	);
	
	Math::float3x3 transform3x3 = rotationMatrix * scaleMatrix;

	Math::float4x4 transformMatrix = Math::float4x4(1.0f);
	transformMatrix[0] = Math::float4(transform3x3[0], 0.0f);
	transformMatrix[1] = Math::float4(transform3x3[1], 0.0f);
	transformMatrix[2] = Math::float4(transform3x3[2], 0.0f);
	transformMatrix[3] = Math::float4(position, 1.0f);

	return transformMatrix;
}

void Tiny2D::DrawLine(const LineDesc& desc)
{
	auto& lins = s_Data->fd->line;

	if (lins.vertexCount >= lins.maxLinesCount * 2)
		lins.ResizeBuffer();

	lins.vertexBufferPtr->position = desc.from;
	lins.vertexBufferPtr->color = desc.fromColor;
	lins.vertexBufferPtr->thickness = desc.thickness;
	lins.vertexBufferPtr++;
		  
	lins.vertexBufferPtr->position = desc.to;
	lins.vertexBufferPtr->color = desc.toColor;
	lins.vertexBufferPtr->thickness = desc.thickness;
	lins.vertexBufferPtr++;

	lins.vertexCount += 2;
}

void Tiny2D::DrawLineList(Math::float3* points, uint32_t size, const Math::float4& color, float thickness)
{
	CORE_ASSERT(points);

	auto& lines = s_Data->fd->line;

	uint32_t requiredSize = lines.vertexCount + size;
	if (requiredSize >= lines.maxLinesCount * 2)
		lines.ResizeBuffer(requiredSize * 2);


	if (size % 2 != 0)
	{
		LOG_CORE_WARN("Line lists require an even number of points, The input size is ({}).", size);
		return;
	}

	for (size_t i = 0; i < size - 1; i += 2)
	{
		lines.vertexBufferPtr->position = points[i];
		lines.vertexBufferPtr->thickness = thickness;
		lines.vertexBufferPtr->color = color;
		lines.vertexBufferPtr++;

		lines.vertexBufferPtr->position = points[i + 1];
		lines.vertexBufferPtr->color = color;
		lines.vertexBufferPtr++;

		lines.vertexCount += 2;
	}
}

void Tiny2D::DrawLineList(std::span<Math::float3> span, const Math::float4& color, float thickness)
{
	DrawLineList(span.data(), (uint32_t)span.size(), color, thickness);
}

void Tiny2D::DrawLineStrip(Math::float3* points, uint32_t size, const Math::float4& color, float thickness)
{
	CORE_ASSERT(points);

	auto& lines = s_Data->fd->line;

	uint32_t requiredSize = lines.vertexCount + size;
	if (requiredSize >= lines.maxLinesCount * 2)
		lines.ResizeBuffer(requiredSize * 2);

	if (size < 2)
	{
		LOG_CORE_ERROR("at least 2 points are required, The input size is ({}).", size);
		return;
	}

	for (size_t i = 0; i < size - 1; i++)
	{
		lines.vertexBufferPtr->position = points[i];
		lines.vertexBufferPtr->thickness = thickness;
		lines.vertexBufferPtr->color = color;
		lines.vertexBufferPtr++;

		lines.vertexBufferPtr->position = points[i + 1];
		lines.vertexBufferPtr->thickness = thickness;
		lines.vertexBufferPtr->color = color;
		lines.vertexBufferPtr++;

		lines.vertexCount += 2;
	}
}

void Tiny2D::DrawLineStrip(std::span<Math::float3> span, const Math::float4& color, float thickness)
{
	DrawLineStrip(span.data(), (uint32_t)span.size(), color, thickness);
}

void Tiny2D::DrawWireBox(const WireBoxDesc& desc)
{
	auto transform = ConstructTransformMatrix(desc.position, desc.rotation, desc.scale);

	Math::float3 vertices[8] = {
		Math::float3(-0.5f, -0.5f, -0.5f), // Bottom-left-front
		Math::float3(0.5f, -0.5f, -0.5f),  // Bottom-right-front
		Math::float3(0.5f, -0.5f,  0.5f),  // Bottom-right-back
		Math::float3(-0.5f, -0.5f,  0.5f), // Bottom-left-back
		Math::float3(-0.5f,  0.5f, -0.5f), // Top-left-front
		Math::float3(0.5f,  0.5f, -0.5f),  // Top-right-front
		Math::float3(0.5f,  0.5f,  0.5f),  // Top-right-back
		Math::float3(-0.5f,  0.5f,  0.5f)  // Top-left-back
	};

	Math::float3 transformedVertices[8];
	for (size_t i = 0; i < 8; i++)
		transformedVertices[i] = Math::float3(transform * Math::float4(vertices[i], 1.0f));

	// Define the line segments of the box
	Math::float3 boxLines[24] = {
		// Top edges
		transformedVertices[4], transformedVertices[5],
		transformedVertices[5], transformedVertices[6],
		transformedVertices[6], transformedVertices[7],
		transformedVertices[7], transformedVertices[4],
		// Bottom edges
		transformedVertices[0], transformedVertices[1],
		transformedVertices[1], transformedVertices[2],
		transformedVertices[2], transformedVertices[3],
		transformedVertices[3], transformedVertices[0],
		// Vertical edges
		transformedVertices[0], transformedVertices[4],
		transformedVertices[1], transformedVertices[5],
		transformedVertices[2], transformedVertices[6],
		transformedVertices[3], transformedVertices[7]
	};

	Tiny2D::DrawLineList(boxLines, desc.color, desc.thickness);
}

void Tiny2D::DrawWireSphere(const WireSphereDesc& desc)
{
	Tiny2D::DrawCircle({
		.position = desc.position,
		.rotation = desc.rotation,
		.radius = desc.radius,
		.color = desc.color,
		.thickness = desc.thickness,
	});

	Tiny2D::DrawCircle({
		.position = desc.position,
		.rotation = desc.rotation * Math::angleAxis(Math::half_pi<float>(), Math::float3(1.0f, 0.0f, 0.0f)),
		.radius = desc.radius,
		.color = desc.color,
		.thickness = desc.thickness,
	});

	Tiny2D::DrawCircle({
		.position = desc.position,
		.rotation = desc.rotation * Math::angleAxis(Math::half_pi<float>(), Math::float3(0.0f, 1.0f, 0.0f)),
		.radius = desc.radius,
		.color = desc.color,
		.thickness = desc.thickness,
	});
}

void Tiny2D::DrawWireCylinder(const WireCylinderDesc& desc)
{
	float r = desc.radius;
	float halfHeight = desc.height * 0.5f;

	Math::float3 topPos = desc.position + desc.rotation * Math::float3(0, halfHeight, 0);
	Math::float3 bottomPos = desc.position + desc.rotation * Math::float3(0, -halfHeight, 0);
	Math::float3 up = Math::normalize(topPos - bottomPos);
	Math::float3 temp = (fabs(up.x) < 0.99f) ? Math::float3(1, 0, 0) : Math::float3(0, 0, 1);
	Math::float3 right = Math::normalize(Math::cross(up, temp));
	Math::float3 forward = Math::normalize(Math::cross(right, up));

	{
		Math::float3 p[8] = {
			topPos + right * r, bottomPos + right * r,
			topPos + forward * r, bottomPos + forward * r,
			topPos - right * r, bottomPos - right * r,
			topPos - forward * r, bottomPos - forward * r
		};

		Tiny2D::DrawLineList(p, desc.color);
	}

	auto drawFullCircle = [&](Math::float3 center, Math::float3 axis) {
		Math::float3 points[33];

		Math::float3 tangent = Math::normalize(Math::cross(axis, right));
		if (Math::length2(tangent) < 1e-4f) tangent = Math::normalize(Math::cross(axis, forward));
		Math::float3 bitangent = Math::normalize(Math::cross(axis, tangent));

		float step = Math::two_pi<float>() / 32;
		
		for (int i = 0; i <= 32; i++)
			points[i] = center + (cos(i * step) * tangent + sin(i * step) * bitangent) * r;

		Tiny2D::DrawLineStrip(points, desc.color);
	};

	drawFullCircle(topPos, up);
	drawFullCircle(bottomPos, up);
}

void Tiny2D::DrawWireCapsule(const WireCapsuleDesc& desc)
{
	float r = desc.radius;
	float halfHeight = desc.height * 0.5f;

	Math::float3 topPos = desc.position + desc.rotation * Math::float3(0, halfHeight, 0);
	Math::float3 bottomPos = desc.position + desc.rotation * Math::float3(0, -halfHeight, 0);
	Math::float3 up = Math::normalize(topPos - bottomPos);
	Math::float3 temp = (fabs(up.x) < 0.99f) ? Math::float3(1, 0, 0) : Math::float3(0, 0, 1);
	Math::float3 right = Math::normalize(Math::cross(up, temp));
	Math::float3 forward = Math::normalize(Math::cross(right, up));

	{
		Math::float3 p[8] = {
			topPos + right * r, bottomPos + right * r,
			topPos + forward * r, bottomPos + forward * r,
			topPos - right * r, bottomPos - right * r,
			topPos - forward * r, bottomPos - forward * r
		};
		Tiny2D::DrawLineList(p, desc.color);
	}

	auto drawFullCircle = [&](Math::float3 center, Math::float3 axis) {

		Math::float3 points[33];

		Math::float3 tangent = Math::normalize(Math::cross(axis, right));
		if (Math::length2(tangent) < 1e-4f) tangent = Math::normalize(Math::cross(axis, forward));
		Math::float3 bitangent = Math::normalize(Math::cross(axis, tangent));

		float step = Math::two_pi<float>() / 32;

		for (int i = 0; i <= 32; i++)
			points[i] = center + (cos(i * step) * tangent + sin(i * step) * bitangent) * r;

		Tiny2D::DrawLineStrip(points, desc.color);
	};

	drawFullCircle(topPos, up);
	drawFullCircle(bottomPos, up);

	auto drawHalfCircle = [&](Math::float3 axis) {
		
		Math::float3 points[17];
		Math::float3 tangent = Math::normalize(Math::cross(axis, up));
		if (Math::length2(tangent) < 1e-4f) tangent = Math::normalize(Math::cross(axis, right));
		Math::float3 bitangent = Math::normalize(Math::cross(axis, tangent));
		
		float step = Math::two_pi<float>() / 32;

		for (int i = 32; i >= 16; i--)
			points[32 - i] = topPos + (cos(i * step) * tangent + sin(i * step) * bitangent) * r;
		
		Tiny2D::DrawLineStrip(points, desc.color);

		for (int i = 0; i < 17; i++)
			points[i] = bottomPos + (cos(i * step) * tangent + sin(i * step) * bitangent) * r;
		
		Tiny2D::DrawLineStrip(points, desc.color);
	};

	drawHalfCircle(right);
	drawHalfCircle(forward);
}

void Tiny2D::DrawMeshWireframe(Math::float4x4 wt, const Math::float3* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount, const Math::vec4& color)
{
	if (indices && indexCount >= 3)
	{
		for (size_t i = 0; i < indexCount; i += 3)
		{
			Math::float3 a = Transform(vertices[indices[i + 0]], wt);
			Math::float3 b = Transform(vertices[indices[i + 1]], wt);
			Math::float3 c = Transform(vertices[indices[i + 2]], wt);

			Tiny2D::DrawLine({ .from = a, .to = b, .fromColor = color, .toColor = color });
			Tiny2D::DrawLine({ .from = b, .to = c, .fromColor = color, .toColor = color });
			Tiny2D::DrawLine({ .from = c, .to = a, .fromColor = color, .toColor = color });
		}
	}
	else
	{
		for (size_t i = 0; i + 2 < vertexCount; i += 3)
		{
			Math::float3 a = Transform(vertices[i + 0], wt);
			Math::float3 b = Transform(vertices[i + 1], wt);
			Math::float3 c = Transform(vertices[i + 2], wt);

			Tiny2D::DrawLine({ .from = a, .to = b, .fromColor = color, .toColor = color });
			Tiny2D::DrawLine({ .from = b, .to = c, .fromColor = color, .toColor = color });
			Tiny2D::DrawLine({ .from = c, .to = a, .fromColor = color, .toColor = color });
		}
	}
}

void Tiny2D::DrawAABB(const AABBDesc& aabb)
{
	Math::float3 shift = Math::abs(aabb.max - aabb.min);

	Math::float3 top1 = aabb.max;
	Math::float3 top2 = aabb.max - Math::float3(shift.x, 0.0f, 0.0f);
	Math::float3 top3 = aabb.max - Math::float3(shift.x, 0.0f, shift.z);
	Math::float3 top4 = aabb.max - Math::float3(0.0f, 0.0f, shift.z);

	Math::float3 bottom1 = aabb.min + Math::float3(shift.x, 0.0f, shift.z);
	Math::float3 bottom2 = aabb.min + Math::float3(0.0f, 0.0f, shift.z);
	Math::float3 bottom3 = aabb.min;
	Math::float3 bottom4 = aabb.min + Math::float3(shift.x, 0.0f, 0.0f);

	Math::float3 boxLines[24] = {
		top1, top2, top2, top3, top3, top4, top4, top1,
		bottom1, bottom2, bottom2, bottom3, bottom3, bottom4, bottom4, bottom1,
		top1, bottom1, top2, bottom2, top3 , bottom3, top4, bottom4 // Vertical lines
	};

	Tiny2D::DrawLineList(boxLines, aabb.color, aabb.thickness);
}

void Tiny2D::DrawBox(const Tiny2D::BoxDesc& desc)
{
	auto& box = s_Data->fd->box;

	if (box.instanceCount >= box.maxInstanceCount)
		box.ResizeBuffer();
	{
		box.instanceDataPtr->position = desc.position;
		box.instanceDataPtr->rotation = desc.rotation;
		box.instanceDataPtr->scale = desc.scale;
		box.instanceDataPtr->color = desc.color;
		box.instanceDataPtr++;

		box.instanceCount++;
	}
}

void Tiny2D::DrawQuad(const Tiny2D::QuadDesc& desc)
{
	int textureID = desc.texture ? s_Data->descriptorTableManager.CreateDescriptor(nvrhi::BindingSetItem::Texture_SRV(0, desc.texture)) : 0;

	auto& sprite = s_Data->fd->sprite;

	if (sprite.instanceCount >= sprite.maxInstanceCount)
		sprite.ResizeBuffer();

	{
		sprite.instanceDataPtr->position = desc.position;
		sprite.instanceDataPtr->rotation = desc.rotation;
		sprite.instanceDataPtr->scale = desc.scale;
		sprite.instanceDataPtr->uv = { desc.minUV.x, desc.minUV.y, desc.maxUV.x, desc.maxUV.y };
		sprite.instanceDataPtr->color = desc.color;
		sprite.instanceDataPtr->textureID = textureID;
		sprite.instanceDataPtr->id = desc.id;
		sprite.instanceDataPtr++;

		sprite.instanceCount++;
	}
}

void Tiny2D::DrawCircle(const Tiny2D::CircleDesc& desc)
{
	auto& circle = s_Data->fd->circle;

	if (circle.instanceCount >= circle.maxInstanceCount)
		circle.ResizeBuffer();
	{
		circle.instanceDataPtr->position = desc.position;
		circle.instanceDataPtr->radius = desc.radius;
		circle.instanceDataPtr->rotation = desc.rotation;
		circle.instanceDataPtr->color = desc.color;
		circle.instanceDataPtr->thickness = desc.thickness;
		circle.instanceDataPtr++;

		circle.instanceCount++;
	}
}

void Tiny2D::DrawText(const TextDesc& desc)
{
	auto& font = s_Data->defaultFont;
	if (!font) return;

	auto& text = s_Data->fd->text;

	uint32_t requiredSize = text.instanceCount + (uint32_t)desc.text.size();
	if (requiredSize >= text.maxInstanceCount)
		text.ResizeBuffer(requiredSize * 2);

	int textureID = s_Data->descriptorTableManager.CreateDescriptor(nvrhi::BindingSetItem::Texture_SRV(0, font->atlasTexture));

	const auto& fontGeometry = font->fontGeometry;
	const auto& metrics = fontGeometry.getMetrics();

	const double fsScale = 1.0 / (metrics.ascenderY - metrics.descenderY);
	double x = 0.0, y = 0.0;

	for (size_t i = 0; i < desc.text.size(); i++)
	{
		const char character = desc.text[i];

		if (character == '\r')
			continue;

		if (character == '\n')
		{
			x = 0;
			y -= fsScale * metrics.lineHeight;
			continue;
		}

		auto glyph = fontGeometry.getGlyph(character);
		if (!glyph) glyph = fontGeometry.getGlyph('?');
		if (!glyph) return; // if '?' glyph is missing, exit

		if (character == '\t')
		{
			glyph = fontGeometry.getGlyph(' ');
			x += (fsScale * glyph->getAdvance() + desc.kerningOffset) * 4;
		}

		double al, ab, ar, at;
		glyph->getQuadAtlasBounds(al, ab, ar, at);
		Math::float2 texCoordMin((float)al, (float)ab);
		Math::float2 texCoordMax((float)ar, (float)at);

		double pl, pb, pr, pt;
		glyph->getQuadPlaneBounds(pl, pb, pr, pt);
		Math::float2 quadMin((float)pl, (float)pb);
		Math::float2 quadMax((float)pr, (float)pt);

		quadMin = quadMin * fsScale + Math::float2(x, y);
		quadMax = quadMax * fsScale + Math::float2(x, y);

		float texelWidth = 1.0f / font->atlasTexture->getDesc().width;
		float texelHeight = 1.0f / font->atlasTexture->getDesc().height;
		texCoordMin *= Math::float2(texelWidth, texelHeight);
		texCoordMax *= Math::float2(texelWidth, texelHeight);

		Math::float2 center = (quadMin + quadMax) * 0.5f;
		Math::float2 size = quadMax - quadMin;

		Math::float3 worldPos = desc.position + desc.rotation * Math::float3(center, 0.0f) * desc.scale;
		Math::float3 worldScale = Math::float3(size, 1.0f) * desc.scale;

		text.instanceDataPtr->position = worldPos;
		text.instanceDataPtr->rotation = desc.rotation;
		text.instanceDataPtr->scale = worldScale;
		text.instanceDataPtr->color = desc.color;
		text.instanceDataPtr->uv = { texCoordMin, texCoordMax };
		text.instanceDataPtr->textureID = textureID;
		text.instanceDataPtr++;

		text.instanceCount++;

		if (i < desc.text.size() - 1)
		{
			double advance = glyph->getAdvance();
			fontGeometry.getAdvance(advance, character, desc.text[i + 1]);
			x += fsScale * advance + desc.kerningOffset;
		}
	}
}
