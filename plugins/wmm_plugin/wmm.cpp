#include "wmm.hpp"
#include <cmath>

namespace {
// One row of the WMM .COF file: degree n, order m, main-field g/h (nT), and
// secular variation gdot/hdot (nT/yr).
struct CofLine { int n, m; double g, h, gdot, hdot; };

// WMM2025 coefficients (epoch 2025.0, valid 2025.0–2030.0), verbatim from the
// official WMM2025.COF.
const CofLine kWmm2025[] = {
    { 1, 0, -29351.8,     0.0,  12.0,   0.0},
    { 1, 1,  -1410.8,  4545.4,   9.7, -21.5},
    { 2, 0,  -2556.6,     0.0, -11.6,   0.0},
    { 2, 1,   2951.1, -3133.6,  -5.2, -27.7},
    { 2, 2,   1649.3,  -815.1,  -8.0, -12.1},
    { 3, 0,   1361.0,     0.0,  -1.3,   0.0},
    { 3, 1,  -2404.1,   -56.6,  -4.2,   4.0},
    { 3, 2,   1243.8,   237.5,   0.4,  -0.3},
    { 3, 3,    453.6,  -549.5, -15.6,  -4.1},
    { 4, 0,    895.0,     0.0,  -1.6,   0.0},
    { 4, 1,    799.5,   278.6,  -2.4,  -1.1},
    { 4, 2,     55.7,  -133.9,  -6.0,   4.1},
    { 4, 3,   -281.1,   212.0,   5.6,   1.6},
    { 4, 4,     12.1,  -375.6,  -7.0,  -4.4},
    { 5, 0,   -233.2,     0.0,   0.6,   0.0},
    { 5, 1,    368.9,    45.4,   1.4,  -0.5},
    { 5, 2,    187.2,   220.2,   0.0,   2.2},
    { 5, 3,   -138.7,  -122.9,   0.6,   0.4},
    { 5, 4,   -142.0,    43.0,   2.2,   1.7},
    { 5, 5,     20.9,   106.1,   0.9,   1.9},
    { 6, 0,     64.4,     0.0,  -0.2,   0.0},
    { 6, 1,     63.8,   -18.4,  -0.4,   0.3},
    { 6, 2,     76.9,    16.8,   0.9,  -1.6},
    { 6, 3,   -115.7,    48.8,   1.2,  -0.4},
    { 6, 4,    -40.9,   -59.8,  -0.9,   0.9},
    { 6, 5,     14.9,    10.9,   0.3,   0.7},
    { 6, 6,    -60.7,    72.7,   0.9,   0.9},
    { 7, 0,     79.5,     0.0,  -0.0,   0.0},
    { 7, 1,    -77.0,   -48.9,  -0.1,   0.6},
    { 7, 2,     -8.8,   -14.4,  -0.1,   0.5},
    { 7, 3,     59.3,    -1.0,   0.5,  -0.8},
    { 7, 4,     15.8,    23.4,  -0.1,   0.0},
    { 7, 5,      2.5,    -7.4,  -0.8,  -1.0},
    { 7, 6,    -11.1,   -25.1,  -0.8,   0.6},
    { 7, 7,     14.2,    -2.3,   0.8,  -0.2},
    { 8, 0,     23.2,     0.0,  -0.1,   0.0},
    { 8, 1,     10.8,     7.1,   0.2,  -0.2},
    { 8, 2,    -17.5,   -12.6,   0.0,   0.5},
    { 8, 3,      2.0,    11.4,   0.5,  -0.4},
    { 8, 4,    -21.7,    -9.7,  -0.1,   0.4},
    { 8, 5,     16.9,    12.7,   0.3,  -0.5},
    { 8, 6,     15.0,     0.7,   0.2,  -0.6},
    { 8, 7,    -16.8,    -5.2,  -0.0,   0.3},
    { 8, 8,      0.9,     3.9,   0.2,   0.2},
    { 9, 0,      4.6,     0.0,  -0.0,   0.0},
    { 9, 1,      7.8,   -24.8,  -0.1,  -0.3},
    { 9, 2,      3.0,    12.2,   0.1,   0.3},
    { 9, 3,     -0.2,     8.3,   0.3,  -0.3},
    { 9, 4,     -2.5,    -3.3,  -0.3,   0.3},
    { 9, 5,    -13.1,    -5.2,   0.0,   0.2},
    { 9, 6,      2.4,     7.2,   0.3,  -0.1},
    { 9, 7,      8.6,    -0.6,  -0.1,  -0.2},
    { 9, 8,     -8.7,     0.8,   0.1,   0.4},
    { 9, 9,    -12.9,    10.0,  -0.1,   0.1},
    {10, 0,     -1.3,     0.0,   0.1,   0.0},
    {10, 1,     -6.4,     3.3,   0.0,   0.0},
    {10, 2,      0.2,     0.0,   0.1,  -0.0},
    {10, 3,      2.0,     2.4,   0.1,  -0.2},
    {10, 4,     -1.0,     5.3,  -0.0,   0.1},
    {10, 5,     -0.6,    -9.1,  -0.3,  -0.1},
    {10, 6,     -0.9,     0.4,   0.0,   0.1},
    {10, 7,      1.5,    -4.2,  -0.1,   0.0},
    {10, 8,      0.9,    -3.8,  -0.1,  -0.1},
    {10, 9,     -2.7,     0.9,  -0.0,   0.2},
    {10,10,     -3.9,    -9.1,  -0.0,  -0.0},
    {11, 0,      2.9,     0.0,   0.0,   0.0},
    {11, 1,     -1.5,     0.0,  -0.0,  -0.0},
    {11, 2,     -2.5,     2.9,   0.0,   0.1},
    {11, 3,      2.4,    -0.6,   0.0,  -0.0},
    {11, 4,     -0.6,     0.2,   0.0,   0.1},
    {11, 5,     -0.1,     0.5,  -0.1,  -0.0},
    {11, 6,     -0.6,    -0.3,   0.0,  -0.0},
    {11, 7,     -0.1,    -1.2,  -0.0,   0.1},
    {11, 8,      1.1,    -1.7,  -0.1,  -0.0},
    {11, 9,     -1.0,    -2.9,  -0.1,   0.0},
    {11,10,     -0.2,    -1.8,  -0.1,   0.0},
    {11,11,      2.6,    -2.3,  -0.1,   0.0},
    {12, 0,     -2.0,     0.0,   0.0,   0.0},
    {12, 1,     -0.2,    -1.3,   0.0,  -0.0},
    {12, 2,      0.3,     0.7,  -0.0,   0.0},
    {12, 3,      1.2,     1.0,  -0.0,  -0.1},
    {12, 4,     -1.3,    -1.4,  -0.0,   0.1},
    {12, 5,      0.6,    -0.0,  -0.0,  -0.0},
    {12, 6,      0.6,     0.6,   0.1,  -0.0},
    {12, 7,      0.5,    -0.1,  -0.0,  -0.0},
    {12, 8,     -0.1,     0.8,   0.0,   0.0},
    {12, 9,     -0.4,     0.1,   0.0,  -0.0},
    {12,10,     -0.2,    -1.0,  -0.1,  -0.0},
    {12,11,     -1.3,     0.1,  -0.0,   0.0},
    {12,12,     -0.7,     0.2,  -0.1,  -0.1},
};
}  // namespace

