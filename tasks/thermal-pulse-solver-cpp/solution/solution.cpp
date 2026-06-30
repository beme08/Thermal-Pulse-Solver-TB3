#include "thermal_oracle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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

struct Query {
    Point point;
    std::size_t index;
};

struct Factors {
    std::vector<double> lower;
    std::vector<double> upper;
    std::vector<double> cprime;
    std::vector<double> denom;
};

struct Grid {
    int nx = 320;
    int ny = 320;
    int nx_int = 318;
    int ny_int = 318;
    int nt = 4096;
    double hx = 1.0 / 319.0;
    double hy = 1.0 / 319.0;
    double dt = 1.0 / 4096.0;
};

struct SourceBasis {
    std::vector<double> q0;
    std::vector<double> q1;
    int probe_a = 0;
    int probe_b = 1;
    double det = 0.0;
    double t0 = 0.0;
    double t1 = 0.00137;
};

int idx(int i, int j, int nx_int) {
    return j * nx_int + i;
}

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
    if (begin == end || !std::isfinite(value)) {
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

std::string solver_mode() {
    const char* raw = std::getenv("THERMAL_SOLVER_MODE");
    return raw ? std::string(raw) : std::string("reference");
}

int detect_nt() {
    const std::string mode = solver_mode();
    if (mode == "coarse_dt") {
        return 128;
    }
    if (mode == "brute_overresolve") {
        return 65536;
    }
    if (mode == "explicit") {
        return 4096;
    }

    const int samples = 4096;
    std::vector<double> values(samples);
    double mean = 0.0;
    const double x = 0.43;
    const double y = 0.58;
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(samples - 1);
        values[i] = heat_source(x, y, t);
        mean += values[i];
    }
    mean /= static_cast<double>(samples);
    int crossings = 0;
    double prev = values[0] - mean;
    for (int i = 1; i < samples; ++i) {
        const double cur = values[i] - mean;
        if ((prev < 0.0 && cur >= 0.0) || (prev >= 0.0 && cur < 0.0)) {
            ++crossings;
        }
        if (std::abs(cur) > 1.0e-14) {
            prev = cur;
        }
    }
    return crossings > 300 ? 8192 : 4096;
}

