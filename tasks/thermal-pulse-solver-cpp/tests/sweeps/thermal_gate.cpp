#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

struct Config {
    std::string case_name = "reference";
    int nx = 256;
    int ny = 256;
    int nt = 4096;
    double tol = 5.0e-3;
    double freq = 96.0;
    double sharp = 36.0;
};

struct Factors {
    std::vector<double> lower;
    std::vector<double> diag;
    std::vector<double> upper;
    std::vector<double> cprime;
    std::vector<double> denom;
};

struct Result {
    std::string status = "ok";
    double rel_error = std::numeric_limits<double>::quiet_NaN();
    double seconds = 0.0;
    double max_abs = 0.0;
};

int idx(int i, int j, int nx_int) {
    return j * nx_int + i;
}

double conductivity_x(double x) {
    return 1.55 + 0.50 * std::cos(kTwoPi * x);
}

double conductivity_x_prime(double x) {
    return -0.50 * kTwoPi * std::sin(kTwoPi * x);
}

double conductivity_y(double y) {
    return 1.45 + 0.35 * std::sin(kTwoPi * y);
}

double conductivity_y_prime(double y) {
    return 0.35 * kTwoPi * std::cos(kTwoPi * y);
}

double temporal(const Params& p, double t) {
    return (1.0 + p.slow * t) * (1.0 + p.amp * std::sin(kTwoPi * p.freq * t));
}

double temporal_prime(const Params& p, double t) {
    const double w = kTwoPi * p.freq;
    const double osc = 1.0 + p.amp * std::sin(w * t);
    return p.slow * osc + (1.0 + p.slow * t) * p.amp * w * std::cos(w * t);
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

    div_k_grad_s = conductivity_x_prime(x) * sx + conductivity_x(x) * sxx
        + conductivity_y_prime(y) * sy + conductivity_y(y) * syy;
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out << '\\' << ch;
        } else {
            out << ch;
        }
    }
    return out.str();
}

Factors make_factor(const std::vector<double>& lower, const std::vector<double>& diag, const std::vector<double>& upper) {
    const int n = static_cast<int>(diag.size());
    Factors f;
    f.lower = lower;
    f.diag = diag;
    f.upper = upper;
    f.cprime.assign(n, 0.0);
    f.denom.assign(n, 0.0);

    f.denom[0] = diag[0];
    if (n > 1) {
        f.cprime[0] = upper[0] / f.denom[0];
    }
    for (int i = 1; i < n; ++i) {
        f.denom[i] = diag[i] - lower[i] * f.cprime[i - 1];
        if (i + 1 < n) {
            f.cprime[i] = upper[i] / f.denom[i];
        }
    }
    return f;
}

void solve_factored(const Factors& f, std::vector<double>& rhs) {
    const int n = static_cast<int>(rhs.size());
    rhs[0] /= f.denom[0];
    for (int i = 1; i < n; ++i) {
        rhs[i] = (rhs[i] - f.lower[i] * rhs[i - 1]) / f.denom[i];
    }
    for (int i = n - 2; i >= 0; --i) {
        rhs[i] -= f.cprime[i] * rhs[i + 1];
    }
}

void build_operator_coefficients(
    int n_nodes,
    double h,
    bool x_axis,
    std::vector<double>& lower_l,
    std::vector<double>& diag_l,
    std::vector<double>& upper_l) {
    const int n = n_nodes - 2;
    lower_l.assign(n, 0.0);
    diag_l.assign(n, 0.0);
    upper_l.assign(n, 0.0);

    for (int i = 0; i < n; ++i) {
        const int node = i + 1;
        const double xm = (static_cast<double>(node) - 0.5) * h;
        const double xp = (static_cast<double>(node) + 0.5) * h;
        const double km = x_axis ? conductivity_x(xm) : conductivity_y(xm);
        const double kp = x_axis ? conductivity_x(xp) : conductivity_y(xp);
        const double scale = 1.0 / (h * h);
        lower_l[i] = km * scale;
        diag_l[i] = -(km + kp) * scale;
        upper_l[i] = kp * scale;
        if (i == 0) {
            lower_l[i] = 0.0;
        }
        if (i + 1 == n) {
            upper_l[i] = 0.0;
        }
    }
}

Factors make_implicit_factor(
    const std::vector<double>& lower_l,
    const std::vector<double>& diag_l,
    const std::vector<double>& upper_l,
    double r) {
    const int n = static_cast<int>(diag_l.size());
    std::vector<double> lower(n, 0.0);
    std::vector<double> diag(n, 0.0);
    std::vector<double> upper(n, 0.0);
    for (int i = 0; i < n; ++i) {
        lower[i] = -r * lower_l[i];
        diag[i] = 1.0 - r * diag_l[i];
        upper[i] = -r * upper_l[i];
    }
    return make_factor(lower, diag, upper);
}

