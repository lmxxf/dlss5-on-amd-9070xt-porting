#include <stdint.h>

extern "C" __global__ void capture_raw_buffer(
    const uint4 *source, uint4 *destination, uint32_t uint4_count) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < uint4_count) {
        destination[index] = source[index];
    }
}
