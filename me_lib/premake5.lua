-- MatchX Matching Engine Build Configuration
-- Generates Visual Studio, Xcode, or Makefiles for building the library

workspace "MatchEngine"
    architecture "x64"
    configurations { "Debug", "Release" }
    startproject "MatchEngine"
    
    -- Output directories
    outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
    
    filter "system:windows"
        systemversion "latest"
    filter {}

-- MatchX Shared Library Project
project "MatchEngine"
    kind "SharedLib"
    language "C++"
    cppdialect "C++14"
    staticruntime "off"
    
    targetdir ("bin/" .. outputdir)
    objdir ("obj/" .. outputdir)
    
    -- Source files
    files {
        "include/matchengine.h",
        "include/internal/**.h",
        "src/**.cpp"
    }
    
    -- Include directories
    includedirs {
        "include",
        "include/internal"
    }
    
    -- Defines
    defines {
        "MX_BUILD_SHARED"
    }
    
    -- Compiler flags for performance
    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "Speed"
        inlining "Auto"
        flags { "LinkTimeOptimization" }
        
        filter "system:windows"
            buildoptions { "/O2", "/Oi", "/Ot", "/GL" }
            linkoptions { "/LTCG" }
        
        filter "system:linux or system:macosx"
            buildoptions { "-O3", "-march=native", "-mtune=native" }
            
            -- GCC/Clang specific optimizations
            filter "toolset:gcc or toolset:clang"
                buildoptions {
                    "-ffast-math",
                    "-funroll-loops",
                    "-fvisibility=hidden"  -- Hide symbols by default
                }
    filter {}
    
    -- Debug configuration
    filter "configurations:Debug"
        defines { "MX_DEBUG", "_DEBUG" }
        symbols "On"
        optimize "Off"
    filter {}
    
    -- Platform-specific settings
    filter "system:windows"
        defines {
            "_WINDOWS",
            "_USRDLL",
            "MX_BUILD_SHARED"
        }
        
    filter "system:linux"
        links { "pthread" }
        pic "On"
        
        -- Symbol visibility
        buildoptions {
            "-fvisibility=hidden"
        }
        
    filter "system:macosx"
        pic "On"
        
        -- Symbol visibility
        buildoptions {
            "-fvisibility=hidden"
        }
        
    filter {}
    
    -- Warnings
    filter "toolset:msc"
        warnings "Extra"
        disablewarnings { "4100" }  -- unreferenced formal parameter
        
    filter "toolset:gcc or toolset:clang"
        warnings "Extra"
        buildoptions {
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Wno-unused-parameter"
        }
    filter {}

-- Static Library variant (optional)
project "MatchEngineStatic"
    kind "StaticLib"
    language "C++"
    cppdialect "C++14"
    staticruntime "on"
    
    targetdir ("bin/" .. outputdir)
    objdir ("obj/" .. outputdir)
    
    files {
        "include/matchengine.h",
        "include/internal/**.h",
        "src/**.cpp"
    }
    
    includedirs {
        "include",
        "include/internal"
    }
    
    defines {
        "MX_BUILD_STATIC"
    }
    
    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "Speed"
    filter {}
    
    filter "configurations:Debug"
        defines { "MX_DEBUG", "_DEBUG" }
        symbols "On"
    filter {}

-- Example: Basic Usage
project "BasicExample"
    kind "ConsoleApp"
    language "C"
    cdialect "C99"
    staticruntime "off"
    
    targetdir ("bin/" .. outputdir .. "/examples")
    objdir ("obj/" .. outputdir .. "/examples")
    
    files {
        "examples/basic_usage.c"
    }
    
    includedirs {
        "include"
    }
    
    links {
        "MatchEngine"
    }
    
    filter "configurations:Debug"
        symbols "On"
    filter {}
    
    filter "configurations:Release"
        optimize "On"
    filter {}

-- Example: Advanced Usage (C++)
project "AdvancedExample"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++14"
    staticruntime "off"
    
    targetdir ("bin/" .. outputdir .. "/examples")
    objdir ("obj/" .. outputdir .. "/examples")
    
    files {
        "examples/advanced_usage.cpp"
    }
    
    includedirs {
        "include"
    }
    
    links {
        "MatchEngine"
    }
    
    filter "configurations:Debug"
        symbols "On"
    filter {}
    
    filter "configurations:Release"
        optimize "On"
    filter {}

-- Example: Benchmark
project "Benchmark"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++14"
    staticruntime "off"
    
    targetdir ("bin/" .. outputdir .. "/examples")
    objdir ("obj/" .. outputdir .. "/examples")
    
    files {
        "examples/benchmark.cpp"
    }
    
    includedirs {
        "include"
    }
    
    links {
        "MatchEngine"
    }
    
    filter "configurations:Debug"
        symbols "On"
    filter {}
    
    filter "configurations:Release"
        optimize "Speed"
        defines { "NDEBUG" }
    filter {}

-- Clean action
newaction {
    trigger = "clean",
    description = "Clean all build files and outputs",
    execute = function()
        os.rmdir("bin")
        os.rmdir("obj")
        os.remove("*.sln")
        os.remove("*.vcxproj")
        os.remove("*.vcxproj.filters")
        os.remove("*.vcxproj.user")
        os.remove("*.workspace")
        os.remove("*.make")
        os.remove("Makefile")
        print("Cleaned build files")
    end
}
