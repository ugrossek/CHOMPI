/* The few bits of libDaisy that SING's audio engine (DSPEngine.h) uses, for
 * running it on a PC (engine_sim.cpp). Put this directory first on the
 * include path so it stands in for the real daisy.h. */
#pragma once
#include <chrono>
#include <cstdint>
#include "daisysp.h"
#include "util/FIFO.h" // libDaisy's own, header-only

using namespace daisysp;

inline float   s162f(int32_t x) { return float(x) * (1.f / 32768.f); }
inline int32_t f2s16(float x)
{
    x = x <= -1.f ? -1.f : x >= 1.f ? 1.f : x;
    return int32_t(x * 32767.f);
}

namespace daisy
{
/** host stand-in: measures the wall-clock time of each block against the
 *  block's real-time budget, so "load" is the PC's, not the Cortex-M7's */
class CpuLoadMeter
{
  public:
    void Init(float sr, int block)
    {
        budget_us_ = 1e6 * block / sr;
        Reset();
    }
    void OnBlockStart() { t0_ = std::chrono::steady_clock::now(); }
    void OnBlockEnd()
    {
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0_).count();
        const float  load = float(us / budget_us_);
        avg_ = n_ == 0 ? load : avg_ + (load - avg_) * .001f;
        max_ = load > max_ ? load : max_;
        n_++;
    }
    float GetAvgCpuLoad() const { return avg_; }
    float GetMaxCpuLoad() const { return max_; }
    float GetMinCpuLoad() const { return 0.f; }
    void  Reset() { avg_ = max_ = 0.f; n_ = 0; }

  private:
    double budget_us_ = 1000.;
    float  avg_ = 0.f, max_ = 0.f;
    long   n_ = 0;
    std::chrono::steady_clock::time_point t0_;
};
} // namespace daisy
