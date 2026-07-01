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

struct Point {
    double x;
    double y;
    double t;
};

double parse_number_after(const std::string& text, const std::string& key, std::size_t& pos) {
    const std::size_t key_pos = text.find(key, pos);
    if (key_pos == std::string::npos) {
        throw std::runtime_error("missing key");
    }
    const std::size_t colon = text.find(':', key_pos + key.size());
    if (colon == std::string::npos) {
        throw std::runtime_error("missing colon");
    }
    const char* begin = text.c_str() + colon + 1;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (begin == end) {
        throw std::runtime_error("invalid number");
    }
    pos = static_cast<std::size_t>(end - text.c_str());
    return value;
}

std::vector<Point> parse_points(const std::string& text) {
    std::vector<Point> points;
    std::size_t pos = text.find("\"points\"");
    if (pos == std::string::npos) {
        throw std::runtime_error("missing points");
    }
    while (true) {
        const std::size_t next = text.find("\"x\"", pos);
        if (next == std::string::npos) {
            break;
        }
        pos = next;
        Point p{};
        p.x = parse_number_after(text, "\"x\"", pos);
        p.y = parse_number_after(text, "\"y\"", pos);
        p.t = parse_number_after(text, "\"t\"", pos);
        points.push_back(p);
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

double finite_or(double value, double fallback = 0.0) {
    return std::isfinite(value) ? value : fallback;
}

double clamp01(double v) {
    if (v < 0.0) {
        return 0.0;
    }
    if (v > 1.0) {
        return 1.0;
    }
    return v;
}

bool on_boundary(double x, double y) {
    constexpr double eps = 1.0e-13;
    return x <= eps || x >= 1.0 - eps || y <= eps || y >= 1.0 - eps;
}

std::vector<double> representative_times(double tmax, const std::vector<Point>& points) {
    std::vector<double> times;
    times.push_back(0.0);
    if (tmax > 0.0) {
        const double fractions[] = {0.125, 0.25, 0.5, 0.75, 0.875, 1.0};
        for (double f : fractions) {
            times.push_back(f * tmax);
        }
        std::vector<double> query_times;
        query_times.reserve(points.size());
        for (const Point& p : points) {
            if (std::isfinite(p.t) && p.t > 0.0) {
                query_times.push_back(std::min(p.t, tmax));
            }
        }
        std::sort(query_times.begin(), query_times.end());
        const std::size_t limit = std::min<std::size_t>(query_times.size(), 8);
        for (std::size_t k = 0; k < limit; ++k) {
            const std::size_t idx = (limit == 1) ? 0 : k * (query_times.size() - 1) / (limit - 1);
            times.push_back(query_times[idx]);
        }
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) {
                    return std::abs(a - b) <= 1.0e-12 * std::max(1.0, std::max(std::abs(a), std::abs(b)));
                }),
                times.end());
    return times;
}

struct Resolution {
    int n;
    double dt;
};

struct VariationEstimate {
    double required_n = 32.0;

    void add_series(const std::vector<double>& values, double scale_hint, double spacing) {
        if (values.size() < 3 || !(spacing > 0.0)) {
            return;
        }
        double scale = std::max(1.0, std::abs(scale_hint));
        for (double v : values) {
            scale = std::max(scale, std::abs(v));
        }
        double max_second = 0.0;
        for (std::size_t i = 1; i + 1 < values.size(); ++i) {
            max_second = std::max(max_second, std::abs(values[i + 1] - 2.0 * values[i] + values[i - 1]));
        }
        if (max_second > 0.0) {
            const double f2 = max_second / (spacing * spacing);
            const double req = std::sqrt(f2 / (8.0 * 0.0035 * scale));
            required_n = std::max(required_n, req);
        }
    }

