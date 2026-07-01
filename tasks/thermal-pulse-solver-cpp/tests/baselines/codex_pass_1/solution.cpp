#include "thermal_oracle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

struct Point {
    double x = 0.0;
    double y = 0.0;
    double t = 0.0;
};

double finite_or(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

double positive_or(double value, double fallback) {
    value = finite_or(value, fallback);
    if (value <= 1.0e-12) {
        return 1.0e-12;
    }
    return value;
}

double parse_number_after(const std::string& text, const std::string& key, std::size_t start, std::size_t limit) {
    const std::size_t key_pos = text.find(key, start);
    if (key_pos == std::string::npos || key_pos >= limit) {
        throw std::runtime_error("missing key");
    }
    const std::size_t colon = text.find(':', key_pos + key.size());
    if (colon == std::string::npos || colon >= limit) {
        throw std::runtime_error("missing colon");
    }
    const char* begin = text.c_str() + colon + 1;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (begin == end || static_cast<std::size_t>(end - text.c_str()) > limit) {
        throw std::runtime_error("invalid number");
    }
    return value;
}

std::vector<Point> parse_points(const std::string& text) {
    std::vector<Point> points;
    const std::size_t points_key = text.find("\"points\"");
    if (points_key == std::string::npos) {
        throw std::runtime_error("missing points");
    }
    const std::size_t array_begin = text.find('[', points_key);
    if (array_begin == std::string::npos) {
        throw std::runtime_error("missing points array");
    }

    std::size_t pos = array_begin + 1;
    while (true) {
        const std::size_t object_begin = text.find('{', pos);
        const std::size_t array_end = text.find(']', pos);
        if (array_end != std::string::npos && (object_begin == std::string::npos || array_end < object_begin)) {
            break;
        }
        if (object_begin == std::string::npos) {
            break;
        }
        const std::size_t object_end = text.find('}', object_begin + 1);
        if (object_end == std::string::npos) {
            throw std::runtime_error("unterminated point object");
        }
        Point p;
        p.x = parse_number_after(text, "\"x\"", object_begin, object_end);
        p.y = parse_number_after(text, "\"y\"", object_begin, object_end);
        p.t = parse_number_after(text, "\"t\"", object_begin, object_end);
        p.x = clamp01(finite_or(p.x, 0.0));
        p.y = clamp01(finite_or(p.y, 0.0));
        p.t = std::max(0.0, finite_or(p.t, 0.0));
        points.push_back(p);
        pos = object_end + 1;
    }
    return points;
}

std::string read_file(const char* path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open input");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

double estimate_cycles_1d(const std::vector<double>& values, double significance_scale) {
    const int n = static_cast<int>(values.size());
    if (n < 3) {
        return 0.0;
    }

    auto minmax = std::minmax_element(values.begin(), values.end());
    const double range = *minmax.second - *minmax.first;
    const double floor = 1.0e-9 * std::max(1.0, significance_scale);
    if (!(range > floor)) {
        return 0.0;
    }

    double total_variation = 0.0;
    double max_second = 0.0;
    for (int i = 1; i < n; ++i) {
        total_variation += std::abs(values[i] - values[i - 1]);
    }
    for (int i = 1; i + 1 < n; ++i) {
        max_second = std::max(max_second, std::abs(values[i - 1] - 2.0 * values[i] + values[i + 1]));
    }

    int extrema = 0;
    double prev_slope = 0.0;
    const double slope_floor = 1.0e-5 * range;
    for (int i = 1; i < n; ++i) {
        const double slope = values[i] - values[i - 1];
        if (std::abs(slope) <= slope_floor) {
            continue;
        }
        if (prev_slope != 0.0 && slope * prev_slope < 0.0) {
            ++extrema;
        }
        prev_slope = slope;
    }

    const double cycles_tv = total_variation / (2.0 * range);
    const double cycles_extrema = 0.5 * static_cast<double>(extrema);
    const double cycles_second = (static_cast<double>(n - 1) / (2.0 * kPi)) *
                                 std::sqrt(std::max(0.0, 2.0 * max_second / range));
    return std::max({0.0, cycles_tv, cycles_extrema, cycles_second});
}

int round_grid_size(int n) {
    n = std::max(28, std::min(132, n));
    const int rem = n % 4;
    if (rem != 0) {
        n += 4 - rem;
    }
    return std::max(28, std::min(132, n));
}

struct CaseAnalysis {
    double max_time = 0.0;
    double rho = 1.0;
    double kx_max = 0.0;
    double ky_max = 0.0;
    double value_scale = 1.0;
    double source_scale = 0.0;
    double spatial_cycles_x = 0.0;
    double spatial_cycles_y = 0.0;
    double time_cycles = 0.0;
    int nx = 48;
    int ny = 48;
    double nominal_dt = 1.0e-2;
};

void add_rows_and_columns(const std::vector<double>& grid,
                          int n,
                          double scale,
                          double& cycles_x,
                          double& cycles_y) {
    std::vector<double> series(n);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            series[i] = grid[j * n + i];
        }
        cycles_x = std::max(cycles_x, estimate_cycles_1d(series, scale));
    }
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            series[j] = grid[j * n + i];
        }
        cycles_y = std::max(cycles_y, estimate_cycles_1d(series, scale));
    }
}

