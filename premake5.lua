IncludeDir["Tiny2D"] = "%{HE}/Plugins/Tiny2D/Include"

function Link.Plugin.Tiny2D()

    buildoptions {

        AddProjCppm(HE, "Tiny2D"),
    }

    includedirs {

        "%{IncludeDir.Tiny2D}",
    }

    links {

        "Tiny2D",
    }
end

group "Plugins/Tiny2D"

include "ThirdParty/msdf-atlas-gen"

project "Tiny2D"
    kind "SharedLib"
    language "C++"
    cppdialect  "C++latest"
    staticruntime "Off"
    targetdir (binOutputDir)
    objdir (IntermediatesOutputDir)

    Link.Runtime.Core()

    files
    {
        "Source/Tiny2D/**.h",
        "Source/Tiny2D/**.cpp",
        "Source/Tiny2D/**.cppm",

        "Include/**.h",
        "Include/**.cppm",
        
        "Source/Shaders/**.hlsl",
        "Source/Shaders/**.h",
        "Source/Shaders/shaders.cfg",
        
        "*.lua",
    }
    
    defines
    {
        "TINY2D_AS_SHAREDLIB",
        "TINY2D_BUILD",
    }

    includedirs
    {
       "Include",
       "ThirdParty/msdf-atlas-gen/msdf-atlas-gen",
       "ThirdParty/msdf-atlas-gen/msdfgen",
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
