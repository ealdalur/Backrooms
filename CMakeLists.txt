cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW) # Enforce the modern MSVC runtime policy

project(Backrooms C CXX)

add_executable(${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Source files (explicitly listed so the build is reproducible and IDEs show
# every translation unit / header in its module folder).
# ---------------------------------------------------------------------------
set(BACKROOMS_SOURCES
  Main.cpp

  src/AI/Agent.cpp
  src/AI/EntityDirector.cpp
  src/AI/NavGrid.cpp
  src/AI/Stalker.cpp
  src/AI/Wanderer.cpp

  src/Audio/AudioEngine.cpp
  src/Audio/DoomSounds.cpp
  src/Audio/PhoneSounds.cpp
  src/Audio/SoundBank.cpp
  src/Audio/Soundscape.cpp
  src/Audio/SpeechSynth.cpp
  src/Audio/StorySounds.cpp
  src/Audio/TeslaSounds.cpp

  src/Core/CommandLine.cpp
  src/Core/ConsoleLog.cpp
  src/Core/Engine.cpp
  src/Core/EnginePuzzle.cpp
  src/Core/EngineTesla.cpp
  src/Core/GpuSelection.cpp
  src/Core/Input.cpp

  src/Gameplay/Doom/DoomAssets.cpp
  src/Gameplay/Doom/DoomGame.cpp
  src/Gameplay/Doom/DoomLevel.cpp
  src/Gameplay/Doom/DoomRenderer.cpp
  src/Gameplay/Inventory.cpp
  src/Gameplay/Lightning.cpp
  src/Gameplay/PhoneCall.cpp
  src/Gameplay/PuzzleChain.cpp
  src/Gameplay/TerminalConsole.cpp
  src/Gameplay/TerminalText.cpp
  src/Gameplay/TeslaGun.cpp

  src/Math/Noise.cpp

  src/Physics/Physics.cpp

  src/Render/BitmapFont.cpp
  src/Render/Camera.cpp
  src/Render/EntityRenderer.cpp
  src/Render/Frustum.cpp
  src/Render/LightGrid.cpp
  src/Render/LightningRenderer.cpp
  src/Render/MaterialLibrary.cpp
  src/Render/Mesh.cpp
  src/Render/MeshBuilder.cpp
  src/Render/PostProcess.cpp
  src/Render/Renderer.cpp
  src/Render/Shader.cpp
  src/Render/ShaderSources.cpp
  src/Render/TerminalRenderer.cpp
  src/Render/TextOverlay.cpp
  src/Render/TitleRenderer.cpp
  src/Render/Texture.cpp

  src/World/Chunk.cpp
  src/World/ChunkManager.cpp
  src/World/Decals.cpp
  src/World/LightFixture.cpp
  src/World/OfficeLayout.cpp
  src/World/Stairwell.cpp
  src/World/WorldGenerator.cpp

  src/Actors/CreatureRig.cpp
  src/Actors/Door.cpp
  src/Actors/FileCabinet.cpp
  src/Actors/Furniture.cpp
  src/Actors/OfficeFurniture.cpp
  src/Actors/Phone.cpp
  src/Actors/Player.cpp
  src/Actors/TeslaParts.cpp
  src/Actors/Terminal.cpp
)

set(BACKROOMS_HEADERS
  src/AI/Agent.h
  src/AI/EntityDirector.h
  src/AI/NavGrid.h
  src/AI/NoiseEvent.h
  src/AI/Perception.h
  src/AI/Stalker.h
  src/AI/Wanderer.h

  src/Audio/AudioEngine.h
  src/Audio/DoomSounds.h
  src/Audio/PhoneSounds.h
  src/Audio/Dsp.h
  src/Audio/SoundBank.h
  src/Audio/Soundscape.h
  src/Audio/SpeechSynth.h
  src/Audio/StorySounds.h
  src/Audio/SynthKit.h
  src/Audio/TeslaSounds.h

  src/Core/Config.h
  src/Core/CommandLine.h
  src/Core/ConsoleLog.h
  src/Core/Engine.h
  src/Core/GpuSelection.h
  src/Core/Input.h

  src/Gameplay/Doom/DoomAssets.h
  src/Gameplay/Doom/DoomGame.h
  src/Gameplay/Doom/DoomLevel.h
  src/Gameplay/Doom/DoomRenderer.h
  src/Gameplay/Inventory.h
  src/Gameplay/Items.h
  src/Gameplay/Lightning.h
  src/Gameplay/PhoneCall.h
  src/Gameplay/PuzzleChain.h
  src/Gameplay/Shockable.h
  src/Gameplay/TerminalConsole.h
  src/Gameplay/TerminalText.h
  src/Gameplay/TeslaGun.h

  src/Math/Noise.h
  src/Math/Random.h

  src/Physics/AABB.h
  src/Physics/Physics.h

  src/Render/AtlasLayout.h
  src/Render/BitmapFont.h
  src/Render/Camera.h
  src/Render/EntityRenderer.h
  src/Render/Frustum.h
  src/Render/LightGrid.h
  src/Render/LightningRenderer.h
  src/Render/MaterialLibrary.h
  src/Render/MaterialTypes.h
  src/Render/Mesh.h
  src/Render/MeshBuilder.h
  src/Render/PostProcess.h
  src/Render/Renderer.h
  src/Render/Shader.h
  src/Render/ShaderSources.h
  src/Render/TerminalRenderer.h
  src/Render/TextOverlay.h
  src/Render/TitleRenderer.h
  src/Render/Texture.h

  src/World/Chunk.h
  src/World/ChunkCoord.h
  src/World/ChunkManager.h
  src/World/Decals.h
  src/World/LightFixture.h
  src/World/OfficeLayout.h
  src/World/Stairwell.h
  src/World/WorldConstants.h
  src/World/WorldGenerator.h

  src/Actors/CreatureRig.h
  src/Actors/Door.h
  src/Actors/FileCabinet.h
  src/Actors/Furniture.h
  src/Actors/OfficeFurniture.h
  src/Actors/Phone.h
  src/Actors/Player.h
  src/Actors/TeslaParts.h
  src/Actors/Terminal.h
)

target_sources(${PROJECT_NAME} PRIVATE
  ${BACKROOMS_SOURCES}
  ${BACKROOMS_HEADERS}
  glad.c
  glad.h
  khrplatform.h
)

# Mirror the on-disk folder layout inside Visual Studio / Xcode solution trees.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${BACKROOMS_SOURCES} ${BACKROOMS_HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}        # glad.h / khrplatform.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src    # module headers, e.g. "Render/Shader.h"
)

set_source_files_properties(glad.c PROPERTIES
  COMPILE_DEFINITIONS "GLAD_GL_IMPLEMENTATION"
  LANGUAGE C
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  GLM_FORCE_RADIANS
  GLM_FORCE_SILENT_WARNINGS
)

if(MSVC)
  #statically link MSVC runtime library
  set_target_properties(${PROJECT_NAME} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

  #set compiler optimization flags
  target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Debug>:/Od>
    $<$<COMPILE_LANGUAGE:CXX>:/W3 /permissive- /utf-8 /Zc:__cplusplus /MP>
  )
else() # For compilers like GCC or Clang
  #set compiler optimization flags
  target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-O0>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>
  )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Threads REQUIRED)   # procedural texture / sound synthesis runs in parallel
target_link_libraries(${PROJECT_NAME} PRIVATE
  SDL3::SDL3
  OpenGL::GL
  glm::glm
  Threads::Threads
  ${CMAKE_DL_LIBS}               # glad uses dlopen() on Linux
)

if(WIN32)
  # DXGI: used to verify OpenGL landed on the high-performance GPU on
  # hybrid-graphics laptops (see src/Core/GpuSelection.cpp).
  target_link_libraries(${PROJECT_NAME} PRIVATE dxgi)
endif()