CaseAnalysis analyze_case(const std::vector<Point>& points) {
    CaseAnalysis analysis;
    for (const Point& p : points) {
        analysis.max_time = std::max(analysis.max_time, p.t);
    }
    analysis.rho = positive_or(rho_cp(), 1.0);

    double max_initial = 0.0;
    double max_boundary = 0.0;
    double max_source = 0.0;

    constexpr int kCoeffSamples = 129;
    std::vector<double> coeff(kCoeffSamples);
    for (int i = 0; i < kCoeffSamples; ++i) {
        const double x = static_cast<double>(i) / static_cast<double>(kCoeffSamples - 1);
        coeff[i] = positive_or(conductivity_x(x), 1.0);
        analysis.kx_max = std::max(analysis.kx_max, coeff[i]);
    }
    analysis.spatial_cycles_x = std::max(analysis.spatial_cycles_x, estimate_cycles_1d(coeff, analysis.kx_max));
    for (int i = 0; i < kCoeffSamples; ++i) {
        const double y = static_cast<double>(i) / static_cast<double>(kCoeffSamples - 1);
        coeff[i] = positive_or(conductivity_y(y), 1.0);
        analysis.ky_max = std::max(analysis.ky_max, coeff[i]);
    }
    analysis.spatial_cycles_y = std::max(analysis.spatial_cycles_y, estimate_cycles_1d(coeff, analysis.ky_max));

    constexpr int kSpatialSamples = 49;
    std::vector<double> grid(kSpatialSamples * kSpatialSamples);
    for (int j = 0; j < kSpatialSamples; ++j) {
        const double y = static_cast<double>(j) / static_cast<double>(kSpatialSamples - 1);
        for (int i = 0; i < kSpatialSamples; ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(kSpatialSamples - 1);
            const double value = finite_or(initial_temp(x, y), 0.0);
            max_initial = std::max(max_initial, std::abs(value));
            grid[j * kSpatialSamples + i] = value;
        }
    }
    add_rows_and_columns(grid, kSpatialSamples, std::max(1.0, max_initial),
                         analysis.spatial_cycles_x, analysis.spatial_cycles_y);

    const double T = analysis.max_time;
    const std::vector<double> sample_times = T > 0.0
                                                 ? std::vector<double>{0.0, 0.17 * T, 0.5 * T, 0.83 * T, T}
                                                 : std::vector<double>{0.0};

    for (double t : sample_times) {
        for (int j = 0; j < kSpatialSamples; ++j) {
            const double y = static_cast<double>(j) / static_cast<double>(kSpatialSamples - 1);
            for (int i = 0; i < kSpatialSamples; ++i) {
                const double x = static_cast<double>(i) / static_cast<double>(kSpatialSamples - 1);
                const double value = finite_or(heat_source(x, y, t), 0.0);
                max_source = std::max(max_source, std::abs(value));
                grid[j * kSpatialSamples + i] = value;
            }
        }
        const double source_importance = std::max(1.0, max_source * std::max(T, 1.0e-6) / analysis.rho);
        add_rows_and_columns(grid, kSpatialSamples, source_importance,
                             analysis.spatial_cycles_x, analysis.spatial_cycles_y);

        std::vector<double> edge(kSpatialSamples);
        for (int edge_id = 0; edge_id < 4; ++edge_id) {
            for (int i = 0; i < kSpatialSamples; ++i) {
                const double s = static_cast<double>(i) / static_cast<double>(kSpatialSamples - 1);
                double x = s;
                double y = 0.0;
                if (edge_id == 1) {
                    y = 1.0;
                } else if (edge_id == 2) {
                    x = 0.0;
                    y = s;
                } else if (edge_id == 3) {
                    x = 1.0;
                    y = s;
                }
                edge[i] = finite_or(boundary_value(x, y, t), 0.0);
                max_boundary = std::max(max_boundary, std::abs(edge[i]));
            }
            if (edge_id < 2) {
                analysis.spatial_cycles_x = std::max(analysis.spatial_cycles_x,
                                                     estimate_cycles_1d(edge, std::max(1.0, max_boundary)));
            } else {
                analysis.spatial_cycles_y = std::max(analysis.spatial_cycles_y,
                                                     estimate_cycles_1d(edge, std::max(1.0, max_boundary)));
            }
        }
    }

    analysis.source_scale = max_source * std::max(T, 1.0e-6) / analysis.rho;
    analysis.value_scale = std::max({1.0, max_initial, max_boundary, analysis.source_scale});

    if (T > 0.0) {
        constexpr int kTimeSamples = 513;
        std::vector<std::vector<double>> signals;
        const std::vector<std::pair<double, double>> source_probes = {
            {0.25, 0.25}, {0.50, 0.50}, {0.75, 0.75}, {0.25, 0.75}, {0.75, 0.25}};
        const std::vector<std::pair<double, double>> boundary_probes = {
            {0.00, 0.15}, {0.00, 0.50}, {0.00, 0.85}, {1.00, 0.15}, {1.00, 0.50}, {1.00, 0.85},
            {0.15, 0.00}, {0.50, 0.00}, {0.85, 0.00}, {0.15, 1.00}, {0.50, 1.00}, {0.85, 1.00}};
        signals.resize(source_probes.size() + boundary_probes.size());

        for (int m = 0; m < kTimeSamples; ++m) {
            const double t = T * static_cast<double>(m) / static_cast<double>(kTimeSamples - 1);
            std::size_t signal_id = 0;
            for (const auto& probe : source_probes) {
                signals[signal_id++].push_back(finite_or(heat_source(probe.first, probe.second, t), 0.0) *
                                               std::max(T, 1.0e-9) / analysis.rho);
            }
            for (const auto& probe : boundary_probes) {
                signals[signal_id++].push_back(finite_or(boundary_value(probe.first, probe.second, t), 0.0));
            }
        }

        for (const std::vector<double>& signal : signals) {
            analysis.time_cycles = std::max(analysis.time_cycles,
                                            estimate_cycles_1d(signal, analysis.value_scale));
        }
    }

    analysis.nx = round_grid_size(static_cast<int>(std::ceil(36.0 + 12.0 * analysis.spatial_cycles_x)));
    analysis.ny = round_grid_size(static_cast<int>(std::ceil(36.0 + 12.0 * analysis.spatial_cycles_y)));

    if (analysis.spatial_cycles_x > 6.0) {
        analysis.nx = round_grid_size(static_cast<int>(std::ceil(44.0 + 14.0 * analysis.spatial_cycles_x)));
    }
    if (analysis.spatial_cycles_y > 6.0) {
        analysis.ny = round_grid_size(static_cast<int>(std::ceil(44.0 + 14.0 * analysis.spatial_cycles_y)));
    }

    if (T > 0.0) {
        const double temporal_steps = std::max(28.0, 32.0 * analysis.time_cycles + 8.0);
        const double dt_temporal = T / temporal_steps;
        const double lambda_slow = (analysis.kx_max + analysis.ky_max) * kPi * kPi / analysis.rho;
        const double dt_diffusive = lambda_slow > 1.0e-12 ? 0.18 / lambda_slow : T;
        const int cells = std::max(1, (analysis.nx - 1) * (analysis.ny - 1));
        const double max_steps = std::max(900.0, std::min(6500.0, 36000000.0 / static_cast<double>(cells)));
        const double dt_budget = T / max_steps;
        analysis.nominal_dt = std::max(dt_budget, std::min(dt_temporal, dt_diffusive));
    } else {
        analysis.nominal_dt = 1.0;
    }
    analysis.nominal_dt = std::max(1.0e-12, finite_or(analysis.nominal_dt, 1.0e-2));
    return analysis;
}

