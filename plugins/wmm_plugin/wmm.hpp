#pragma once

// World Magnetic Model (WMM2025) declination calculator.
//
// Computes magnetic declination (variation) via the standard spherical-harmonic
// synthesis of the WMM2025 main-field + secular-variation Gauss coefficients
// (degree/order 12), valid 2025.0–2030.0. The algorithm is the canonical NOAA
// geomag routine (geodetic→geocentric, Schmidt-normalized associated Legendre
// recursion, field-component summation); coefficients are the official WMM2025
// set. Ported from the pygeomag reference implementation (validated against the
// official WMM2025 test values).
//
// declination() is const and uses only local scratch state, so it is reentrant.
class WmmModel {
public:
    WmmModel();

    // Magnetic declination in degrees, EAST positive (the convention the nav
    // store uses: true = magnetic + variation). `latDeg`/`lonDeg` are geodetic
    // (WGS84), `altKm` is height above the ellipsoid in km (0 at sea level),
    // `decimalYear` e.g. 2026.45.
    double declination(double latDeg, double lonDeg, double altKm, double decimalYear) const;

private:
    static constexpr int kMaxOrd = 12;
    static constexpr int kSize   = 13;   // kMaxOrd + 1
    static constexpr double kEpoch = 2025.0;

    // Schmidt-normalized (unnormalized Gauss) coefficients and recursion factors,
    // built once in the constructor. g(n,m) lives at c_[m][n]; h(n,m) at c_[n][m-1].
    double c_[kSize][kSize];
    double cd_[kSize][kSize];
    double k_[kSize][kSize];
    double fn_[kSize];
    double fm_[kSize];
};