void apply_plus_x(
    int nx_int,
    int ny_int,
    const std::vector<double>& lower_l,
    const std::vector<double>& diag_l,
    const std::vector<double>& upper_l,
    double r,
    const std::vector<double>& u,
    std::vector<double>& out) {
    for (int j = 0; j < ny_int; ++j) {
        for (int i = 0; i < nx_int; ++i) {
            double lu = diag_l[i] * u[idx(i, j, nx_int)];
            if (i > 0) {
                lu += lower_l[i] * u[idx(i - 1, j, nx_int)];
            }
            if (i + 1 < nx_int) {
                lu += upper_l[i] * u[idx(i + 1, j, nx_int)];
            }
            out[idx(i, j, nx_int)] = u[idx(i, j, nx_int)] + r * lu;
        }
    }
}

void apply_plus_y(
    int nx_int,
    int ny_int,
    const std::vector<double>& lower_l,
    const std::vector<double>& diag_l,
    const std::vector<double>& upper_l,
    double r,
    const std::vector<double>& u,
    std::vector<double>& out) {
    for (int j = 0; j < ny_int; ++j) {
        for (int i = 0; i < nx_int; ++i) {
            double lu = diag_l[j] * u[idx(i, j, nx_int)];
            if (j > 0) {
                lu += lower_l[j] * u[idx(i, j - 1, nx_int)];
            }
            if (j + 1 < ny_int) {
                lu += upper_l[j] * u[idx(i, j + 1, nx_int)];
            }
            out[idx(i, j, nx_int)] = u[idx(i, j, nx_int)] + r * lu;
        }
    }
}

void apply_laplacian_split(
    int nx_int,
    int ny_int,
    const std::vector<double>& lower_x,
    const std::vector<double>& diag_x,
    const std::vector<double>& upper_x,
    const std::vector<double>& lower_y,
    const std::vector<double>& diag_y,
    const std::vector<double>& upper_y,
    const std::vector<double>& u,
    std::vector<double>& out) {
    for (int j = 0; j < ny_int; ++j) {
        for (int i = 0; i < nx_int; ++i) {
            double lu = diag_x[i] * u[idx(i, j, nx_int)] + diag_y[j] * u[idx(i, j, nx_int)];
            if (i > 0) {
                lu += lower_x[i] * u[idx(i - 1, j, nx_int)];
            }
            if (i + 1 < nx_int) {
                lu += upper_x[i] * u[idx(i + 1, j, nx_int)];
            }
            if (j > 0) {
                lu += lower_y[j] * u[idx(i, j - 1, nx_int)];
            }
            if (j + 1 < ny_int) {
                lu += upper_y[j] * u[idx(i, j + 1, nx_int)];
            }
            out[idx(i, j, nx_int)] = lu;
        }
    }
}

void solve_x_lines(int nx_int, int ny_int, const Factors& factor_x, std::vector<double>& field) {
    std::vector<double> line(nx_int, 0.0);
    for (int j = 0; j < ny_int; ++j) {
        for (int i = 0; i < nx_int; ++i) {
            line[i] = field[idx(i, j, nx_int)];
        }
        solve_factored(factor_x, line);
        for (int i = 0; i < nx_int; ++i) {
            field[idx(i, j, nx_int)] = line[i];
        }
    }
}

void solve_y_lines(int nx_int, int ny_int, const Factors& factor_y, std::vector<double>& field) {
    std::vector<double> line(ny_int, 0.0);
    for (int i = 0; i < nx_int; ++i) {
        for (int j = 0; j < ny_int; ++j) {
            line[j] = field[idx(i, j, nx_int)];
        }
        solve_factored(factor_y, line);
        for (int j = 0; j < ny_int; ++j) {
            field[idx(i, j, nx_int)] = line[j];
        }
    }
}

void accumulate_snapshot_error(
    const Params& p,
    double t,
    double theta,
    const std::vector<double>& s_grid,
    const std::vector<double>& u0,
    const std::vector<double>& u1,
    double& err2,
    double& ref2,
    double& max_abs) {
    const double d = temporal(p, t);
    for (std::size_t n = 0; n < s_grid.size(); ++n) {
        const double approx = (1.0 - theta) * u0[n] + theta * u1[n];
        const double exact = s_grid[n] * d;
        const double diff = approx - exact;
        err2 += diff * diff;
        ref2 += exact * exact;
        max_abs = std::max(max_abs, std::abs(approx));
    }
}

