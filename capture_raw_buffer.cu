#include <stdint.h>

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
