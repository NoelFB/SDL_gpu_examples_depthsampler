#include <SDL_gpu_examples.h>

static SDL_GpuGraphicsPipeline* MaskerPipeline;
static SDL_GpuGraphicsPipeline* MaskeePipeline;
static SDL_GpuBuffer* VertexBuffer;
static SDL_GpuTexture* DepthStencilTexture;

typedef struct PositionColorVertex
{
	float x, y, z;
	Uint8 r, g, b, a;
} PositionColorVertex;

static int Init(Context* context)
{
	int result = CommonInit(context, 0);
	if (result < 0)
	{
		return result;
	}

	size_t vsCodeSize;
	void* vsBytes = LoadAsset("Content/Shaders/Compiled/PositionColor.vert.spv", &vsCodeSize);
	if (vsBytes == NULL)
	{
		SDL_Log("Could not load vertex shader from disk!");
		return -1;
	}

	SDL_GpuShader* vertexShader = SDL_GpuCreateShader(context->Device, &(SDL_GpuShaderCreateInfo){
		.stage = SDL_GPU_SHADERSTAGE_VERTEX,
		.code = vsBytes,
		.codeSize = vsCodeSize,
		.entryPointName = "vs_main",
		.format = SDL_GPU_SHADERFORMAT_SPIRV,
	});
	if (vertexShader == NULL)
	{
		SDL_Log("Failed to create vertex shader!");
		return -1;
	}

	size_t fsCodeSize;
	void *fsBytes = LoadAsset("Content/Shaders/Compiled/SolidColor.frag.spv", &fsCodeSize);
	if (fsBytes == NULL)
	{
		SDL_Log("Could not load fragment shader from disk!");
		return -1;
	}

	SDL_GpuShader* fragmentShader = SDL_GpuCreateShader(context->Device, &(SDL_GpuShaderCreateInfo){
		.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
		.code = fsBytes,
		.codeSize = fsCodeSize,
		.entryPointName = "fs_main",
		.format = SDL_GPU_SHADERFORMAT_SPIRV
	});
	if (fragmentShader == NULL)
	{
		SDL_Log("Failed to create fragment shader!");
		return -1;
	}

	SDL_GpuGraphicsPipelineCreateInfo pipelineCreateInfo = {
		.attachmentInfo = {
			.colorAttachmentCount = 1,
			.colorAttachmentDescriptions = (SDL_GpuColorAttachmentDescription[]){{
				.format = SDL_GpuGetSwapchainFormat(context->Device, context->Window),
				.blendState = {
					.blendEnable = SDL_TRUE,
					.alphaBlendOp = SDL_GPU_BLENDOP_ADD,
					.colorBlendOp = SDL_GPU_BLENDOP_ADD,
					.colorWriteMask = 0xF,
					.srcColorBlendFactor = SDL_BLENDFACTOR_ONE,
					.srcAlphaBlendFactor = SDL_BLENDFACTOR_ONE,
					.dstColorBlendFactor = SDL_BLENDFACTOR_ZERO,
					.dstAlphaBlendFactor = SDL_BLENDFACTOR_ZERO
				}
			}},
			.hasDepthStencilAttachment = SDL_TRUE,
			.depthStencilFormat = SDL_GPU_TEXTUREFORMAT_D32_SFLOAT_S8_UINT
		},
		.depthStencilState = (SDL_GpuDepthStencilState){
			.stencilTestEnable = SDL_TRUE,
			.frontStencilState = (SDL_GpuStencilOpState){
				.compareOp = SDL_GPU_COMPAREOP_NEVER,
				.failOp = SDL_GPU_STENCILOP_REPLACE
			},
			.reference = 1,
			.writeMask = 0xFF
		},
		.rasterizerState = (SDL_GpuRasterizerState){
			.cullMode = SDL_GPU_CULLMODE_NONE,
			.fillMode = SDL_GPU_FILLMODE_FILL,
			.frontFace = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE
		},
		.vertexInputState = (SDL_GpuVertexInputState){
			.vertexBindingCount = 1,
			.vertexBindings = (SDL_GpuVertexBinding[]){{
				.binding = 0,
				.inputRate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
				.stepRate = 0,
				.stride = sizeof(PositionColorVertex)
			}},
			.vertexAttributeCount = 2,
			.vertexAttributes = (SDL_GpuVertexAttribute[]){{
				.binding = 0,
				.format = SDL_GPU_VERTEXELEMENTFORMAT_VECTOR3,
				.location = 0,
				.offset = 0
			}, {
				.binding = 0,
				.format = SDL_GPU_VERTEXELEMENTFORMAT_COLOR,
				.location = 1,
				.offset = sizeof(float) * 3
			}}
		},
		.multisampleState.sampleMask = 0xFFFF,
		.primitiveType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.vertexShader = vertexShader,
		.fragmentShader = fragmentShader
	};

	MaskerPipeline = SDL_GpuCreateGraphicsPipeline(context->Device, &pipelineCreateInfo);
	if (MaskerPipeline == NULL)
	{
		SDL_Log("Could not create masker pipeline!");
		return -1;
	}

	pipelineCreateInfo.depthStencilState = (SDL_GpuDepthStencilState){
		.stencilTestEnable = SDL_TRUE,
		.frontStencilState = (SDL_GpuStencilOpState){
			.compareOp = SDL_GPU_COMPAREOP_EQUAL
		},
		.reference = 0,
		.compareMask = 0xFF,
		.writeMask = 0
	};

	MaskeePipeline = SDL_GpuCreateGraphicsPipeline(context->Device, &pipelineCreateInfo);
	if (MaskeePipeline == NULL)
	{
		SDL_Log("Could not create maskee pipeline!");
		return -1;
	}

	SDL_GpuQueueDestroyShader(context->Device, vertexShader);
	SDL_GpuQueueDestroyShader(context->Device, fragmentShader);
	SDL_free(vsBytes);
	SDL_free(fsBytes);

	VertexBuffer = SDL_GpuCreateGpuBuffer(
		context->Device,
		SDL_GPU_BUFFERUSAGE_VERTEX_BIT,
		sizeof(PositionColorVertex) * 6
	);

	int w, h;
	SDL_GetWindowSizeInPixels(context->Window, &w, &h);

	DepthStencilTexture = SDL_GpuCreateTexture(
		context->Device,
		&(SDL_GpuTextureCreateInfo) {
			.width = w,
			.height = h,
			.depth = 1,
			.layerCount = 1,
			.levelCount = 1,
			.sampleCount = SDL_GPU_SAMPLECOUNT_1,
			.format = SDL_GPU_TEXTUREFORMAT_D32_SFLOAT_S8_UINT,
			.usageFlags = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT
		}
	);

	SDL_GpuTransferBuffer* transferBuffer = SDL_GpuCreateTransferBuffer(
		context->Device,
		SDL_GPU_TRANSFERUSAGE_BUFFER,
		SDL_GPU_TRANSFER_MAP_WRITE,
		sizeof(PositionColorVertex) * 6
	);

	PositionColorVertex* transferData;

	SDL_GpuMapTransferBuffer(
		context->Device,
		transferBuffer,
		SDL_FALSE,
		(void**) &transferData
	);

	transferData[0] = (PositionColorVertex) { -0.5f, -0.5f, 0, 255, 255,   0, 255 };
	transferData[1] = (PositionColorVertex) {  0.5f, -0.5f, 0, 255, 255,   0, 255 };
	transferData[2] = (PositionColorVertex) {     0,  0.5f, 0, 255, 255,   0, 255 };
	transferData[3] = (PositionColorVertex) {    -1,    -1, 0, 255,   0,   0, 255 };
	transferData[4] = (PositionColorVertex) {     1,    -1, 0,   0, 255,   0, 255 };
	transferData[5] = (PositionColorVertex) {     0,     1, 0,   0,   0, 255, 255 };

	SDL_GpuUnmapTransferBuffer(context->Device, transferBuffer);

	SDL_GpuCommandBuffer* uploadCmdBuf = SDL_GpuAcquireCommandBuffer(context->Device);
	SDL_GpuCopyPass* copyPass = SDL_GpuBeginCopyPass(uploadCmdBuf);

	SDL_GpuUploadToBuffer(
		copyPass,
		transferBuffer,
		VertexBuffer,
		&(SDL_GpuBufferCopy) {
			.srcOffset = 0,
			.dstOffset = 0,
			.size = sizeof(PositionColorVertex) * 6
		},
		SDL_FALSE
	);

	SDL_GpuEndCopyPass(copyPass);
	SDL_GpuSubmit(uploadCmdBuf);
	SDL_GpuQueueDestroyTransferBuffer(context->Device, transferBuffer);

	return 0;
}