    void add_grid(const std::vector<double>& values, int s, double scale_hint) {
        if (s < 3 || static_cast<int>(values.size()) != s * s) {
            return;
        }
        const double h = 1.0 / static_cast<double>(s - 1);
        double scale = std::max(1.0, std::abs(scale_hint));
        for (double v : values) {
            scale = std::max(scale, std::abs(v));
        }

        double max_second = 0.0;
        for (int j = 0; j < s; ++j) {
            for (int i = 1; i + 1 < s; ++i) {
                const double v = values[j * s + i + 1] - 2.0 * values[j * s + i] + values[j * s + i - 1];
                max_second = std::max(max_second, std::abs(v));
            }
        }
        for (int j = 1; j + 1 < s; ++j) {
            for (int i = 0; i < s; ++i) {
                const double v = values[(j + 1) * s + i] - 2.0 * values[j * s + i] + values[(j - 1) * s + i];
                max_second = std::max(max_second, std::abs(v));
            }
        }
        if (max_second > 0.0) {
            const double f2 = max_second / (h * h);
            const double req = std::sqrt(f2 / (8.0 * 0.0035 * scale));
            required_n = std::max(required_n, req);
        }
    }
};

double sample_max_diffusivity() {
    const double rho = std::max(1.0e-12, std::abs(finite_or(rho_cp(), 1.0)));
    double max_k = 1.0e-12;
    constexpr int s = 129;
    for (int i = 0; i < s; ++i) {
        const double x = static_cast<double>(i) / static_cast<double>(s - 1);
        max_k = std::max(max_k, std::abs(finite_or(conductivity_x(x), 1.0)));
        max_k = std::max(max_k, std::abs(finite_or(conductivity_y(x), 1.0)));
    }
    return max_k / rho;
}

int choose_grid_size(double tmax, const std::vector<Point>& points) {
    if (!(tmax > 0.0)) {
        return 32;
    }

    VariationEstimate estimate;
    constexpr int s = 35;
    std::vector<double> grid(s * s);

    for (int j = 0; j < s; ++j) {
        const double y = static_cast<double>(j) / static_cast<double>(s - 1);
        for (int i = 0; i < s; ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(s - 1);
            grid[j * s + i] = finite_or(initial_temp(x, y));
        }
    }
    estimate.add_grid(grid, s, 1.0);

    const std::vector<double> times = representative_times(tmax, points);
    const double rho = std::max(1.0e-12, std::abs(finite_or(rho_cp(), 1.0)));
    for (double t : times) {
        for (int j = 0; j < s; ++j) {
            const double y = static_cast<double>(j) / static_cast<double>(s - 1);
            for (int i = 0; i < s; ++i) {
                const double x = static_cast<double>(i) / static_cast<double>(s - 1);
                grid[j * s + i] = (tmax / rho) * finite_or(heat_source(x, y, t));
            }
        }
        estimate.add_grid(grid, s, 1.0);
    }

    constexpr int edge_s = 65;
    std::vector<double> edge(edge_s);
    const double edge_h = 1.0 / static_cast<double>(edge_s - 1);
    for (double t : times) {
        for (int edge_id = 0; edge_id < 4; ++edge_id) {
            for (int q = 0; q < edge_s; ++q) {
                const double z = static_cast<double>(q) / static_cast<double>(edge_s - 1);
                double x = z;
                double y = 0.0;
                if (edge_id == 1) {
                    y = 1.0;
                } else if (edge_id == 2) {
                    x = 0.0;
                    y = z;
                } else if (edge_id == 3) {
                    x = 1.0;
                    y = z;
                }
                edge[q] = finite_or(boundary_value(x, y, t));
            }
            estimate.add_series(edge, 1.0, edge_h);
        }
    }

    std::vector<double> coeff(edge_s);
    for (int i = 0; i < edge_s; ++i) {
        const double x = static_cast<double>(i) / static_cast<double>(edge_s - 1);
        coeff[i] = finite_or(conductivity_x(x), 1.0);
    }
    estimate.add_series(coeff, 1.0, edge_h);
    for (int i = 0; i < edge_s; ++i) {
        const double y = static_cast<double>(i) / static_cast<double>(edge_s - 1);
        coeff[i] = finite_or(conductivity_y(y), 1.0);
    }
    estimate.add_series(coeff, 1.0, edge_h);

    double min_boundary_distance = 1.0;
    for (const Point& p : points) {
        if (p.t > 0.0) {
            const double x = clamp01(p.x);
            const double y = clamp01(p.y);
            if (!on_boundary(x, y)) {
                min_boundary_distance = std::min(min_boundary_distance, std::min(std::min(x, 1.0 - x), std::min(y, 1.0 - y)));
            }
        }
    }

    int n = 56;
    const double req = estimate.required_n;
    if (req > 42.0) {
        n = 72;
    }
    if (req > 68.0) {
        n = 88;
    }
    if (req > 95.0) {
        n = 112;
    }
    if (req > 135.0) {
        n = 144;
    }
    if (req > 190.0) {
        n = 176;
    }
    if (min_boundary_distance < 0.025) {
        n = std::max(n, 88);
    }
    if (min_boundary_distance < 0.0125) {
        n = std::max(n, 120);
    }
    return std::max(24, std::min(192, n));
}

