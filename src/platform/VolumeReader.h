#pragma once

#include <QString>

struct IAudioEndpointVolume;

namespace dreamdsp {

// The master volume of one render endpoint, in dB of attenuation.
//
// Named VolumeReader rather than EndpointVolume for a dull but load-bearing
// reason: the Windows SDK header this needs is <endpointvolume.h>, the include
// path contains src/platform, and Windows filesystems are case-insensitive --
// so a header called EndpointVolume.h here silently shadows the SDK's and the
// interface it declares never gets defined.
//
// This exists so that the loudness correction can be a pure function of its
// parameters. Equalizer APO reads the volume from a thread inside audiodg.exe,
// which means COM and a polling thread inside a system audio process; here the
// interface reads it -- it already holds an MMDevice for the meter -- and puts
// the number on the wire with everything else.
//
// The scale is the device's own: 0 dB at full volume, negative below it, with a
// floor that varies by hardware (-60 and -96 are both common). That is the
// scale Equalizer APO's formula was written against, which is why the reading
// is not normalised to something tidier.
class VolumeReader
{
public:
    VolumeReader() = default;
    ~VolumeReader();
    VolumeReader(const VolumeReader &) = delete;
    VolumeReader &operator=(const VolumeReader &) = delete;

    // Empty id attaches to the current default render endpoint.
    bool attach(const QString &endpointId);
    void detach();
    bool valid() const { return m_volume != nullptr; }
    QString endpointId() const { return m_endpointId; }

    // Attenuation in dB, 0 at full volume. Returns 1.0 when unavailable --
    // above the legal range, so it cannot be mistaken for a real reading.
    float levelDb() const;
    // The bottom of this device's range, for a UI that wants to show it.
    float minDb() const { return m_minDb; }

private:
    IAudioEndpointVolume *m_volume = nullptr;
    QString m_endpointId;
    float m_minDb = -60.0f;
};

} // namespace dreamdsp
