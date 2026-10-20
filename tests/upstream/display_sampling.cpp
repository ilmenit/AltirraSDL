// Execute Altirra's production fragment shader on real offscreen GPU textures.
// No HDR monitor or swapchain is needed to verify its encoded sampling path.
#include <stdafx.h>
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include "generated/display_sampling_vert_dxil.h"
#include "../../src/AltirraSDL/source/display/shaders/generated/sdlgpu_screenfx_frag_dxil.h"
static constexpr auto kFormat = SDL_GPU_SHADERFORMAT_DXIL;
static const auto *kFragment = kATSDLGPU_ScreenFX_DXIL;
static constexpr auto kFragmentSize = kATSDLGPU_ScreenFX_DXIL_len;
#elif defined(__APPLE__)
#include "generated/display_sampling_vert_msl.h"
#include "../../src/AltirraSDL/source/display/shaders/generated/sdlgpu_screenfx_frag_msl.h"
static constexpr auto kFormat = SDL_GPU_SHADERFORMAT_MSL;
static const auto *kFragment = kATSDLGPU_ScreenFX_MSL;
static constexpr auto kFragmentSize = kATSDLGPU_ScreenFX_MSL_len;
#else
#include "generated/display_sampling_vert_spv.h"
#include "../../src/AltirraSDL/source/display/shaders/generated/sdlgpu_screenfx_frag_spv.h"
static constexpr auto kFormat = SDL_GPU_SHADERFORMAT_SPIRV;
static const auto *kFragment = kATSDLGPU_ScreenFX_SPIRV;
static constexpr auto kFragmentSize = kATSDLGPU_ScreenFX_SPIRV_len;
#endif

static void Check(bool condition, const char *message) {
	if (!condition) {
		std::fprintf(stderr, "%s: %s\n", message, SDL_GetError());
		std::exit(1);
	}
}

