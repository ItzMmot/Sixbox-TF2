#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace LocalExtension
{
    bool ModelTransportReady();
    std::vector<unsigned char> AcquireModelPixels(size_t bytes);
    void PublishModelPixels(std::vector<unsigned char>&& pixels, unsigned width, unsigned height, uint64_t capturedAt);
    void ModelCaptureError(bool failed);
}
