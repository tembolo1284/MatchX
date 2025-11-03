-- MatchX Matching Engine Build Configuration
-- Clean build structure: everything goes in build/
workspace "MatchEngine"
    architecture "x64"
    configurations { "debug", "release" }  -- lowercase to get build/bin/release/
    
    -- Put all generated Makefiles in build/
    location "build"
    
    -- Output directories - when location is "build", these are relative to build/
    -- So this creates build/bin/release/ and build/obj/release/
    targetdir ("%{wks.location}/bin/%{cfg.buildcfg}")
    objdir ("%{wks.location}/obj/%{cfg.buildcfg}/%{prj.name}")
    
    filter "system:windows"
        systemversion "latest"
    filter {}

-- MatchX Shared Library Project
project "MatchEngine"
    kind "SharedLib"
    language "C++"
    cppdialect "C++14"
    staticruntime "off"
    
    -- Source files - paths relative to workspace (project root)
    files {
        "%{wks.location}/../include/matchengine.h",
        "%{wks.location}/../include/internal/**.h",
        "%{wks.location}/../src/**.cpp"
    }
    
    -- Include directories - relative to project root
    includedirs {
        "%{wks.location}/../include",
        "%{wks.location}/../include/internal"
    }
    
    defines {
        "MX_BUILD_SHARED"
    }
    
    filter "configurations:release"
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
    
    filter "configurations:debug"
        defines { "MX_DEBUG", "_DEBUG" }
        symbols "On"
        optimize "Off"
    filter {}
    
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

-- Static Library variant
project "MatchEngineStatic"
    kind "StaticLib"
    language "C++"
    cppdialect "C++14"
    staticruntime "on"
    
    files {
        "%{wks.location}/../include/matchengine.h",
        "%{wks.location}/../include/internal/**.h",
        "%{wks.location}/../src/**.cpp"
    }
    
    includedirs {
        "%{wks.location}/../include",
        "%{wks.location}/../include/internal"
    }
    
    defines {
        "MX_BUILD_STATIC"
    }
    
    filter "configurations:release"
        defines { "NDEBUG" }
        optimize "Speed"
    filter {}
    
    filter "configurations:debug"
        defines { "MX_DEBUG", "_DEBUG" }
        symbols "On"
    filter {}

-- Example: Basic Usage (C)
project "BasicExample"
    kind "ConsoleApp"
    language "C"
    cdialect "C99"
    staticruntime "off"
    
    files {
        "%{wks.location}/../examples/basic_usage.c"
    }
    
    includedirs {
        "%{wks.location}/../include"
    }
    
    links {
        "MatchEngine"
    }
    
    -- Library is in build/bin/release (same directory as this executable)
    libdirs {
        "%{cfg.buildtarget.directory}"
    }
    
    filter "system:linux"
        links { "m", "pthread" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
    filter "system:macosx"
        linkoptions { "-Wl,-rpath,@executable_path" }
    filter {}
    
    filter "configurations:debug"
        symbols "On"
    filter {}
    
    filter "configurations:release"
        optimize "On"
    filter {}

-- Example: Advanced Usage (C++)
project "AdvancedExample"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++14"
    staticruntime "off"
    
    files {
        "%{wks.location}/../examples/advanced_usage.cpp"
    }
    
    includedirs {
        "%{wks.location}/../include"
    }
    
    links {
        "MatchEngine"
    }
    
    libdirs {
        "%{cfg.buildtarget.directory}"
    }
    
    filter "system:linux"
        links { "pthread" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
    filter "system:macosx"
        linkoptions { "-Wl,-rpath,@executable_path" }
    filter {}
    
    filter "configurations:debug"
        symbols "On"
    filter {}
    
    filter "configurations:release"
        optimize "On"
    filter {}

-- Example: Benchmark
project "Benchmark"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++14"
    staticruntime "off"
    
    files {
        "%{wks.location}/../examples/benchmark.cpp"
    }
    
    includedirs {
        "%{wks.location}/../include"
    }
    
    links {
        "MatchEngine"
    }
    
    libdirs {
        "%{cfg.buildtarget.directory}"
    }
    
    filter "system:linux"
        links { "pthread" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
    filter "system:macosx"
        linkoptions { "-Wl,-rpath,@executable_path" }
    filter {}
    
    filter "configurations:debug"
        symbols "On"
    filter {}
    
    filter "configurations:release"
        optimize "Speed"
        defines { "NDEBUG" }
    filter {}
