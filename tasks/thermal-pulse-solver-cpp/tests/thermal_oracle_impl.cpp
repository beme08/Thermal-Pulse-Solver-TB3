#include "thermal_oracle.hpp"

#include <cmath>
#include <cstdlib>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTwoPi = 2.0 * kPi;

struct Params {
    double rho = 1.0;
    double amp = 0.75;
    double freq = 96.0;
    double slow = 0.05;
    double shape_b = 0.25;
    double sharp = 36.0;
    double cx = 0.43;
    double cy = 0.58;
    double final_t = 1.0;
};

Params params() {
    Params p;
    const char* raw = std::getenv("THERMAL_INSTANCE");
    const int instance = raw ? std::atoi(raw) : 0;
    if (instance == 1) {
        p.freq = 192.0;
    } else if (instance == 2) {
        p.freq = 128.0;
        p.sharp = 640.0;
    }
    return p;
}

double temporal(const Params& p, double t) {
    return (1.0 + p.slow * t) * (1.0 + p.amp * std::sin(kTwoPi * p.freq * t));
}

double temporal_prime(const Params& p, double t) {
    const double w = kTwoPi * p.freq;
    const double osc = 1.0 + p.amp * std::sin(w * t);
    return p.slow * osc + (1.0 + p.slow * t) * p.amp * w * std::cos(w * t);
}

double kx_prime(double x) {
    return -0.50 * kTwoPi * std::sin(kTwoPi * x);
}

double ky_prime(double y) {
    return 0.35 * kTwoPi * std::cos(kTwoPi * y);
}

void spatial_terms(const Params& p, double x, double y, double& s, double& div_k_grad_s) {
    const double ax = std::sin(kPi * x);
    const double ay = std::sin(kPi * y);
    const double ax1 = kPi * std::cos(kPi * x);
    const double ay1 = kPi * std::cos(kPi * y);
    const double ax2 = -kPi * kPi * ax;
    const double ay2 = -kPi * kPi * ay;

    const double cx2 = std::cos(kTwoPi * x);
    const double sx2 = std::sin(kTwoPi * x);
    const double cy2 = std::cos(kTwoPi * y);
    const double sy2 = std::sin(kTwoPi * y);

    const double c = 1.0 + p.shape_b * cx2 * cy2;
    const double cx = -p.shape_b * kTwoPi * sx2 * cy2;
    const double cy = -p.shape_b * kTwoPi * cx2 * sy2;
    const double cxx = -p.shape_b * kTwoPi * kTwoPi * cx2 * cy2;
    const double cyy = -p.shape_b * kTwoPi * kTwoPi * cx2 * cy2;

    const double dx = x - p.cx;
    const double dy = y - p.cy;
    const double g = std::exp(-p.sharp * (dx * dx + dy * dy));
    const double gx_scale = -2.0 * p.sharp * dx;
    const double gy_scale = -2.0 * p.sharp * dy;
    const double gx = g * gx_scale;
    const double gy = g * gy_scale;
    const double gxx = g * (gx_scale * gx_scale - 2.0 * p.sharp);
    const double gyy = g * (gy_scale * gy_scale - 2.0 * p.sharp);

    s = ax * ay * c * g;

    const double sx = ay * (ax1 * c * g + ax * cx * g + ax * c * gx);
    const double sy = ax * (ay1 * c * g + ay * cy * g + ay * c * gy);

    const double sxx = ay * (
        ax2 * c * g
        + ax * cxx * g
        + ax * c * gxx
        + 2.0 * ax1 * cx * g
        + 2.0 * ax1 * c * gx
        + 2.0 * ax * cx * gx);

    const double syy = ax * (
        ay2 * c * g
        + ay * cyy * g
        + ay * c * gyy
        + 2.0 * ay1 * cy * g
        + 2.0 * ay1 * c * gy
        + 2.0 * ay * cy * gy);

    div_k_grad_s = kx_prime(x) * sx + conductivity_x(x) * sxx
        + ky_prime(y) * sy + conductivity_y(y) * syy;
}

double exact_temp(double x, double y, double t) {
    const Params p = params();
    double s = 0.0;
    double div = 0.0;
    spatial_terms(p, x, y, s, div);
    return s * temporal(p, t);
}

}  // namespace

double conductivity_x(double x) {
    return 1.55 + 0.50 * std::cos(kTwoPi * x);
}

double conductivity_y(double y) {
    return 1.45 + 0.35 * std::sin(kTwoPi * y);
}

double rho_cp() {
    return 1.0;
}

double domain_T() {
    return 1.0;
}

double initial_temp(double x, double y) {
    return exact_temp(x, y, 0.0);
}

double boundary_value(double x, double y, double t) {
    return exact_temp(x, y, t);
}

double heat_source(double x, double y, double t) {
    const Params p = params();
    double s = 0.0;
    double div = 0.0;
    spatial_terms(p, x, y, s, div);
    return p.rho * s * temporal_prime(p, t) - temporal(p, t) * div;
}