double choose_time_step(double tmax, int n, const std::vector<Point>& points) {
    if (!(tmax > 0.0)) {
        return 1.0;
    }

    const double alpha = std::max(1.0e-12, sample_max_diffusivity());
    const double h = 1.0 / static_cast<double>(std::max(1, n));
    double dt = std::min(0.025, 0.55 * h / std::max(0.35, std::sqrt(alpha)));

    std::vector<std::pair<double, double> > source_points;
    const double locs[] = {0.17, 0.31, 0.5, 0.69, 0.83};
    for (double x : locs) {
        for (double y : locs) {
            if (source_points.size() < 13) {
                source_points.emplace_back(x, y);
            }
        }
    }

    std::vector<std::pair<double, double> > boundary_points;
    const double edge_locs[] = {0.0, 0.19, 0.37, 0.5, 0.73, 0.91, 1.0};
    for (double z : edge_locs) {
        boundary_points.emplace_back(z, 0.0);
        boundary_points.emplace_back(z, 1.0);
        boundary_points.emplace_back(0.0, z);
        boundary_points.emplace_back(1.0, z);
    }

    const double rho = std::max(1.0e-12, std::abs(finite_or(rho_cp(), 1.0)));
    constexpr int samples = 257;
    const double sample_dt = tmax / static_cast<double>(samples - 1);
    if (sample_dt > 0.0) {
        std::vector<double> prev;
        std::vector<double> cur;
        std::vector<double> prev_delta;
        prev.reserve(source_points.size() + boundary_points.size());
        cur.reserve(source_points.size() + boundary_points.size());
        prev_delta.reserve(source_points.size() + boundary_points.size());
        double scale = 1.0;
        double max_slope = 0.0;
        double max_second = 0.0;

        auto fill_values = [&](double t, std::vector<double>& out) {
            out.clear();
            for (const auto& xy : source_points) {
                const double v = tmax * finite_or(heat_source(xy.first, xy.second, t)) / rho;
                out.push_back(v);
            }
            for (const auto& xy : boundary_points) {
                out.push_back(finite_or(boundary_value(xy.first, xy.second, t)));
            }
        };

        fill_values(0.0, prev);
        for (double v : prev) {
            scale = std::max(scale, std::abs(v));
        }
        for (int s = 1; s < samples; ++s) {
            const double t = tmax * static_cast<double>(s) / static_cast<double>(samples - 1);
            fill_values(t, cur);
            if (cur.size() == prev.size()) {
                for (std::size_t i = 0; i < cur.size(); ++i) {
                    scale = std::max(scale, std::abs(cur[i]));
                    const double delta = cur[i] - prev[i];
                    max_slope = std::max(max_slope, std::abs(delta) / sample_dt);
                    if (!prev_delta.empty()) {
                        max_second = std::max(max_second, std::abs(delta - prev_delta[i]) / (sample_dt * sample_dt));
                    }
                }
                prev_delta.resize(cur.size());
                for (std::size_t i = 0; i < cur.size(); ++i) {
                    prev_delta[i] = cur[i] - prev[i];
                }
            }
            prev.swap(cur);
        }
        if (max_slope > 0.0) {
            dt = std::min(dt, 0.030 * scale / max_slope);
        }
        if (max_second > 0.0) {
            dt = std::min(dt, std::sqrt(0.010 * scale / max_second));
        }
    }

    std::vector<double> positive_times;
    positive_times.reserve(points.size());
    for (const Point& p : points) {
        if (std::isfinite(p.t) && p.t > 0.0) {
            positive_times.push_back(p.t);
        }
    }
    if (!positive_times.empty()) {
        std::sort(positive_times.begin(), positive_times.end());
        const double first_t = positive_times.front();
        if (first_t < 0.05 * tmax) {
            dt = std::min(dt, std::max(first_t / 6.0, tmax / 4000.0));
        }
    }

    dt = std::min(dt, tmax / 45.0);
    dt = std::max(dt, tmax / 5000.0);
    return dt;
}