WmmModel::WmmModel() {
    for (int i = 0; i < kSize; ++i) {
        fn_[i] = fm_[i] = 0.0;
        for (int j = 0; j < kSize; ++j) { c_[i][j] = cd_[i][j] = k_[i][j] = 0.0; }
    }

    double snorm[kSize * kSize];
    for (int i = 0; i < kSize * kSize; ++i) snorm[i] = 0.0;
    snorm[0] = 1.0;

    // Load raw coefficients: g(n,m) -> c_[m][n], h(n,m) -> c_[n][m-1].
    for (const CofLine& L : kWmm2025) {
        if (L.m > kMaxOrd || L.m > L.n) continue;
        c_[L.m][L.n]  = L.g;
        cd_[L.m][L.n] = L.gdot;
        if (L.m != 0) {
            c_[L.n][L.m - 1]  = L.h;
            cd_[L.n][L.m - 1] = L.hdot;
        }
    }

    // Convert Schmidt semi-normalized to unnormalized Gauss coefficients and
    // precompute the Legendre recursion factors (canonical geomag setup).
    fm_[0] = 0.0;
    for (int n = 1; n <= kMaxOrd; ++n) {
        snorm[n] = snorm[n - 1] * double(2 * n - 1) / double(n);
        int j = 2;
        for (int m = 0; m <= n; ++m) {
            k_[m][n] = double((n - 1) * (n - 1) - m * m)
                       / double((2 * n - 1) * (2 * n - 3));
            if (m > 0) {
                const double flnmj = double((n - m + 1) * j) / double(n + m);
                snorm[n + m * kSize] = snorm[n + (m - 1) * kSize] * std::sqrt(flnmj);
                j = 1;
                c_[n][m - 1]  *= snorm[n + m * kSize];
                cd_[n][m - 1] *= snorm[n + m * kSize];
            }
            c_[m][n]  *= snorm[n + m * kSize];
            cd_[m][n] *= snorm[n + m * kSize];
        }
        fn_[n] = double(n + 1);
        fm_[n] = double(n);
    }
    k_[1][1] = 0.0;
}

