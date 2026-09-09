#include <stdint.h>
#include <cuda_runtime.h>
#include <cuda_fp16.h>

extern "C" __global__ void capture_raw_buffer(
    const uint4 *source, uint4 *destination, uint32_t uint4_count) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < uint4_count) {
        destination[index] = source[index];
    }
}

extern "C" __global__ void fill_raw_buffer(
    uint32_t *destination, uint32_t count, uint32_t value) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count) {
        destination[index] = value;
    }
}

extern "C" __global__ void capture_surface_rgba16f(
    cudaSurfaceObject_t source, float4 *destination,
    uint32_t width, uint32_t height, uint32_t origin_x, uint32_t origin_y,
    uint32_t stride_x, uint32_t stride_y) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < width && y < height) {
        const uint32_t source_x = origin_x + x * stride_x;
        const uint32_t source_y = origin_y + y * stride_y;
        const ushort4 packed =
            surf2Dread<ushort4>(source, source_x * sizeof(ushort4), source_y);
        destination[y * width + x] = make_float4(
            __half2float(__ushort_as_half(packed.x)),
            __half2float(__ushort_as_half(packed.y)),
            __half2float(__ushort_as_half(packed.z)),
            __half2float(__ushort_as_half(packed.w)));
    }
}

extern "C" __global__ void capture_texture_rgba(
    cudaTextureObject_t source, float4 *destination,
    uint32_t output_width, uint32_t output_height,
    uint32_t source_width, uint32_t source_height,
    uint32_t origin_x, uint32_t origin_y, uint32_t stride_x, uint32_t stride_y) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < output_width && y < output_height) {
        const float u = (origin_x + x * stride_x + 0.5f) / source_width;
        const float v = (origin_y + y * stride_y + 0.5f) / source_height;
        destination[y * output_width + x] = tex2D<float4>(source, u, v);
    }
}
