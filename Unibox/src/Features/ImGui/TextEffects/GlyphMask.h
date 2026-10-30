#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace TextEffects
{
    struct Settings
    {
        float expansion = 0.f, strength = 0.f, radius = 4.f;
        static Settings Sanitize(float expansion, float strength, float radius)
        {
            return { std::isfinite(expansion) ? std::clamp(expansion, 0.f, 2.f) : 0.f,
                std::isfinite(strength) ? std::clamp(strength, 0.f, 200.f) : 0.f,
                std::isfinite(radius) ? std::clamp(radius, 0.f, 12.f) : 0.f };
        }
        bool Active() const { return expansion > 0.f || (strength > 0.f && radius > 0.f); }
        bool operator==(const Settings& b) const { return expansion == b.expansion && strength == b.strength && radius == b.radius; }
        bool operator!=(const Settings& b) const { return !(*this == b); }
        unsigned Pack() const
        {
            return unsigned(std::lround(expansion * 20.f))
                | (unsigned(std::lround(strength)) << 6) | (unsigned(std::lround(radius * 4.f)) << 14);
        }
        static Settings Unpack(unsigned flags)
        {
            return Sanitize((flags & 63) / 20.f, float((flags >> 6) & 255), ((flags >> 14) & 63) / 4.f);
        }
    };

    // Scratch storage is reused on glyph-cache misses; rendering cached text
    // never runs these filters and does not allocate any effect geometry.
    struct Workspace
    {
        static constexpr int Side = 256, Pixels = Side * Side, KernelRadius = 64;
        std::array<float, Pixels> source{}, fill{}, temp{}, blurred{};
        std::array<unsigned char, Pixels> output{};
        std::array<float, KernelRadius * 2 + 1> kernel{};

        void Dilate(const float* input, float* result, int w, int h, float radius, bool horizontal)
        {
            const int r = int(std::ceil(radius));
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    float value = input[y * w + x];
                    for (int d = -r; d <= r; ++d)
                    {
                        const int sx = x + (horizontal ? d : 0), sy = y + (horizontal ? 0 : d);
                        if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
                        const float coverage = std::clamp(radius + 1.f - std::abs(float(d)), 0.f, 1.f);
                        value = std::max(value, input[sy * w + sx] * coverage);
                    }
                    result[y * w + x] = value;
                }
        }
        void Blur(const float* input, float* result, int w, int h, float radius, bool horizontal)
        {
            const int r = std::clamp(int(std::ceil(radius)), 1, KernelRadius);
            const float sigma = std::max(radius / 3.f, .35f);
            float sum = 0.f;
            for (int d = -r; d <= r; ++d)
                sum += kernel[d + r] = std::exp(-float(d * d) / (2.f * sigma * sigma));
            for (int d = 0; d <= r * 2; ++d) kernel[d] /= sum;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    float value = 0.f;
                    for (int d = -r; d <= r; ++d)
                    {
                        const int sx = x + (horizontal ? d : 0), sy = y + (horizontal ? 0 : d);
                        if (sx >= 0 && sx < w && sy >= 0 && sy < h)
                            value += input[sy * w + sx] * kernel[d + r];
                    }
                    result[y * w + x] = value;
                }
        }
        // Input is a padded alpha mask in source[]. Pixel scales account for
        // atlas oversampling without stretching the glyph's layout metrics.
        bool Apply(int w, int h, Settings s, float scaleX, float scaleY)
        {
            if (w <= 0 || h <= 0 || w > Side || h > Side || !std::isfinite(scaleX) || !std::isfinite(scaleY)
                || scaleX <= 0.f || scaleY <= 0.f || scaleX > 8.f || scaleY > 8.f) return false;
            s = Settings::Sanitize(s.expansion, s.strength, s.radius);
            if (s.expansion > 0.f)
            {
                Dilate(source.data(), temp.data(), w, h, s.expansion * scaleX, true);
                Dilate(temp.data(), fill.data(), w, h, s.expansion * scaleY, false);
            }
            else std::copy_n(source.begin(), w * h, fill.begin());
            if (s.strength > 0.f && s.radius > 0.f)
            {
                Blur(fill.data(), temp.data(), w, h, s.radius * scaleX, true);
                Blur(temp.data(), blurred.data(), w, h, s.radius * scaleY, false);
                for (int i = 0; i < w * h; ++i)
                {
                    const float halo = std::clamp(blurred[i] * s.strength / 100.f, 0.f, 1.f);
                    output[i] = static_cast<unsigned char>(std::lround(std::clamp(fill[i] + halo * (1.f - fill[i]), 0.f, 1.f) * 255.f));
                }
            }
            else
                for (int i = 0; i < w * h; ++i)
                    output[i] = static_cast<unsigned char>(std::lround(std::clamp(fill[i], 0.f, 1.f) * 255.f));
            return true;
        }
    };
}
