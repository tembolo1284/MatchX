-- me_server/premake5.lua
-- Build configuration for the matching engine server

workspace "MatchingEngine"
    architecture "x86_64"
    configurations { "Debug", "Release" }
    startproject "TradingClient"
    
    -- Put generated files in build/
    location "build"
    
    -- Output directories (absolute)
    outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
    basedir = path.getdirectory(_SCRIPT)  -- me_server/
    
    -- Workspace-wide build options
    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"
        optimize "Off"
        
    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "Speed"
        symbols "Off"
        
    filter "system:linux"
        buildoptions { "-std=c++17", "-Wall", "-Wextra", "-pthread" }
        links { "pthread" }
        
    filter "system:macosx"
        buildoptions { "-std=c++17", "-Wall", "-Wextra", "-pthread" }
        links { "pthread" }
        
    filter {}

-- =============================================================================
-- ENGINE - Matching Engine Process
-- =============================================================================
project "Engine"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    
    targetdir (basedir .. "/bin/" .. outputdir .. "/%{prj.name}")
    objdir (basedir .. "/build/obj/" .. outputdir .. "/%{prj.name}")
    
    files {
        basedir .. "/engine/src/**.cpp",
        basedir .. "/engine/src/**.h",
        basedir .. "/common/**.h"
    }
    
    includedirs {
        basedir .. "/engine/src",
        basedir .. "/common",
        basedir .. "/../me_lib/include"
    }
    
    -- Link against me_lib
    filter "configurations:Release"
        libdirs {
            basedir .. "/../me_lib/build/bin/release",
            basedir .. "/../me_lib/build/bin/debug"
        }
        
    filter "configurations:Debug"
        libdirs {
            basedir .. "/../me_lib/build/bin/debug",
            basedir .. "/../me_lib/build/bin/release"
        }
        
    filter {}
    
    links {
        "MatchEngineStatic"
    }
    
    targetname "matching_engine"

-- =============================================================================
-- GATEWAY - TCP Gateway Server
-- =============================================================================
project "Gateway"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    
    targetdir (basedir .. "/bin/" .. outputdir .. "/%{prj.name}")
    objdir (basedir .. "/build/obj/" .. outputdir .. "/%{prj.name}")
    
    files {
        basedir .. "/gateway/src/**.cpp",
        basedir .. "/gateway/src/**.h",
        basedir .. "/common/**.h"
    }
    
    includedirs {
        basedir .. "/gateway/src",
        basedir .. "/common"
    }
    
    targetname "gateway_server"

-- =============================================================================
-- CLIENT - Trading Client
-- =============================================================================
project "TradingClient"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    
    targetdir (basedir .. "/bin/" .. outputdir .. "/%{prj.name}")
    objdir (basedir .. "/build/obj/" .. outputdir .. "/%{prj.name}")
    
    files {
        basedir .. "/client/src/**.cpp",
        basedir .. "/client/src/**.h",
        basedir .. "/common/**.h"
    }
    
    includedirs {
        basedir .. "/client/src",
        basedir .. "/common"
    }
    
    targetname "trading_client"