Result run_adi(const Config& cfg, const Params& p) {
    const auto start = std::chrono::steady_clock::now();
    const int nx_int = cfg.nx - 2;
    const int ny_int = cfg.ny - 2;
    const int total = nx_int * ny_int;
    const double hx = 1.0 / static_cast<double>(cfg.nx - 1);
    const double hy = 1.0 / static_cast<double>(cfg.ny - 1);
    const double dt = p.final_t / static_cast<double>(cfg.nt);
    const double r = 0.5 * dt / p.rho;

    std::vector<double> s_grid(total, 0.0);
    std::vector<double> div_grid(total, 0.0);
    for (int j = 0; j < ny_int; ++j) {
        const double y = static_cast<double>(j + 1) * hy;
        for (int i = 0; i < nx_int; ++i) {
            const double x = static_cast<double>(i + 1) * hx;
            spatial_terms(p, x, y, s_grid[idx(i, j, nx_int)], div_grid[idx(i, j, nx_int)]);
        }
    }

    std::vector<double> lx_lower, lx_diag, lx_upper;
    std::vector<double> ly_lower, ly_diag, ly_upper;
    build_operator_coefficients(cfg.nx, hx, true, lx_lower, lx_diag, lx_upper);
    build_operator_coefficients(cfg.ny, hy, false, ly_lower, ly_diag, ly_upper);
    const Factors factor_x = make_implicit_factor(lx_lower, lx_diag, lx_upper, r);
    const Factors factor_y = make_implicit_factor(ly_lower, ly_diag, ly_upper, r);

    std::vector<double> u(total, 0.0);
    std::vector<double> next(total, 0.0);
    std::vector<double> tmp(total, 0.0);
    std::vector<double> rhs(total, 0.0);
    for (int n = 0; n < total; ++n) {
        u[n] = s_grid[n] * temporal(p, 0.0);
    }

    const std::vector<double> sample_times = {0.137, 0.333, 0.591, 0.827};
    std::size_t sample_index = 0;
    double err2 = 0.0;
    double ref2 = 0.0;
    double max_abs = 0.0;

    for (int step = 0; step < cfg.nt; ++step) {
        const double t0 = static_cast<double>(step) * dt;
        const double t1 = static_cast<double>(step + 1) * dt;
        const double tm = 0.5 * (t0 + t1);
        const double d = temporal(p, tm);
        const double dp = temporal_prime(p, tm);

        apply_plus_x(nx_int, ny_int, lx_lower, lx_diag, lx_upper, r, u, tmp);
        apply_plus_y(nx_int, ny_int, ly_lower, ly_diag, ly_upper, r, tmp, rhs);

        for (int n = 0; n < total; ++n) {
            rhs[n] += dt / p.rho * (p.rho * s_grid[n] * dp - d * div_grid[n]);
        }

        solve_x_lines(nx_int, ny_int, factor_x, rhs);
        solve_y_lines(nx_int, ny_int, factor_y, rhs);
        next.swap(rhs);

        while (sample_index < sample_times.size() && sample_times[sample_index] <= t1 + 1.0e-14) {
            const double ts = sample_times[sample_index];
            if (ts >= t0 - 1.0e-14) {
                const double theta = std::max(0.0, std::min(1.0, (ts - t0) / dt));
                accumulate_snapshot_error(p, ts, theta, s_grid, u, next, err2, ref2, max_abs);
            }
            ++sample_index;
        }

        u.swap(next);
    }

    Result result;
    result.rel_error = std::sqrt(err2 / std::max(ref2, 1.0e-300));
    result.max_abs = max_abs;
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}

