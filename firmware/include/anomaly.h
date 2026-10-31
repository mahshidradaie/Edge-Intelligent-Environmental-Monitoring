// anomaly.h - lightweight streaming anomaly detector (no heap, no dependencies).
//
// EWMA mean / variance + z-score test with:
//   * warm-up period (no alerts while the baseline is being learned)
//   * debounce: `raiseCount` consecutive outliers raise an alert,
//               `clearCount` consecutive normal samples clear it
//   * outliers are NOT folded into the baseline, so an incident cannot "teach"
//     the detector that the faulty value is normal.
#pragma once
#include <math.h>
#include <stdint.h>

namespace edgeguard {

struct DetectorConfig {
    float    alpha       = 0.05f;   // EWMA smoothing factor
    float    zThreshold  = 4.0f;    // |z| above this is an outlier
    uint16_t warmup      = 30;      // samples before alerts are allowed
    uint16_t raiseCount  = 3;       // consecutive outliers to raise
    uint16_t clearCount  = 5;       // consecutive normal samples to clear
    float    minStd      = 0.05f;   // floor on sigma (avoids div-by-zero on flat signals)
};

class AnomalyDetector {
public:
    explicit AnomalyDetector(DetectorConfig c = DetectorConfig()) : cfg_(c) {}

    // Feed one sample. Returns true while an alert is active.
    bool update(float x) {
        if (n_ == 0) { mean_ = x; var_ = 0.0f; z_ = 0.0f; n_ = 1; return false; }

        float sd = sqrtf(var_);
        if (sd < cfg_.minStd) sd = cfg_.minStd;
        z_ = (x - mean_) / sd;

        const bool warm    = n_ >= cfg_.warmup;
        const bool outlier = warm && fabsf(z_) > cfg_.zThreshold;

        if (outlier) { sat_inc(out_); norm_ = 0; }
        else         { sat_inc(norm_); out_ = 0; }

        if (!alert_ && out_ >= cfg_.raiseCount)       alert_ = true;
        else if (alert_ && norm_ >= cfg_.clearCount)  alert_ = false;

        if (!outlier) {
            const float d = x - mean_;
            mean_ += cfg_.alpha * d;
            var_   = (1.0f - cfg_.alpha) * (var_ + cfg_.alpha * d * d);
        }
        if (n_ < 65535) ++n_;
        return alert_;
    }

    void  reset()        { n_ = 0; out_ = norm_ = 0; alert_ = false; mean_ = var_ = z_ = 0.0f; }
    float zscore() const { return z_; }
    float mean()   const { return mean_; }
    bool  alert()  const { return alert_; }

private:
    static void sat_inc(uint16_t &v) { if (v < 65535) ++v; }
    DetectorConfig cfg_;
    float    mean_ = 0.0f, var_ = 0.0f, z_ = 0.0f;
    uint16_t n_ = 0, out_ = 0, norm_ = 0;
    bool     alert_ = false;
};

}  // namespace edgeguard