struct Tridiagonal {
    std::vector<double> denom;
    std::vector<double> cprime;

    Tridiagonal() = default;

    Tridiagonal(const std::vector<double>& lower, const std::vector<double>& diag, const std::vector<double>& upper) {
        build(lower, diag, upper);
    }

    void build(const std::vector<double>& lower, const std::vector<double>& diag, const std::vector<double>& upper) {
        const int n = static_cast<int>(diag.size());
        denom.assign(n, 0.0);
        cprime.assign(std::max(0, n - 1), 0.0);
        if (n == 0) {
            return;
        }
        denom[0] = diag[0];
        if (std::abs(denom[0]) < 1.0e-300) {
            denom[0] = (denom[0] < 0.0) ? -1.0e-300 : 1.0e-300;
        }
        if (n > 1) {
            cprime[0] = upper[0] / denom[0];
        }
        for (int i = 1; i < n; ++i) {
            denom[i] = diag[i] - lower[i - 1] * cprime[i - 1];
            if (std::abs(denom[i]) < 1.0e-300) {
                denom[i] = (denom[i] < 0.0) ? -1.0e-300 : 1.0e-300;
            }
            if (i + 1 < n) {
                cprime[i] = upper[i] / denom[i];
            }
        }
    }

    void solve(const std::vector<double>& lower, const std::vector<double>& rhs, std::vector<double>& out) const {
        const int n = static_cast<int>(rhs.size());
        out.assign(n, 0.0);
        if (n == 0) {
            return;
        }
        std::vector<double> dprime(n, 0.0);
        dprime[0] = rhs[0] / denom[0];
        for (int i = 1; i < n; ++i) {
            dprime[i] = (rhs[i] - lower[i - 1] * dprime[i - 1]) / denom[i];
        }
        out[n - 1] = dprime[n - 1];
        for (int i = n - 2; i >= 0; --i) {
            out[i] = dprime[i] - cprime[i] * out[i + 1];
        }
    }
};

class HeatSolver {
public:
    HeatSolver(int intervals, double rho_value)
        : n_(intervals),
          m_(intervals + 1),
          h_(1.0 / static_cast<double>(intervals)),
          rho_(std::max(1.0e-12, std::abs(rho_value))),
          u_(m_ * m_, 0.0),
          work_(m_ * m_, 0.0),
          next_(m_ * m_, 0.0),
          ax_(intervals + 1, 0.0),
          ay_(intervals + 1, 0.0),
          xs_(intervals + 1, 0.0),
          ys_(intervals + 1, 0.0) {
        for (int i = 0; i <= n_; ++i) {
            xs_[i] = static_cast<double>(i) * h_;
            ys_[i] = static_cast<double>(i) * h_;
        }
        for (int i = 1; i <= n_; ++i) {
            const double xf = (static_cast<double>(i) - 0.5) * h_;
            const double yf = (static_cast<double>(i) - 0.5) * h_;
            ax_[i] = std::max(1.0e-14, std::abs(finite_or(conductivity_x(xf), 1.0))) / (rho_ * h_ * h_);
            ay_[i] = std::max(1.0e-14, std::abs(finite_or(conductivity_y(yf), 1.0))) / (rho_ * h_ * h_);
        }
        initialize();
    }

