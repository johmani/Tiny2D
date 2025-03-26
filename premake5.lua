group "Plugins/Tiny2D"

include "msdf-atlas-gen"

project "Tiny2D"
    kind "SharedLib"
    language "C++"
    cppdialect  "C++latest"
    staticruntime "Off"
    targetdir (binOutputDir)
    objdir (IntermediatesOutputDir)

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
         "HE_CORE_IMPORT_SHAREDLIB",
         "TINY2D_BUILD_SHAREDLIB",
         "NVRHI_SHARED_LIBRARY_INCLUDE",
         "_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING",
    }
    
    includedirs
    {
       "Source/Tiny2D",
       "%{IncludeDir.HydraEngine}",
       "msdf-atlas-gen/msdf-atlas-gen",
       "msdf-atlas-gen/msdfgen",
    }
    
    links
    {
       "HydraEngine",
       "nvrhi",
       "msdf-atlas-gen"
    }

    buildoptions 
    {
        AddCppm("std"),
        AddCppm("HydraEngine"),
        AddCppm("nvrhi"),
        AddCppm("HydraEngine", "glm")
    }

    filter { "files:**.hlsl" }
        buildcommands {
            BuildShaders(
                { D3D12 = true, VULKAN = true },          -- api
                "%{prj.location}/Source/Shaders",         -- sourceDir
                "%{prj.location}/Source/Tiny2D/Embeded",  -- cacheDir
                "--header",                               -- args
                {}
            ),
        }
        buildoutputs { "%{wks.location}/dumy" }
    filter {}
    
    filter "system:windows"
        systemversion "latest"
        defines 
        {
        	"NVRHI_HAS_D3D12",
        	"NVRHI_HAS_VULKAN",
        }

    filter "system:linux"
		pic "On"
		systemversion "latest"
        defines
        {
        	"NVRHI_HAS_VULKAN",
        }
     
    filter "configurations:Debug"
     	defines "HE_DEBUG"
     	runtime "Debug"
     	symbols "On"
     
    filter "configurations:Release"
     	defines "HE_RELEASE"
     	runtime "Release"
     	optimize "On"
     
    filter "configurations:Profile"
        includedirs { "%{IncludeDir.tracy}" }
        defines { "HE_PROFILE", "TRACY_IMPORTS" }
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
     	defines "HE_DIST"
     	runtime "Release"
     	optimize "Speed"
        symbols "Off"

group "Plugins"