#include "BiquadFilter.h"

#include <cmath>
#include <complex>

namespace dreamdsp::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void BiquadFilter::design(Kind kind, double freqHz, double q, double gainDb, double sampleRate)
{
    if (sampleRate <= 0.0 || freqHz <= 0.0 || q <= 0.0) {
        m_b0 = 1.0f; m_b1 = m_b2 = m_a1 = m_a2 = 0.0f;
        return;
    }

    const double nyquist = sampleRate * 0.5;
    if (freqHz >= nyquist)
        freqHz = nyquist * 0.999;

    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freqHz / sampleRate;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s / (2.0 * q);

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (kind) {
    case Kind::LowPass:
        b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2;
        a0 = 1 + alpha;   a1 = -2 * c; a2 = 1 - alpha;
        break;
    case Kind::HighPass:
        b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2;
        a0 = 1 + alpha;   a1 = -2 * c;   a2 = 1 - alpha;
        break;
    case Kind::LowShelf: {
        const double sq = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * c + sq);
        b1 = 2 * A * ((A - 1) - (A + 1) * c);
        b2 = A * ((A + 1) - (A - 1) * c - sq);
        a0 = (A + 1) + (A - 1) * c + sq;
        a1 = -2 * ((A - 1) + (A + 1) * c);
        a2 = (A + 1) + (A - 1) * c - sq;
        break;
    }
    case Kind::HighShelf: {
        const double sq = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * c + sq);
        b1 = -2 * A * ((A - 1) + (A + 1) * c);
        b2 = A * ((A + 1) + (A - 1) * c - sq);
        a0 = (A + 1) - (A - 1) * c + sq;
        a1 = 2 * ((A - 1) - (A + 1) * c);
        a2 = (A + 1) - (A - 1) * c - sq;
        break;
    }
    case Kind::Peak:
        b0 = 1 + alpha * A; b1 = -2 * c; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * c; a2 = 1 - alpha / A;
        break;
    case Kind::AllPass:
        b0 = 1 - alpha; b1 = -2 * c; b2 = 1 + alpha;
        a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
        break;
    }

    m_b0 = float(b0 / a0);
    m_b1 = float(b1 / a0);
    m_b2 = float(b2 / a0);
    m_a1 = float(a1 / a0);
    m_a2 = float(a2 / a0);
}

double BiquadFilter::magnitude(double freqHz, double sampleRate) const
{
    const double w = 2.0 * kPi * freqHz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = double(m_b0) + double(m_b1) * z1 + double(m_b2) * z2;
    const std::complex<double> den = 1.0 + double(m_a1) * z1 + double(m_a2) * z2;
    return std::abs(num / den);
}

void LinkwitzRiley4::design(double crossoverHz, double sampleRate)
{
    // Two cascaded Butterworth (Q = 1/sqrt2) sections give the -6 dB crossing
    // point and the in-phase summation LR is chosen for.
    constexpr double kButterworthQ = 0.70710678118654752;
    m_lp1.design(BiquadFilter::Kind::LowPass, crossoverHz, kButterworthQ, 0.0, sampleRate);
    m_lp2.design(BiquadFilter::Kind::LowPass, crossoverHz, kButterworthQ, 0.0, sampleRate);
    m_hp1.design(BiquadFilter::Kind::HighPass, crossoverHz, kButterworthQ, 0.0, sampleRate);
    m_hp2.design(BiquadFilter::Kind::HighPass, crossoverHz, kButterworthQ, 0.0, sampleRate);
    reset();
}

void LinkwitzRiley4::reset()
{
    m_lp1.reset(); m_lp2.reset(); m_hp1.reset(); m_hp2.reset();
}

} // namespace dreamdsp::dsp