    void advance(double t0, double t1) {
        const double dt = t1 - t0;
        if (!(dt > 0.0)) {
            set_boundaries(t1, u_);
            return;
        }
        const double r = 0.5 * dt;
        const double tm = 0.5 * (t0 + t1);
        set_boundaries(t0, u_);
        set_boundaries(tm, work_);
        set_boundaries(t1, next_);

        const int interior = n_ - 1;
        std::vector<double> lower_x(std::max(0, interior - 1), 0.0);
        std::vector<double> diag_x(std::max(0, interior), 0.0);
        std::vector<double> upper_x(std::max(0, interior - 1), 0.0);
        std::vector<double> lower_y(std::max(0, interior - 1), 0.0);
        std::vector<double> diag_y(std::max(0, interior), 0.0);
        std::vector<double> upper_y(std::max(0, interior - 1), 0.0);

        for (int i = 1; i <= interior; ++i) {
            diag_x[i - 1] = 1.0 + r * (ax_[i] + ax_[i + 1]);
            if (i > 1) {
                lower_x[i - 2] = -r * ax_[i];
            }
            if (i < interior) {
                upper_x[i - 1] = -r * ax_[i + 1];
            }
        }
        for (int j = 1; j <= interior; ++j) {
            diag_y[j - 1] = 1.0 + r * (ay_[j] + ay_[j + 1]);
            if (j > 1) {
                lower_y[j - 2] = -r * ay_[j];
            }
            if (j < interior) {
                upper_y[j - 1] = -r * ay_[j + 1];
            }
        }
        Tridiagonal solve_x(lower_x, diag_x, upper_x);
        Tridiagonal solve_y(lower_y, diag_y, upper_y);

        std::vector<double> rhs(interior, 0.0);
        std::vector<double> line;
        for (int j = 1; j <= interior; ++j) {
            const double y = ys_[j];
            for (int i = 1; i <= interior; ++i) {
                const double ly = ay_[j] * (u_[idx(i, j - 1)] - u_[idx(i, j)])
                                + ay_[j + 1] * (u_[idx(i, j + 1)] - u_[idx(i, j)]);
                double value = u_[idx(i, j)] + r * ly
                             + r * finite_or(heat_source(xs_[i], y, tm)) / rho_;
                if (i == 1) {
                    value += r * ax_[i] * finite_or(boundary_value(0.0, y, tm));
                }
                if (i == interior) {
                    value += r * ax_[i + 1] * finite_or(boundary_value(1.0, y, tm));
                }
                rhs[i - 1] = value;
            }
            solve_x.solve(lower_x, rhs, line);
            for (int i = 1; i <= interior; ++i) {
                work_[idx(i, j)] = line[i - 1];
            }
        }

        for (int i = 1; i <= interior; ++i) {
            const double x = xs_[i];
            for (int j = 1; j <= interior; ++j) {
                const double lx = ax_[i] * (work_[idx(i - 1, j)] - work_[idx(i, j)])
                                + ax_[i + 1] * (work_[idx(i + 1, j)] - work_[idx(i, j)]);
                double value = work_[idx(i, j)] + r * lx
                             + r * finite_or(heat_source(x, ys_[j], tm)) / rho_;
                if (j == 1) {
                    value += r * ay_[j] * finite_or(boundary_value(x, 0.0, t1));
                }
                if (j == interior) {
                    value += r * ay_[j + 1] * finite_or(boundary_value(x, 1.0, t1));
                }
                rhs[j - 1] = value;
            }
            solve_y.solve(lower_y, rhs, line);
            for (int j = 1; j <= interior; ++j) {
                next_[idx(i, j)] = line[j - 1];
            }
        }

        u_.swap(next_);
        set_boundaries(t1, u_);
    }