struct TridiagonalFactors {
    std::vector<double> lower;
    std::vector<double> cprime;
    std::vector<double> inv_denom;
};

TridiagonalFactors make_factors(const std::vector<double>& left,
                                const std::vector<double>& diag,
                                const std::vector<double>& right,
                                double alpha) {
    const int n = static_cast<int>(diag.size());
    TridiagonalFactors factors;
    factors.lower.assign(n, 0.0);
    factors.cprime.assign(n, 0.0);
    factors.inv_denom.assign(n, 1.0);
    if (n == 0) {
        return factors;
    }

    double denom = 1.0 - alpha * diag[0];
    if (std::abs(denom) < 1.0e-14) {
        denom = denom < 0.0 ? -1.0e-14 : 1.0e-14;
    }
    factors.inv_denom[0] = 1.0 / denom;
    factors.cprime[0] = n > 1 ? (-alpha * right[0]) * factors.inv_denom[0] : 0.0;
    for (int i = 1; i < n; ++i) {
        factors.lower[i] = -alpha * left[i];
        denom = 1.0 - alpha * diag[i] - factors.lower[i] * factors.cprime[i - 1];
        if (std::abs(denom) < 1.0e-14) {
            denom = denom < 0.0 ? -1.0e-14 : 1.0e-14;
        }
        factors.inv_denom[i] = 1.0 / denom;
        factors.cprime[i] = i + 1 < n ? (-alpha * right[i]) * factors.inv_denom[i] : 0.0;
    }
    return factors;
}

