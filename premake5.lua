group "Plugins/Tiny2D"

include "msdf-atlas-gen"

project "Tiny2D"
    kind "SharedLib"
    language "C++"
    cppdialect  "C++latest"
    staticruntime "Off"
    targetdir (binOutputDir)
    objdir (IntermediatesOutputDir)

    LinkHydra(includSourceCode)
    SetHydraFilters()

    files
    {
        "Source/Tiny2D/**.h",
        "Source/Tiny2D/**.cpp",
        "Source/Tiny2D/**.cppm",
        
        "Source/Shaders/**.hlsl",
        "Source/Shaders/**.h",
        "Source/Shaders/shaders.cfg",
        
        "*.lua",
    }
    
    defines
    {
        "TINY2D_BUILD_SHAREDLIB",
    }

    includedirs
    {
       "Source/Tiny2D",
       "msdf-atlas-gen/msdf-atlas-gen",
       "msdf-atlas-gen/msdfgen",
    }
    
    links
    {
       "msdf-atlas-gen"
    }

    SetupShaders(
        { D3D12 = true, VULKAN = true },          -- api
        "%{prj.location}/Source/Shaders",         -- sourceDir
        "%{prj.location}/Source/Tiny2D/Embeded",  -- cacheDir
        "--header"                                -- args
    )

group "Plugins"
