-- MatchX Matching Engine Build Configuration
-- Clean build structure: everything goes in build/

workspace "MatchEngine"
    architecture "x64"
    configurations { "Debug", "Release" }
    
    -- Put all generated files in build/
    location "build"
    
    -- Output directories (relative to build/ directory)
    targetdir ("bin/%{cfg.buildcfg}")
    objdir ("obj/%{cfg.buildcfg}/%{prj.name}")
    
    filter "system:windows"
        systemversion "latest"
    filter {}

-- MatchX Shared Library Project
project "MatchEngine"
    kind "SharedLib"
    language "C++"
    cppdialect "C++14"
    staticruntime "off"
    
    -- Source files (relative to build/ directory where Makefile is)
    files {
        "../include/matchengine.h",
        "../include/internal/**.h",
        "../src/**.cpp"
    }
    
    -- Include directories
    includedirs {
        "../include",
        "../include/internal"
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
            
            filter "toolset:gcc or toolset:clang"
                buildoptions {
                    "-ffast-math",
                    "-funroll-loops",
                    "-fvisibility=hidden"
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
        buildoptions { "-fvisibility=hidden" }
        
    filter "system:macosx"
        pic "On"
        buildoptions { "-fvisibility=hidden" }
    filter {}
    
    -- Warnings
    filter "toolset:msc"
        warnings "Extra"
        disablewarnings { "4100" }
        
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
    
    files {
        "../include/matchengine.h",
        "../include/internal/**.h",
        "../src/**.cpp"
    }
    
    includedirs {
        "../include",
        "../include/internal"
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
    
    files {
        "../examples/basic_usage.c"
    }
    
    includedirs {
        "../include"
    }
    
    links {
        "MatchEngine"
    }
    
    -- Set RPATH so executable can find the .so in same directory
    filter "system:linux"
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
    filter "system:macosx"
        linkoptions { "-Wl,-rpath,@executable_path" }
    filter {}
    
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
    
    files {
        "../examples/advanced_usage.cpp"
    }
    
    includedirs {
        "../include"
    }
    
    links {
        "MatchEngine"
    }
    
    filter "system:linux"
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
    filter "system:macosx"
        linkoptions { "-Wl,-rpath,@executable_path" }
    filter {}
    
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
    
    files {
        "../examples/benchmark.cpp"
    }
    
    includedirs {
        "../include"
    }
    
    links {
        "MatchEngine"
    }
    
    filter "system:linux"
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
    filter "system:macosx"
        linkoptions { "-Wl,-rpath,@executable_path" }
    filter {}
    
    filter "configurations:Debug"
        symbols "On"
    filter {}
    
    filter "configurations:Release"
        optimize "Speed"
        defines { "NDEBUG" }
    filter {}