static int Update(Context* context)
{
	return 0;
}

static int Draw(Context* context)
{
	SDL_GpuCommandBuffer* cmdbuf = SDL_GpuAcquireCommandBuffer(context->Device);
	if (cmdbuf == NULL)
	{
		SDL_Log("GpuAcquireCommandBuffer failed");
		return -1;
	}

	Uint32 w, h;
	SDL_GpuTexture* swapchainTexture = SDL_GpuAcquireSwapchainTexture(cmdbuf, context->Window, &w, &h);
	if (swapchainTexture != NULL)
	{
		SDL_GpuColorAttachmentInfo colorAttachmentInfo = { 0 };
		colorAttachmentInfo.textureSlice.texture = swapchainTexture;
		colorAttachmentInfo.clearColor = (SDL_GpuColor){ 0.0f, 0.0f, 0.0f, 1.0f };
		colorAttachmentInfo.loadOp = SDL_GPU_LOADOP_CLEAR;
		colorAttachmentInfo.storeOp = SDL_GPU_STOREOP_STORE;

		SDL_GpuDepthStencilAttachmentInfo depthStencilAttachmentInfo = { 0 };
		depthStencilAttachmentInfo.textureSlice.texture = DepthStencilTexture;
		depthStencilAttachmentInfo.cycle = SDL_TRUE;
		depthStencilAttachmentInfo.depthStencilClearValue.depth = 0;
		depthStencilAttachmentInfo.depthStencilClearValue.stencil = 0;
		depthStencilAttachmentInfo.loadOp = SDL_GPU_LOADOP_CLEAR;
		depthStencilAttachmentInfo.storeOp = SDL_GPU_STOREOP_DONT_CARE;
		depthStencilAttachmentInfo.stencilStoreOp = SDL_GPU_STOREOP_DONT_CARE;

		SDL_GpuRenderPass* renderPass = SDL_GpuBeginRenderPass(
			cmdbuf,
			&colorAttachmentInfo,
			1,
			&depthStencilAttachmentInfo
		);

		SDL_GpuBindGraphicsPipeline(renderPass, MaskerPipeline);
		SDL_GpuBindVertexBuffers(renderPass, 0, 1, &(SDL_GpuBufferBinding){ .gpuBuffer = VertexBuffer, .offset = 0 });
		SDL_GpuDrawPrimitives(renderPass, 0, 1);
		SDL_GpuBindGraphicsPipeline(renderPass, MaskeePipeline);
		SDL_GpuDrawPrimitives(renderPass, 3, 1);

		SDL_GpuEndRenderPass(renderPass);
	}

	SDL_GpuSubmit(cmdbuf);

	return 0;
}

static void Quit(Context* context)
{
	SDL_GpuQueueDestroyGraphicsPipeline(context->Device, MaskeePipeline);
	SDL_GpuQueueDestroyGraphicsPipeline(context->Device, MaskerPipeline);

	SDL_GpuQueueDestroyTexture(context->Device, DepthStencilTexture);
	SDL_GpuQueueDestroyGpuBuffer(context->Device, VertexBuffer);

	CommonQuit(context);
}

Example BasicStencil_Example = { "BasicStencil", Init, Update, Draw, Quit };