double WmmModel::declination(double glat, double glon, double altKm, double time) const {
    constexpr double kD2R = 3.14159265358979323846 / 180.0;
    constexpr double kR2D = 180.0 / 3.14159265358979323846;
    // WGS84 + geomagnetic reference radius (km).
    constexpr double a = 6378.137, b = 6356.7523142, re = 6371.2;
    constexpr double a2 = a * a, b2 = b * b, c2 = a2 - b2;
    constexpr double a4 = a2 * a2, b4 = b2 * b2, c4 = a4 - b4;

    const double dt = time - kEpoch;
    const double rlon = glon * kD2R;
    double rlat = glat * kD2R;
    if (glat > 89.9999) rlat = 89.9999 * kD2R;          // avoid the pole singularity
    else if (glat < -89.9999) rlat = -89.9999 * kD2R;

    const double srlon = std::sin(rlon), crlon = std::cos(rlon);
    const double srlat = std::sin(rlat), crlat = std::cos(rlat);
    const double srlat2 = srlat * srlat, crlat2 = crlat * crlat;

    // Geodetic to geocentric.
    const double q = std::sqrt(a2 - c2 * srlat2);
    const double q1 = altKm * q;
    const double qt = (q1 + a2) / (q1 + b2);
    const double q2 = qt * qt;
    const double ct = srlat / std::sqrt(q2 * crlat2 + srlat2);
    const double st = std::sqrt(1.0 - ct * ct);
    const double r2 = altKm * altKm + 2.0 * q1 + (a4 - c4 * srlat2) / (q * q);
    const double r = std::sqrt(r2);
    const double d = std::sqrt(a2 * crlat2 + b2 * srlat2);
    const double ca = (altKm + d) / r;
    const double sa = c2 * crlat * srlat / (r * d);

    // sin/cos of m*longitude.
    double sp[kSize], cp[kSize];
    sp[0] = 0.0; cp[0] = 1.0;
    sp[1] = srlon; cp[1] = crlon;
    for (int m = 2; m <= kMaxOrd; ++m) {
        sp[m] = sp[1] * cp[m - 1] + cp[1] * sp[m - 1];
        cp[m] = cp[1] * cp[m - 1] - sp[1] * sp[m - 1];
    }

    double p[kSize * kSize];
    double dp[kSize][kSize];
    double pp[kSize];
    for (int i = 0; i < kSize * kSize; ++i) p[i] = 0.0;
    for (int i = 0; i < kSize; ++i) { pp[i] = 0.0; for (int j = 0; j < kSize; ++j) dp[i][j] = 0.0; }
    p[0] = 1.0;
    pp[0] = 1.0;

    double aor = re / r;
    double ar = aor * aor;
    double br = 0.0, bt = 0.0, bp = 0.0, bpp = 0.0;

    for (int n = 1; n <= kMaxOrd; ++n) {
        ar *= aor;
        for (int m = 0; m <= n; ++m) {
            // Associated Legendre polynomials and derivatives (recursion).
            if (n == m) {
                p[n + m * kSize] = st * p[(n - 1) + (m - 1) * kSize];
                dp[m][n] = st * dp[m - 1][n - 1] + ct * p[(n - 1) + (m - 1) * kSize];
            } else if (n == 1 && m == 0) {
                p[n + m * kSize] = ct * p[(n - 1) + m * kSize];
                dp[m][n] = ct * dp[m][n - 1] - st * p[(n - 1) + m * kSize];
            } else if (n > 1 && n != m) {
                p[n + m * kSize] = ct * p[(n - 1) + m * kSize] - k_[m][n] * p[(n - 2) + m * kSize];
                dp[m][n] = ct * dp[m][n - 1] - st * p[(n - 1) + m * kSize]
                           - k_[m][n] * dp[m][n - 2];
            }

            // Time-adjusted coefficients.
            const double tcmn = c_[m][n] + dt * cd_[m][n];
            const double tcnm = (m != 0) ? (c_[n][m - 1] + dt * cd_[n][m - 1]) : 0.0;

            // Accumulate the spherical-harmonic expansion.
            const double par = ar * p[n + m * kSize];
            double temp1, temp2;
            if (m == 0) { temp1 = tcmn * cp[m]; temp2 = tcmn * sp[m]; }
            else        { temp1 = tcmn * cp[m] + tcnm * sp[m]; temp2 = tcmn * sp[m] - tcnm * cp[m]; }
            bt -= ar * temp1 * dp[m][n];
            bp += fm_[m] * temp2 * par;
            br += fn_[n] * temp1 * par;

            // Geographic poles: longitudinal component needs L'Hopital handling.
            if (st == 0.0 && m == 1) {
                pp[n] = (n == 1) ? pp[n - 1] : (ct * pp[n - 1] - k_[m][n] * pp[n - 2]);
                bpp += fm_[m] * temp2 * ar * pp[n];
            }
        }
    }
    bp = (st == 0.0) ? bpp : (bp / st);

    // Rotate geocentric -> geodetic; declination = atan2(east, north).
    const double bx = -bt * ca - br * sa;
    const double by = bp;
    return std::atan2(by, bx) * kR2D;
}