Result run_explicit(const Config& cfg, const Params& p) {
    const auto start = std::chrono::steady_clock::now();
    const int nx_int = cfg.nx - 2;
    const int ny_int = cfg.ny - 2;
    const int total = nx_int * ny_int;
    const double hx = 1.0 / static_cast<double>(cfg.nx - 1);
    const double hy = 1.0 / static_cast<double>(cfg.ny - 1);
    const double dt = p.final_t / static_cast<double>(cfg.nt);

    std::vector<double> s_grid(total, 0.0);
    std::vector<double> div_grid(total, 0.0);
    for (int j = 0; j < ny_int; ++j) {
        const double y = static_cast<double>(j + 1) * hy;
        for (int i = 0; i < nx_int; ++i) {
            const double x = static_cast<double>(i + 1) * hx;
            spatial_terms(p, x, y, s_grid[idx(i, j, nx_int)], div_grid[idx(i, j, nx_int)]);
        }
    }

    std::vector<double> lx_lower, lx_diag, lx_upper;
    std::vector<double> ly_lower, ly_diag, ly_upper;
    build_operator_coefficients(cfg.nx, hx, true, lx_lower, lx_diag, lx_upper);
    build_operator_coefficients(cfg.ny, hy, false, ly_lower, ly_diag, ly_upper);

    std::vector<double> u(total, 0.0);
    std::vector<double> lu(total, 0.0);
    std::vector<double> next(total, 0.0);
    for (int n = 0; n < total; ++n) {
        u[n] = s_grid[n] * temporal(p, 0.0);
    }

    const std::vector<double> sample_times = {0.137, 0.333, 0.591, 0.827};
    std::size_t sample_index = 0;
    double err2 = 0.0;
    double ref2 = 0.0;
    double max_abs = 0.0;
    Result result;

    for (int step = 0; step < cfg.nt; ++step) {
        const double t0 = static_cast<double>(step) * dt;
        const double t1 = static_cast<double>(step + 1) * dt;
        const double tm = 0.5 * (t0 + t1);
        const double d = temporal(p, tm);
        const double dp = temporal_prime(p, tm);

        apply_laplacian_split(
            nx_int, ny_int,
            lx_lower, lx_diag, lx_upper,
            ly_lower, ly_diag, ly_upper,
            u, lu);

        for (int n = 0; n < total; ++n) {
            const double q = p.rho * s_grid[n] * dp - d * div_grid[n];
            next[n] = u[n] + dt / p.rho * (lu[n] + q);
            max_abs = std::max(max_abs, std::abs(next[n]));
        }

        if (!std::isfinite(max_abs) || max_abs > 1.0e8) {
            result.status = "unstable";
            result.rel_error = std::numeric_limits<double>::infinity();
            result.max_abs = max_abs;
            result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            return result;
        }

        while (sample_index < sample_times.size() && sample_times[sample_index] <= t1 + 1.0e-14) {
            const double ts = sample_times[sample_index];
            if (ts >= t0 - 1.0e-14) {
                const double theta = std::max(0.0, std::min(1.0, (ts - t0) / dt));
                accumulate_snapshot_error(p, ts, theta, s_grid, u, next, err2, ref2, max_abs);
            }
            ++sample_index;
        }

        u.swap(next);
    }

    result.status = "ok";
    result.rel_error = std::sqrt(err2 / std::max(ref2, 1.0e-300));
    result.max_abs = max_abs;
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}

Config parse_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + name);
            }
            return argv[++i];
        };

        if (arg == "--case") {
            cfg.case_name = require_value(arg);
        } else if (arg == "--nx") {
            cfg.nx = std::stoi(require_value(arg));
        } else if (arg == "--ny") {
            cfg.ny = std::stoi(require_value(arg));
        } else if (arg == "--nt") {
            cfg.nt = std::stoi(require_value(arg));
        } else if (arg == "--tol") {
            cfg.tol = std::stod(require_value(arg));
        } else if (arg == "--freq") {
            cfg.freq = std::stod(require_value(arg));
        } else if (arg == "--sharp") {
            cfg.sharp = std::stod(require_value(arg));
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    if (cfg.nx < 4 || cfg.ny < 4 || cfg.nt < 1) {
        throw std::runtime_error("invalid grid or time-step count");
    }
    return cfg;
}

void print_json(const Config& cfg, const Result& result) {
    std::cout << std::setprecision(12)
              << "{"
              << "\"case\":\"" << json_escape(cfg.case_name) << "\","
              << "\"nx\":" << cfg.nx << ","
              << "\"ny\":" << cfg.ny << ","
              << "\"nt\":" << cfg.nt << ","
              << "\"seconds\":" << result.seconds << ","
              << "\"relative_error\":";
    if (std::isfinite(result.rel_error)) {
        std::cout << result.rel_error;
    } else {
        std::cout << "null";
    }
    std::cout << ",\"status\":\"" << json_escape(result.status) << "\","
              << "\"max_abs\":" << result.max_abs
              << "}" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Config cfg = parse_args(argc, argv);
        Params params;
        params.freq = cfg.freq;
        params.sharp = cfg.sharp;

        Result result;
        if (cfg.case_name == "explicit") {
            result = run_explicit(cfg, params);
        } else {
            result = run_adi(cfg, params);
        }

        print_json(cfg, result);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "thermal_gate: " << ex.what() << std::endl;
        return 2;
    }
}