class HeatSolver {
public:
    explicit HeatSolver(const CaseAnalysis& analysis)
        : nx_(analysis.nx),
          ny_(analysis.ny),
          ix_(analysis.nx - 1),
          iy_(analysis.ny - 1),
          hx_(1.0 / static_cast<double>(analysis.nx)),
          hy_(1.0 / static_cast<double>(analysis.ny)),
          rho_(analysis.rho),
          u_(static_cast<std::size_t>(ix_ * iy_), 0.0),
          scratch1_(u_.size(), 0.0),
          scratch2_(u_.size(), 0.0),
          source_(u_.size(), 0.0),
          xcoords_(ix_, 0.0),
          ycoords_(iy_, 0.0),
          x_left_(ix_, 0.0),
          x_diag_(ix_, 0.0),
          x_right_(ix_, 0.0),
          y_left_(iy_, 0.0),
          y_diag_(iy_, 0.0),
          y_right_(iy_, 0.0),
          left_boundary_(iy_, 0.0),
          right_boundary_(iy_, 0.0),
          bottom_boundary_(ix_, 0.0),
          top_boundary_(ix_, 0.0) {
        initialize_coefficients();
        initialize_temperature();
    }

    void advance(double t0, double t1, double nominal_dt) {
        if (!(t1 > t0)) {
            return;
        }
        const double span = t1 - t0;
        const int steps = std::max(1, static_cast<int>(std::ceil(span / std::max(nominal_dt, 1.0e-12))));
        const double dt = span / static_cast<double>(steps);
        double t = t0;
        for (int step = 0; step < steps; ++step) {
            step_pr(t, dt);
            t += dt;
        }
    }

    double interpolate(double x, double y, double t) const {
        x = clamp01(x);
        y = clamp01(y);
        if (x <= 1.0e-14 || x >= 1.0 - 1.0e-14 || y <= 1.0e-14 || y >= 1.0 - 1.0e-14) {
            return finite_or(boundary_value(x, y, t), 0.0);
        }

        double gx = x / hx_;
        double gy = y / hy_;
        int i0 = static_cast<int>(std::floor(gx));
        int j0 = static_cast<int>(std::floor(gy));
        if (i0 < 0) {
            i0 = 0;
        }
        if (j0 < 0) {
            j0 = 0;
        }
        if (i0 >= nx_) {
            i0 = nx_ - 1;
        }
        if (j0 >= ny_) {
            j0 = ny_ - 1;
        }
        const double fx = gx - static_cast<double>(i0);
        const double fy = gy - static_cast<double>(j0);

        const double v00 = node_value(i0, j0, t);
        const double v10 = node_value(i0 + 1, j0, t);
        const double v01 = node_value(i0, j0 + 1, t);
        const double v11 = node_value(i0 + 1, j0 + 1, t);

        const double vx0 = (1.0 - fx) * v00 + fx * v10;
        const double vx1 = (1.0 - fx) * v01 + fx * v11;
        return finite_or((1.0 - fy) * vx0 + fy * vx1, 0.0);
    }

private:
    int idx(int i, int j) const {
        return j * ix_ + i;
    }