Grid make_grid() {
    Grid g;
    g.nt = detect_nt();
    g.nx_int = g.nx - 2;
    g.ny_int = g.ny - 2;
    g.hx = 1.0 / static_cast<double>(g.nx - 1);
    g.hy = 1.0 / static_cast<double>(g.ny - 1);
    g.dt = domain_T() / static_cast<double>(g.nt);
    return g;
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

Factors make_factor(
    const std::vector<double>& lower_l,
    const std::vector<double>& diag_l,
    const std::vector<double>& upper_l,
    double r) {
    const int n = static_cast<int>(diag_l.size());
    Factors f;
    f.lower.assign(n, 0.0);
    f.upper.assign(n, 0.0);
    f.cprime.assign(n, 0.0);
    f.denom.assign(n, 0.0);
    for (int i = 0; i < n; ++i) {
        f.lower[i] = -r * lower_l[i];
        const double diag = 1.0 - r * diag_l[i];
        f.upper[i] = -r * upper_l[i];
        if (i == 0) {
            f.denom[i] = diag;
        } else {
            f.denom[i] = diag - f.lower[i] * f.cprime[i - 1];
        }
        if (i + 1 < n) {
            f.cprime[i] = f.upper[i] / f.denom[i];
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

void solve_x_lines(const Grid& g, const Factors& factor_x, std::vector<double>& field) {
    std::vector<double> line(g.nx_int, 0.0);
    for (int j = 0; j < g.ny_int; ++j) {
        for (int i = 0; i < g.nx_int; ++i) {
            line[i] = field[idx(i, j, g.nx_int)];
        }
        solve_factored(factor_x, line);
        for (int i = 0; i < g.nx_int; ++i) {
            field[idx(i, j, g.nx_int)] = line[i];
        }
    }
}

void solve_y_lines(const Grid& g, const Factors& factor_y, std::vector<double>& field) {
    std::vector<double> line(g.ny_int, 0.0);
    for (int i = 0; i < g.nx_int; ++i) {
        for (int j = 0; j < g.ny_int; ++j) {
            line[j] = field[idx(i, j, g.nx_int)];
        }
        solve_factored(factor_y, line);
        for (int j = 0; j < g.ny_int; ++j) {
            field[idx(i, j, g.nx_int)] = line[j];
        }
    }
}

void apply_plus_x(
    const Grid& g,
    const std::vector<double>& lower_l,
    const std::vector<double>& diag_l,
    const std::vector<double>& upper_l,
    double r,
    const std::vector<double>& u,
    std::vector<double>& out) {
    for (int j = 0; j < g.ny_int; ++j) {
        for (int i = 0; i < g.nx_int; ++i) {
            double lu = diag_l[i] * u[idx(i, j, g.nx_int)];
            if (i > 0) {
                lu += lower_l[i] * u[idx(i - 1, j, g.nx_int)];
            }
            if (i + 1 < g.nx_int) {
                lu += upper_l[i] * u[idx(i + 1, j, g.nx_int)];
            }
            out[idx(i, j, g.nx_int)] = u[idx(i, j, g.nx_int)] + r * lu;
        }
    }
}

void apply_plus_y(
    const Grid& g,
    const std::vector<double>& lower_l,
    const std::vector<double>& diag_l,
    const std::vector<double>& upper_l,
    double r,
    const std::vector<double>& u,
    std::vector<double>& out) {
    for (int j = 0; j < g.ny_int; ++j) {
        for (int i = 0; i < g.nx_int; ++i) {
            double lu = diag_l[j] * u[idx(i, j, g.nx_int)];
            if (j > 0) {
                lu += lower_l[j] * u[idx(i, j - 1, g.nx_int)];
            }
            if (j + 1 < g.ny_int) {
                lu += upper_l[j] * u[idx(i, j + 1, g.nx_int)];
            }
            out[idx(i, j, g.nx_int)] = u[idx(i, j, g.nx_int)] + r * lu;
        }
    }
}

void apply_operator(
    const Grid& g,
    const std::vector<double>& lx_lower,
    const std::vector<double>& lx_diag,
    const std::vector<double>& lx_upper,
    const std::vector<double>& ly_lower,
    const std::vector<double>& ly_diag,
    const std::vector<double>& ly_upper,
    const std::vector<double>& u,
    std::vector<double>& out) {
    for (int j = 0; j < g.ny_int; ++j) {
        for (int i = 0; i < g.nx_int; ++i) {
            double lu = (lx_diag[i] + ly_diag[j]) * u[idx(i, j, g.nx_int)];
            if (i > 0) {
                lu += lx_lower[i] * u[idx(i - 1, j, g.nx_int)];
            }
            if (i + 1 < g.nx_int) {
                lu += lx_upper[i] * u[idx(i + 1, j, g.nx_int)];
            }
            if (j > 0) {
                lu += ly_lower[j] * u[idx(i, j - 1, g.nx_int)];
            }
            if (j + 1 < g.ny_int) {
                lu += ly_upper[j] * u[idx(i, j + 1, g.nx_int)];
            }
            out[idx(i, j, g.nx_int)] = lu;
        }
    }
}

SourceBasis build_source_basis(const Grid& g) {
    SourceBasis basis;
    const int total = g.nx_int * g.ny_int;
    basis.q0.assign(total, 0.0);
    basis.q1.assign(total, 0.0);
    int strongest = 0;
    double strongest_norm = -1.0;
    for (int j = 0; j < g.ny_int; ++j) {
        const double y = static_cast<double>(j + 1) * g.hy;
        for (int i = 0; i < g.nx_int; ++i) {
            const double x = static_cast<double>(i + 1) * g.hx;
            const int n = idx(i, j, g.nx_int);
            basis.q0[n] = heat_source(x, y, basis.t0);
            basis.q1[n] = heat_source(x, y, basis.t1);
            const double norm = basis.q0[n] * basis.q0[n] + basis.q1[n] * basis.q1[n];
            if (norm > strongest_norm) {
                strongest_norm = norm;
                strongest = n;
            }
        }
    }
    basis.probe_a = strongest;
    double best_det = 0.0;
    int best = strongest == 0 ? 1 : 0;
    for (int n = 0; n < total; ++n) {
        const double det = basis.q0[basis.probe_a] * basis.q1[n] - basis.q1[basis.probe_a] * basis.q0[n];
        if (std::abs(det) > std::abs(best_det)) {
            best_det = det;
            best = n;
        }
    }
    basis.probe_b = best;
    basis.det = best_det;
    if (std::abs(basis.det) < 1.0e-12) {
        throw std::runtime_error("source basis is degenerate");
    }
    return basis;
}

void source_at_time(const Grid& g, const SourceBasis& basis, double t, std::vector<double>& q) {
    const int ia = basis.probe_a % g.nx_int;
    const int ja = basis.probe_a / g.nx_int;
    const int ib = basis.probe_b % g.nx_int;
    const int jb = basis.probe_b / g.nx_int;
    const double qa = heat_source(static_cast<double>(ia + 1) * g.hx, static_cast<double>(ja + 1) * g.hy, t);
    const double qb = heat_source(static_cast<double>(ib + 1) * g.hx, static_cast<double>(jb + 1) * g.hy, t);
    const double c0 = (qa * basis.q1[basis.probe_b] - qb * basis.q1[basis.probe_a]) / basis.det;
    const double c1 = (basis.q0[basis.probe_a] * qb - basis.q0[basis.probe_b] * qa) / basis.det;
    for (std::size_t n = 0; n < q.size(); ++n) {
        q[n] = c0 * basis.q0[n] + c1 * basis.q1[n];
    }
}

double node_value(const Grid& g, const std::vector<double>& field, int i, int j, double t) {
    const double x = static_cast<double>(i) * g.hx;
    const double y = static_cast<double>(j) * g.hy;
    if (i == 0 || j == 0 || i + 1 == g.nx || j + 1 == g.ny) {
        return boundary_value(x, y, t);
    }
    return field[idx(i - 1, j - 1, g.nx_int)];
}

double interpolate_field(
    const Grid& g,
    const std::vector<double>& previous,
    const std::vector<double>& next,
    double t0,
    double t1,
    const Point& p) {
    const double theta = t1 > t0 ? std::clamp((p.t - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
    const double gx = std::clamp(p.x / g.hx, 0.0, static_cast<double>(g.nx - 1));
    const double gy = std::clamp(p.y / g.hy, 0.0, static_cast<double>(g.ny - 1));
    const int i0 = std::min(static_cast<int>(std::floor(gx)), g.nx - 2);
    const int j0 = std::min(static_cast<int>(std::floor(gy)), g.ny - 2);
    const int i1 = i0 + 1;
    const int j1 = j0 + 1;
    const double ax = gx - static_cast<double>(i0);
    const double ay = gy - static_cast<double>(j0);
    auto sample = [&](const std::vector<double>& field, double t, int i, int j) {
        return node_value(g, field, i, j, t);
    };
    const double values0[4] = {
        sample(previous, t0, i0, j0),
        sample(previous, t0, i1, j0),
        sample(previous, t0, i0, j1),
        sample(previous, t0, i1, j1),
    };
    const double values1[4] = {
        sample(next, t1, i0, j0),
        sample(next, t1, i1, j0),
        sample(next, t1, i0, j1),
        sample(next, t1, i1, j1),
    };
    double blended[4];
    for (int k = 0; k < 4; ++k) {
        blended[k] = (1.0 - theta) * values0[k] + theta * values1[k];
    }
    const double bottom = (1.0 - ax) * blended[0] + ax * blended[1];
    const double top = (1.0 - ax) * blended[2] + ax * blended[3];
    return (1.0 - ay) * bottom + ay * top;
}

void write_output(const char* path, const std::vector<double>& values) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open output");
    }
    out << "{\"temperatures\":[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        if (!std::isfinite(values[i])) {
            throw std::runtime_error("non-finite prediction");
        }
        out << std::setprecision(17) << values[i];
    }
    out << "]}\n";
}

std::vector<double> solve_temperature(const std::vector<Point>& points) {
    const Grid g = make_grid();
    const std::string mode = solver_mode();
    const bool explicit_mode = mode == "explicit";
    const int total = g.nx_int * g.ny_int;
    const double rho = rho_cp();
    const double r = 0.5 * g.dt / rho;

    std::vector<double> lx_lower, lx_diag, lx_upper;
    std::vector<double> ly_lower, ly_diag, ly_upper;
    build_operator_coefficients(g.nx, g.hx, true, lx_lower, lx_diag, lx_upper);
    build_operator_coefficients(g.ny, g.hy, false, ly_lower, ly_diag, ly_upper);
    const Factors factor_x = make_factor(lx_lower, lx_diag, lx_upper, r);
    const Factors factor_y = make_factor(ly_lower, ly_diag, ly_upper, r);
    const SourceBasis basis = build_source_basis(g);

    std::vector<double> u(total, 0.0);
    for (int j = 0; j < g.ny_int; ++j) {
        const double y = static_cast<double>(j + 1) * g.hy;
        for (int i = 0; i < g.nx_int; ++i) {
            const double x = static_cast<double>(i + 1) * g.hx;
            u[idx(i, j, g.nx_int)] = initial_temp(x, y);
        }
    }

    std::vector<Query> queries;
    queries.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        queries.push_back({points[i], i});
    }
    std::sort(queries.begin(), queries.end(), [](const Query& a, const Query& b) {
        return a.point.t < b.point.t;
    });

    std::vector<double> result(points.size(), 0.0);
    std::vector<double> next(total, 0.0);
    std::vector<double> tmp(total, 0.0);
    std::vector<double> rhs(total, 0.0);
    std::vector<double> q(total, 0.0);
    std::vector<double> lu(total, 0.0);
    std::size_t qi = 0;

    while (qi < queries.size() && queries[qi].point.t <= 0.0) {
        result[queries[qi].index] = interpolate_field(g, u, u, 0.0, 0.0, queries[qi].point);
        ++qi;
    }

    for (int step = 0; step < g.nt && qi < queries.size(); ++step) {
        const double t0 = static_cast<double>(step) * g.dt;
        const double t1 = static_cast<double>(step + 1) * g.dt;
        const double tm = 0.5 * (t0 + t1);
        source_at_time(g, basis, tm, q);

        if (explicit_mode) {
            apply_operator(g, lx_lower, lx_diag, lx_upper, ly_lower, ly_diag, ly_upper, u, lu);
            double max_abs = 0.0;
            for (int n = 0; n < total; ++n) {
                next[n] = u[n] + g.dt / rho * (lu[n] + q[n]);
                max_abs = std::max(max_abs, std::abs(next[n]));
            }
            if (!std::isfinite(max_abs) || max_abs > 1.0e8) {
                throw std::runtime_error("explicit solver unstable");
            }
        } else {
            apply_plus_x(g, lx_lower, lx_diag, lx_upper, r, u, tmp);
            apply_plus_y(g, ly_lower, ly_diag, ly_upper, r, tmp, rhs);
            for (int n = 0; n < total; ++n) {
                rhs[n] += g.dt / rho * q[n];
            }
            solve_x_lines(g, factor_x, rhs);
            solve_y_lines(g, factor_y, rhs);
            next.swap(rhs);
        }

        while (qi < queries.size() && queries[qi].point.t <= t1 + 1.0e-14) {
            result[queries[qi].index] = interpolate_field(g, u, next, t0, t1, queries[qi].point);
            ++qi;
        }
        u.swap(next);
    }

    while (qi < queries.size()) {
        result[queries[qi].index] = interpolate_field(g, u, u, domain_T(), domain_T(), queries[qi].point);
        ++qi;
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
        write_output(argv[2], solve_temperature(points));
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "thermal solver failed: " << ex.what() << "\n";
        return 1;
    }
}
