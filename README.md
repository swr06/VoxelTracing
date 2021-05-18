# Voxel Tracing
A Voxel Ray Tracing Engine implemented using C++ and the modern opengl programmable pipeline

# Features Implemented
- Voxel Ray Casting
- Procedural Terrain Generation
- Fast Diffuse Tracing 
- Temporal Diffuse Filtering
- Normal mapping
- PBR Texture support
- Physically based lighting
- Fast Shadow Tracing
- Temporal Anti Aliasing
- FXAA
- Tonemapping, Gamma correction
- Bilateral Upsampling (For SSAO, Trace)
- Ray traced reflections 
- Naive world saving and loading
- Alpha testing (Shadows and tracing)
- Atmosphere 
- God Rays (Screen space) 
- Emissive Materials
- Basic spatial filtering (Not depth or normal aware as of yet.)
- Screen Space Ambient Occlusion
- Lens Flare

# Features to implement
- Refractions (Will be done in screen space)
- Glass rendering 
- Importance Sampling and GGX model for the diffuse tracing
- Proper denoising
- Water Rendering (Tesselation with FFT? tessendorf waves?)
- Voxel intersection acceleration structure (DAGs? Octrees?) 
- Parallax Occlusion Mapping (Try tracing through the volume, Maybe?)
- Cheap clouds 

# Performance

- 60 FPS on a Vega 8 iGPU on the default settings

# Resources used
- https://github.com/BrutPitt/glslSmartDeNoise/
- https://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.42.3443&rep=rep1&type=pdf
- ScratchAPixel

# Thanks
- UglySwedishFish
- Snurf 
- Telo 
- Moonsheep

# Notice
This project is purely educational. I own none of the assets. All the rights go to their respective owners.