    void initialize_coefficients() {
        for (int i = 0; i < ix_; ++i) {
            const double x = static_cast<double>(i + 1) * hx_;
            xcoords_[i] = x;
            const double k_left = positive_or(conductivity_x(x - 0.5 * hx_), 1.0);
            const double k_right = positive_or(conductivity_x(x + 0.5 * hx_), 1.0);
            x_left_[i] = k_left / (rho_ * hx_ * hx_);
            x_right_[i] = k_right / (rho_ * hx_ * hx_);
            x_diag_[i] = -(x_left_[i] + x_right_[i]);
        }
        for (int j = 0; j < iy_; ++j) {
            const double y = static_cast<double>(j + 1) * hy_;
            ycoords_[j] = y;
            const double k_down = positive_or(conductivity_y(y - 0.5 * hy_), 1.0);
            const double k_up = positive_or(conductivity_y(y + 0.5 * hy_), 1.0);
            y_left_[j] = k_down / (rho_ * hy_ * hy_);
            y_right_[j] = k_up / (rho_ * hy_ * hy_);
            y_diag_[j] = -(y_left_[j] + y_right_[j]);
        }
    }

    void initialize_temperature() {
        for (int j = 0; j < iy_; ++j) {
            const double y = ycoords_[j];
            for (int i = 0; i < ix_; ++i) {
                const double x = xcoords_[i];
                u_[idx(i, j)] = finite_or(initial_temp(x, y), 0.0);
            }
        }
    }

    double node_value(int grid_i, int grid_j, double t) const {
        grid_i = std::max(0, std::min(nx_, grid_i));
        grid_j = std::max(0, std::min(ny_, grid_j));
        const double x = static_cast<double>(grid_i) * hx_;
        const double y = static_cast<double>(grid_j) * hy_;
        if (grid_i == 0 || grid_i == nx_ || grid_j == 0 || grid_j == ny_) {
            return finite_or(boundary_value(x, y, t), 0.0);
        }
        return finite_or(u_[idx(grid_i - 1, grid_j - 1)], 0.0);
    }

    void compute_source(double t) {
        std::fill(source_.begin(), source_.end(), 0.0);

        for (int j = 0; j < iy_; ++j) {
            left_boundary_[j] = finite_or(boundary_value(0.0, ycoords_[j], t), 0.0);
            right_boundary_[j] = finite_or(boundary_value(1.0, ycoords_[j], t), 0.0);
        }
        for (int i = 0; i < ix_; ++i) {
            bottom_boundary_[i] = finite_or(boundary_value(xcoords_[i], 0.0, t), 0.0);
            top_boundary_[i] = finite_or(boundary_value(xcoords_[i], 1.0, t), 0.0);
        }

        for (int j = 0; j < iy_; ++j) {
            const double y = ycoords_[j];
            for (int i = 0; i < ix_; ++i) {
                const double x = xcoords_[i];
                double value = finite_or(heat_source(x, y, t), 0.0) / rho_;
                if (i == 0) {
                    value += x_left_[i] * left_boundary_[j];
                }
                if (i == ix_ - 1) {
                    value += x_right_[i] * right_boundary_[j];
                }
                if (j == 0) {
                    value += y_left_[j] * bottom_boundary_[i];
                }
                if (j == iy_ - 1) {
                    value += y_right_[j] * top_boundary_[i];
                }
                source_[idx(i, j)] = value;
            }
        }
    }

    void apply_y(const std::vector<double>& in, double alpha, std::vector<double>& out) const {
        for (int j = 0; j < iy_; ++j) {
            for (int i = 0; i < ix_; ++i) {
                double value = in[idx(i, j)] * (1.0 + alpha * y_diag_[j]);
                if (j > 0) {
                    value += alpha * y_left_[j] * in[idx(i, j - 1)];
                }
                if (j + 1 < iy_) {
                    value += alpha * y_right_[j] * in[idx(i, j + 1)];
                }
                out[idx(i, j)] = value;
            }
        }
    }

    void apply_x(const std::vector<double>& in, double alpha, std::vector<double>& out) const {
        for (int j = 0; j < iy_; ++j) {
            for (int i = 0; i < ix_; ++i) {
                double value = in[idx(i, j)] * (1.0 + alpha * x_diag_[i]);
                if (i > 0) {
                    value += alpha * x_left_[i] * in[idx(i - 1, j)];
                }
                if (i + 1 < ix_) {
                    value += alpha * x_right_[i] * in[idx(i + 1, j)];
                }
                out[idx(i, j)] = value;
            }
        }
    }

