#pragma once

#include <iostream>
#include <memory>
#include <glad/glad.h>

#include "BloomFBO.h"
#include "GLClasses/Shader.h"
#include "GLClasses/Framebuffer.h"
#include "GLClasses/VertexBuffer.h"
#include "GLClasses/VertexArray.h"

namespace VoxelRT
{
	namespace BloomRenderer
	{
		void Initialize();
		void RenderBloom(BloomFBO& bloom_fbo, BloomFBO& bloom_fbo_alternate, GLuint source_tex, GLuint bright_tex, bool hq, GLuint&, bool Wide, int frame, bool jitter);
		void RecompileShaders();
	}
}