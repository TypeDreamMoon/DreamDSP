#pragma once

#include <QString>

struct IAudioMeterInformation;

namespace dreamdsp {

// Output level for one render endpoint, via IAudioMeterInformation.
//
// Two things to know about the reading:
//   * it is the level *after* APO processing, so it reflects what the
//     equalizer is doing;
//   * a device held in exclusive mode always reports 0.0, which is
//     indistinguishable from silence -- hence `valid()`.
class PeakMeter
{
public:
    PeakMeter() = default;
    ~PeakMeter();
    PeakMeter(const PeakMeter &) = delete;
    PeakMeter &operator=(const PeakMeter &) = delete;

    // Empty id attaches to the current default render endpoint.
    bool attach(const QString &endpointId);
    void detach();
    bool valid() const { return m_meter != nullptr; }
    QString endpointId() const { return m_endpointId; }

    // 0.0 .. 1.0, or -1.0 when unavailable.
    float peak() const;

private:
    IAudioMeterInformation *m_meter = nullptr;
    QString m_endpointId;
};

} // namespace dreamdsp