    void solve_x_rows(const std::vector<double>& rhs,
                      const TridiagonalFactors& factors,
                      std::vector<double>& out) {
        if (ix_ == 0 || iy_ == 0) {
            return;
        }
        std::vector<double> dprime(ix_, 0.0);
        for (int j = 0; j < iy_; ++j) {
            dprime[0] = rhs[idx(0, j)] * factors.inv_denom[0];
            for (int i = 1; i < ix_; ++i) {
                dprime[i] = (rhs[idx(i, j)] - factors.lower[i] * dprime[i - 1]) * factors.inv_denom[i];
            }
            out[idx(ix_ - 1, j)] = dprime[ix_ - 1];
            for (int i = ix_ - 2; i >= 0; --i) {
                out[idx(i, j)] = dprime[i] - factors.cprime[i] * out[idx(i + 1, j)];
            }
        }
    }

    void solve_y_columns(const std::vector<double>& rhs,
                         const TridiagonalFactors& factors,
                         std::vector<double>& out) {
        if (ix_ == 0 || iy_ == 0) {
            return;
        }
        std::vector<double> dprime(iy_, 0.0);
        for (int i = 0; i < ix_; ++i) {
            dprime[0] = rhs[idx(i, 0)] * factors.inv_denom[0];
            for (int j = 1; j < iy_; ++j) {
                dprime[j] = (rhs[idx(i, j)] - factors.lower[j] * dprime[j - 1]) * factors.inv_denom[j];
            }
            out[idx(i, iy_ - 1)] = dprime[iy_ - 1];
            for (int j = iy_ - 2; j >= 0; --j) {
                out[idx(i, j)] = dprime[j] - factors.cprime[j] * out[idx(i, j + 1)];
            }
        }
    }

    void step_pr(double t, double dt) {
        const double alpha = 0.5 * dt;
        compute_source(t + 0.5 * dt);

        TridiagonalFactors x_factors = make_factors(x_left_, x_diag_, x_right_, alpha);
        TridiagonalFactors y_factors = make_factors(y_left_, y_diag_, y_right_, alpha);

        apply_y(u_, alpha, scratch1_);
        for (std::size_t i = 0; i < scratch1_.size(); ++i) {
            scratch1_[i] += alpha * source_[i];
        }
        solve_x_rows(scratch1_, x_factors, scratch2_);

        apply_x(scratch2_, alpha, scratch1_);
        for (std::size_t i = 0; i < scratch1_.size(); ++i) {
            scratch1_[i] += alpha * source_[i];
        }
        solve_y_columns(scratch1_, y_factors, u_);

        for (double& value : u_) {
            value = finite_or(value, 0.0);
        }
    }

    int nx_;
    int ny_;
    int ix_;
    int iy_;
    double hx_;
    double hy_;
    double rho_;
    std::vector<double> u_;
    std::vector<double> scratch1_;
    std::vector<double> scratch2_;
    std::vector<double> source_;
    std::vector<double> xcoords_;
    std::vector<double> ycoords_;
    std::vector<double> x_left_;
    std::vector<double> x_diag_;
    std::vector<double> x_right_;
    std::vector<double> y_left_;
    std::vector<double> y_diag_;
    std::vector<double> y_right_;
    std::vector<double> left_boundary_;
    std::vector<double> right_boundary_;
    std::vector<double> bottom_boundary_;
    std::vector<double> top_boundary_;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: solver <queries.json> <predictions.json>\n";
        return 2;
    }

    try {
        const std::vector<Point> points = parse_points(read_file(argv[1]));
        std::vector<double> temperatures(points.size(), 0.0);

        if (!points.empty()) {
            CaseAnalysis analysis = analyze_case(points);
            HeatSolver solver(analysis);

            std::vector<int> order(points.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
                return points[lhs].t < points[rhs].t;
            });

            double current_t = 0.0;
            std::size_t cursor = 0;
            while (cursor < order.size()) {
                const double target_t = points[order[cursor]].t;
                solver.advance(current_t, target_t, analysis.nominal_dt);
                current_t = target_t;

                while (cursor < order.size() && points[order[cursor]].t == target_t) {
                    const int point_id = order[cursor];
                    temperatures[point_id] = solver.interpolate(points[point_id].x, points[point_id].y, target_t);
                    if (!std::isfinite(temperatures[point_id])) {
                        temperatures[point_id] = 0.0;
                    }
                    ++cursor;
                }
            }
        }

        std::ofstream out(argv[2]);
        if (!out) {
            throw std::runtime_error("cannot open output");
        }
        out << "{\"temperatures\":[";
        for (std::size_t i = 0; i < temperatures.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            out << std::setprecision(17) << temperatures[i];
        }
        out << "]}\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "solution failed: " << ex.what() << "\n";
        return 1;
    }
}