int main(int argc, char **argv) {
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		std::fprintf(stderr, "GPU sampling unavailable: %s\n", SDL_GetError());
		return 77;
	}
	SDL_GPUDevice *device = SDL_CreateGPUDevice(kFormat, false, nullptr);
	if (!device) {
		std::fprintf(stderr, "GPU sampling unavailable: %s\n", SDL_GetError());
		SDL_Quit();
		return 77;
	}
	SDL_GPUShaderCreateInfo shaderInfo {};
	shaderInfo.format = kFormat;
	shaderInfo.entrypoint = kFormat == SDL_GPU_SHADERFORMAT_MSL ? "main0" : "main";
	shaderInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
	shaderInfo.code = kATSamplingVertex;
	shaderInfo.code_size = kATSamplingVertex_len;
	auto *vertex = SDL_CreateGPUShader(device, &shaderInfo);
	Check(vertex != nullptr, "Create vertex shader");
	shaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	// An optional compiled fragment lets a reviewer reproduce historical
	// failures against the same GPU setup without changing generated assets.
	size_t externalSize = 0;
	void *externalFragment = argc > 1 ? SDL_LoadFile(argv[1], &externalSize) : nullptr;
	Check(argc <= 1 || externalFragment, "Load comparison fragment shader");
	shaderInfo.code = externalFragment ? (const unsigned char *)externalFragment : kFragment;
	shaderInfo.code_size = externalFragment ? externalSize : kFragmentSize;
	shaderInfo.num_samplers = 1;
	shaderInfo.num_uniform_buffers = 1;
	auto *fragment = SDL_CreateGPUShader(device, &shaderInfo);
	SDL_free(externalFragment);
	Check(fragment != nullptr, "Create production fragment shader");

	SDL_GPUColorTargetDescription colorDescription {};
	colorDescription.format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo {};
	pipelineInfo.vertex_shader = vertex;
	pipelineInfo.fragment_shader = fragment;
	pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pipelineInfo.rasterizer_state.enable_depth_clip = true;
	pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	pipelineInfo.target_info.color_target_descriptions = &colorDescription;
	pipelineInfo.target_info.num_color_targets = 1;
	auto *pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);
	Check(pipeline != nullptr, "Create sampling pipeline");

	SDL_GPUTextureCreateInfo textureInfo {};
	textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
	textureInfo.format = colorDescription.format;
	textureInfo.width = textureInfo.height = 1;
	textureInfo.layer_count_or_depth = 1;
	textureInfo.num_levels = 1;
	textureInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	auto *target = SDL_CreateGPUTexture(device, &textureInfo);
	Check(target != nullptr, "Create float sampling target");

	SDL_GPUTransferBufferCreateInfo transferInfo {};
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transferInfo.size = 16;
	auto *upload = SDL_CreateGPUTransferBuffer(device, &transferInfo);
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	auto *download = SDL_CreateGPUTransferBuffer(device, &transferInfo);
	Check(upload && download, "Create sampling transfer buffers");
	const Uint32 sourcePixels[4] = {0xff000000, 0xffffffff,
		0xff000000, 0xffffffff};
	void *mapped = SDL_MapGPUTransferBuffer(device, upload, false);
	Check(mapped != nullptr, "Map sampling upload");
	std::memcpy(mapped, sourcePixels, sizeof sourcePixels);
	SDL_UnmapGPUTransferBuffer(device, upload);

	struct Case {
		const char *name;
		bool srgb;
		bool linear;
		bool hdr;
		float filter, pass, scale, expected;
	};
	// Physical output white is independent of interpolation. A 50% encoded
	// midpoint must linearize to 0.214041, not hardware-linear 0.5.
	const float midpoint = 0.21404114f;
	const Case cases[] = {
		{"SDR bilinear unchanged", false, true, false, 1, 0, 0, 0.5f},
		{"linear output with HDR disabled", true, true, false, 1, 0, 2.5f, midpoint * 2.5f},
		{"HDR bilinear", true, true, true, 1, 0, 5, midpoint * 5},
		{"HDR sharp bilinear", true, true, true, 4, 0, 5, midpoint * 5},
		{"PAL encoded intermediate", true, true, true, 1, 1, 0, 0.5f},
		{"bicubic encoded intermediate", true, false, true, 2, 8, 0, 0.5f},
		{"HDR point sampling", true, false, true, 0, 0, 1, 1},
		{"mask point sampling", true, false, true, 0, 11, 0, 1}
	};
	for (const auto& test : cases) {
		textureInfo.width = textureInfo.height = 2;
		textureInfo.format = test.srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
			: SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		auto *source = SDL_CreateGPUTexture(device, &textureInfo);
		Check(source != nullptr, "Create sampling source");
		SDL_GPUSamplerCreateInfo samplerInfo {};
		samplerInfo.min_filter = samplerInfo.mag_filter = test.linear
			? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
		samplerInfo.address_mode_u = samplerInfo.address_mode_v =
			samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		auto *sampler = SDL_CreateGPUSampler(device, &samplerInfo);
		Check(sampler != nullptr, "Create sampling sampler");

		auto *command = SDL_AcquireGPUCommandBuffer(device);
		Check(command != nullptr, "Acquire sampling commands");
		auto *copy = SDL_BeginGPUCopyPass(command);
		SDL_GPUTextureTransferInfo sourceTransfer {};
		sourceTransfer.transfer_buffer = upload;
		sourceTransfer.pixels_per_row = sourceTransfer.rows_per_layer = 2;
		SDL_GPUTextureRegion sourceRegion {};
		sourceRegion.texture = source;
		sourceRegion.w = sourceRegion.h = 2;
		sourceRegion.d = 1;
		SDL_UploadToGPUTexture(copy, &sourceTransfer, &sourceRegion, false);
		SDL_EndGPUCopyPass(copy);

		SDL_GPUColorTargetInfo targetInfo {};
		targetInfo.texture = target;
		targetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
		targetInfo.store_op = SDL_GPU_STOREOP_STORE;
		auto *render = SDL_BeginGPURenderPass(command, &targetInfo, 1, nullptr);
		SDL_BindGPUGraphicsPipeline(render, pipeline);
		SDL_GPUTextureSamplerBinding binding {source, sampler};
		SDL_BindGPUFragmentSamplers(render, 0, &binding, 1);
		// Layout matches ShaderConstants: sixteen float4 groups.
		float uniforms[16][4] {};
		uniforms[0][0] = uniforms[0][1] = 2;
		uniforms[0][2] = uniforms[0][3] = 1;
		uniforms[1][0] = test.filter;
		uniforms[1][1] = uniforms[1][2] = 2;
		uniforms[2][0] = 1; // identity gamma
		uniforms[10][0] = test.pass;
		uniforms[15][0] = test.scale;
		uniforms[15][1] = test.srgb ? 1.0f : 0.0f;
		uniforms[15][2] = test.hdr ? 1.0f : 0.0f;
		uniforms[15][3] = test.linear ? 1.0f : 0.0f;
		SDL_PushGPUFragmentUniformData(command, 0, uniforms, sizeof uniforms);
		SDL_DrawGPUPrimitives(render, 3, 1, 0, 0);
		SDL_EndGPURenderPass(render);

		copy = SDL_BeginGPUCopyPass(command);
		SDL_GPUTextureRegion targetRegion {};
		targetRegion.texture = target;
		targetRegion.w = targetRegion.h = targetRegion.d = 1;
		SDL_GPUTextureTransferInfo targetTransfer {};
		targetTransfer.transfer_buffer = download;
		SDL_DownloadFromGPUTexture(copy, &targetRegion, &targetTransfer);
		SDL_EndGPUCopyPass(copy);
		auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
		Check(fence && SDL_WaitForGPUFences(device, true, &fence, 1),
			"Wait for sampling result");
		const float *pixel = (const float *)SDL_MapGPUTransferBuffer(device,
			download, false);
		Check(pixel != nullptr, "Map sampling result");
		for (int component = 0; component < 3; ++component) {
			if (!std::isfinite(pixel[component])
				|| std::fabs(pixel[component] - test.expected) > 0.003f) {
				std::fprintf(stderr, "%s: got %.6f, expected %.6f\n",
					test.name, pixel[component], test.expected);
				return 1;
			}
		}
		SDL_UnmapGPUTransferBuffer(device, download);
		SDL_ReleaseGPUFence(device, fence);
		SDL_ReleaseGPUSampler(device, sampler);
		SDL_ReleaseGPUTexture(device, source);
	}
	std::printf("GPU sampling passed on %s (%zu cases)\n",
		SDL_GetGPUDeviceDriver(device), sizeof cases / sizeof cases[0]);
	SDL_ReleaseGPUTransferBuffer(device, upload);
	SDL_ReleaseGPUTransferBuffer(device, download);
	SDL_ReleaseGPUTexture(device, target);
	SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
	SDL_ReleaseGPUShader(device, vertex);
	SDL_ReleaseGPUShader(device, fragment);
	SDL_DestroyGPUDevice(device);
	SDL_Quit();
	return 0;
}