    double sample(double x, double y, double t) const {
        x = clamp01(x);
        y = clamp01(y);
        if (on_boundary(x, y)) {
            return finite_or(boundary_value(x, y, t));
        }
        const double gx = x * static_cast<double>(n_);
        const double gy = y * static_cast<double>(n_);
        if (n_ >= 4) {
            int i0 = static_cast<int>(std::floor(gx)) - 1;
            int j0 = static_cast<int>(std::floor(gy)) - 1;
            i0 = std::max(0, std::min(n_ - 3, i0));
            j0 = std::max(0, std::min(n_ - 3, j0));
            const double tx = gx - static_cast<double>(i0);
            const double ty = gy - static_cast<double>(j0);
            double rows[4];
            for (int jj = 0; jj < 4; ++jj) {
                const double v0 = u_[idx(i0, j0 + jj)];
                const double v1 = u_[idx(i0 + 1, j0 + jj)];
                const double v2 = u_[idx(i0 + 2, j0 + jj)];
                const double v3 = u_[idx(i0 + 3, j0 + jj)];
                rows[jj] = lagrange4(v0, v1, v2, v3, tx);
            }
            const double value = lagrange4(rows[0], rows[1], rows[2], rows[3], ty);
            if (std::isfinite(value)) {
                return value;
            }
        }
        int i = static_cast<int>(std::floor(gx));
        int j = static_cast<int>(std::floor(gy));
        if (i < 0) {
            i = 0;
        }
        if (j < 0) {
            j = 0;
        }
        if (i >= n_) {
            i = n_ - 1;
        }
        if (j >= n_) {
            j = n_ - 1;
        }
        const double sx = gx - static_cast<double>(i);
        const double sy = gy - static_cast<double>(j);
        const double v00 = u_[idx(i, j)];
        const double v10 = u_[idx(i + 1, j)];
        const double v01 = u_[idx(i, j + 1)];
        const double v11 = u_[idx(i + 1, j + 1)];
        const double vx0 = (1.0 - sx) * v00 + sx * v10;
        const double vx1 = (1.0 - sx) * v01 + sx * v11;
        return finite_or((1.0 - sy) * vx0 + sy * vx1);
    }

private:
    int n_;
    int m_;
    double h_;
    double rho_;
    std::vector<double> u_;
    std::vector<double> work_;
    std::vector<double> next_;
    std::vector<double> ax_;
    std::vector<double> ay_;
    std::vector<double> xs_;
    std::vector<double> ys_;

    int idx(int i, int j) const {
        return j * m_ + i;
    }

    static double lagrange4(double v0, double v1, double v2, double v3, double t) {
        const double l0 = -((t - 1.0) * (t - 2.0) * (t - 3.0)) / 6.0;
        const double l1 = (t * (t - 2.0) * (t - 3.0)) / 2.0;
        const double l2 = -(t * (t - 1.0) * (t - 3.0)) / 2.0;
        const double l3 = (t * (t - 1.0) * (t - 2.0)) / 6.0;
        return l0 * v0 + l1 * v1 + l2 * v2 + l3 * v3;
    }

    void initialize() {
        for (int j = 0; j <= n_; ++j) {
            for (int i = 0; i <= n_; ++i) {
                const double x = xs_[i];
                const double y = ys_[j];
                if (i == 0 || i == n_ || j == 0 || j == n_) {
                    u_[idx(i, j)] = finite_or(boundary_value(x, y, 0.0), finite_or(initial_temp(x, y)));
                } else {
                    u_[idx(i, j)] = finite_or(initial_temp(x, y));
                }
            }
        }
        work_ = u_;
        next_ = u_;
    }

