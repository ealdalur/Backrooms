You are an expert low-level Graphics Engineer and Software Architect specializing in modern cross-platform C++ development (Windows/Linux).

Your task is to design and write a modular, object-oriented 3D first-person "Backrooms" infinite labyrinth exploration simulator. You must partition the codebase across a clean directory structure containing well-encapsulated C++ header (.h) and source (.cpp) files.

### Technical Stack, Windowing, and Dependencies
- Language Profile: Cross-platform Modern C++ (C++17). Must compile flawlessly under both MSVC (Windows) and GCC/Clang (Linux) using the provided CMake targets. The provided "Main.cpp" file is just an example with a spinning pyramid that successfully compiles with this build system. Feel free to overwrite anything in this file and make this the application entry point. 
- Windowing & Input: SDL3 (Simple DirectMedia Layer 3).
- Graphics Pipeline: OpenGL 3.3 Core Profile loaded via the provided GLAD files (`glad.c`/`glad.h`).
- Math: GLM (OpenGL Mathematics) for all vectors, matrices, and coordinate transformations.
- External Assets: ZERO external data files. All textures, materials, and 3D meshes must be generated procedurally in memory at initialization or dynamically on the fly.
- Initial Window State: Launch windowed on Windows/Linux at a fixed default resolution of 1920x1080.

### Codebase Architecture & Modular Segregation
Do not write a single-file application. Implement a sensible class hierarchy partitioned into separate translation units. Generate full implementations for the following decoupled modules:

1. Build & Project Configuration
   - Update the existing CMakeLists.txt to dynamically gather or explicitly list all generated source files (`Main.cpp` and all your modular source files) so they compile correctly under the `Backrooms` target.

2. Core Engine & Windowing (e.g., `Engine.h/.cpp`)
   - Manages the top-level game state, the lifecycle of the SDL3 window, and the OpenGL 3.3 Core context.
   - Hosts the main execution loop, passing delta time to the simulation systems for frame-rate independent updates.

3. Render Subsystem (e.g., `Renderer.h/.cpp`, `Shader.h/.cpp`, `Texture.h/.cpp`)
   - Custom shader compilation wrappers reading raw string literals for Vertex and Fragment shaders.
   - Generates and binds the procedural high-quality materials at startup using noise/fractal math:
     * Dingy, monochromatic yellow wallpaper (with subtle vertical grain/stains).
     * Distressed, damp-looking office carpet.
     * Stained acoustic ceiling tiles.
     * Solid office furniture colors (dull gray metal, artificial wood laminate).
   - Shading Model: Implement Blinn-Phong or basic PBR shading. Model rectangular fluorescent overhead fixtures featuring distance attenuation.
   - Atmospheric Effects: Ceiling lights must randomly flicker. The frequency, duration, and pattern of the flickering for any given light fixture must be deterministically tied to the parent chunk's random seed.

4. World & Map Generation (e.g., `WorldGenerator.h/.cpp`, `Chunk.h/.cpp`)
   - Implements an infinite 3D grid chunk manager.
   - Employs coordinate-based hashing or a deterministic PRNG seeded by chunk `(x, z)` coordinates to build identical room layouts, furniture setups, and light-flicker signatures if the user reverses direction.
   - Procedurally decides paths: segments boundary edges into solid walls, completely open archways, or interactable door frames.

5. Actors & Interactables (e.g., `Player.h/.cpp`, `Door.h/.cpp`, `Furniture.h/.cpp`)
   - `Player`: Encapsulates camera transformations, velocity, kinematics, and input states. 
   - Standard Controls: Implement `W/A/S/D` for directional movement, `Left Shift` to run, and `Spacebar` to jump. Adds velocity-linked sine/cosine head-bobbing.
   - Believable Jump Physics: Implement realistic jump kinematics using explicit gravity, an initial vertical impulse velocity, and integration with delta time. Avoid floaty or instantaneous height changes.
   - Crouching with Smooth Easing: Pressing or holding the `C` key triggers crouching. Smoothly interpolate (lerp or ease-out) the target camera height and physical AABB collision height over time between standing and crouching states. Scale down maximum movement speed while crouched.
   - Specialized Mouse Overrides: 
     * Default state (Right Mouse Button NOT held): Mouse movement controls camera looking (Pitch/Yaw) with relative mouse mode enabled via SDL3.
     * Overridden state (Right Mouse Button IS held): Mouse look is completely locked. Moving the mouse horizontally (X-axis) translates to left/right strafe velocity, and moving the mouse vertically (Y-axis) translates to forward/back movement velocity. This translational velocity must be directly scaled by the mouse sensitivity multiplier configuration.
   - `Door`: Tracks individual door states (Open/Closed). Doors must visually resemble wood-grain/laminate corporate office doors. If a player is close to a door and triggers `SDL_SCANCODE_E`, smoothly interpolate the door swing rotation.
   - `Furniture`: Manages procedurally built 3D geometry models (desks, file cabinets, chairs, partition walls) compiled into VBOs/VAOs using indexed drawing.

6. Physics & Collision Handling (e.g., `Physics.h/.cpp`)
   - Implements discrete Axis-Aligned Bounding Box (AABB) checking between the player's bounding extents (which dynamically scale down/up with the crouching interpolation) and all solid architectural obstacles (walls, closed doors, furniture pieces).
   - Ensures the player cannot clip into walls but is legally allowed to step up/jump onto flat surfaces of the generated furniture objects.

### Implementation Constraints
- Avoid legacy immediate mode OpenGL (`glBegin`/`glEnd`) or fixed-function matrices. Use exclusively modern buffer allocations and shader uniform streams.
- Provide clean, robust, production-ready, fully commented code blocks for each required file. Do not truncate implementation segments, skip critical function definitions, or leave pseudo-code structures like `// TODO`.