    void set_boundaries(double t, std::vector<double>& a) const {
        for (int i = 0; i <= n_; ++i) {
            const double x = xs_[i];
            a[idx(i, 0)] = finite_or(boundary_value(x, 0.0, t));
            a[idx(i, n_)] = finite_or(boundary_value(x, 1.0, t));
        }
        for (int j = 0; j <= n_; ++j) {
            const double y = ys_[j];
            a[idx(0, j)] = finite_or(boundary_value(0.0, y, t));
            a[idx(n_, j)] = finite_or(boundary_value(1.0, y, t));
        }
    }
};

std::vector<double> solve_queries(const std::vector<Point>& points) {
    std::vector<double> result(points.size(), 0.0);
    if (points.empty()) {
        return result;
    }

    double tmax = 0.0;
    for (const Point& p : points) {
        if (std::isfinite(p.t)) {
            tmax = std::max(tmax, p.t);
        }
    }
    tmax = std::max(0.0, tmax);

    if (!(tmax > 0.0)) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            const double x = clamp01(points[i].x);
            const double y = clamp01(points[i].y);
            if (on_boundary(x, y)) {
                result[i] = finite_or(boundary_value(x, y, 0.0));
            } else {
                result[i] = finite_or(initial_temp(x, y));
            }
        }
        return result;
    }

    const int n = choose_grid_size(tmax, points);
    const double dt_base = choose_time_step(tmax, n, points);
    HeatSolver solver(n, finite_or(rho_cp(), 1.0));

    std::vector<std::size_t> order(points.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const double ta = std::isfinite(points[a].t) ? points[a].t : 0.0;
        const double tb = std::isfinite(points[b].t) ? points[b].t : 0.0;
        return ta < tb;
    });

    double current = 0.0;
    std::size_t pos = 0;
    while (pos < order.size()) {
        const double raw_t = std::isfinite(points[order[pos]].t) ? points[order[pos]].t : 0.0;
        const double target = std::max(0.0, raw_t);
        if (target > current) {
            const int steps = std::max(1, static_cast<int>(std::ceil((target - current) / dt_base)));
            const double dt = (target - current) / static_cast<double>(steps);
            for (int s = 0; s < steps; ++s) {
                const double next_t = (s + 1 == steps) ? target : current + dt;
                solver.advance(current, next_t);
                current = next_t;
            }
        }

        std::size_t end = pos + 1;
        while (end < order.size()) {
            const double other_t = std::max(0.0, std::isfinite(points[order[end]].t) ? points[order[end]].t : 0.0);
            if (std::abs(other_t - target) > 1.0e-12 * std::max(1.0, std::abs(target))) {
                break;
            }
            ++end;
        }
        for (std::size_t k = pos; k < end; ++k) {
            const std::size_t idx = order[k];
            const double x = clamp01(points[idx].x);
            const double y = clamp01(points[idx].y);
            if (target <= 0.0) {
                result[idx] = on_boundary(x, y) ? finite_or(boundary_value(x, y, 0.0)) : finite_or(initial_temp(x, y));
            } else {
                result[idx] = solver.sample(x, y, target);
            }
            if (!std::isfinite(result[idx])) {
                result[idx] = 0.0;
            }
        }
        pos = end;
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: solver <queries.json> <predictions.json>\n";
        return 2;
    }
    try {
        const std::vector<Point> points = parse_points(read_file(argv[1]));
        const std::vector<double> temperatures = solve_queries(points);
        std::ofstream out(argv[2]);
        if (!out) {
            throw std::runtime_error("cannot open output");
        }
        out << "{\"temperatures\":[";
        for (std::size_t i = 0; i < temperatures.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            double value = temperatures[i];
            if (!std::isfinite(value)) {
                value = 0.0;
            }
            out << std::setprecision(17) << value;
        }
        out << "]}\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "solution failed: " << ex.what() << "\n";
        return 1;
    }
}
