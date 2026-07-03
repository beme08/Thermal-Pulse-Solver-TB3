// Transient 2D anisotropic heat-diffusion solver.
//
// PDE on the unit square [0,1]x[0,1]:
//     rho_cp * dT/dt = d/dx( kx(x) dT/dx ) + d/dy( ky(y) dT/dy ) + Q(x,y,t)
// Dirichlet boundary:  T = boundary_value(x,y,t)
// Initial condition:   T = initial_temp(x,y)
//
// Time integration.  Because kx depends only on x and ky only on y, the spatial
// operator is separable (L = Lx + Ly). We march with a Douglas-Gunn ADI scheme
// (approximate factorization of a theta-method): unconditionally stable, second
// order in space, each stage a set of independent tridiagonal solves (Thomas
// algorithm reusing a single factorization per (dt,theta)) -> O(N^2) per step.
// The first steps use backward Euler (theta=1, L-stable "Rannacher startup") to
// damp any initial/boundary mismatch, then Crank-Nicolson (theta=1/2).
//
// Output.  We march on the physics-driven (adaptive) step size and serve each
// query time by quadratic-in-time interpolation between snapshots, so the step
// count is independent of the number of distinct query times.
//
// Adaptive resolution (the crux of the wall-clock budget).  We probe the oracle
// and size each case to what it actually needs:
//   * Grid N from the spatial complexity of the source/boundary/initial fields,
//     plus a boundary-layer term for near-wall early-time queries when there is
//     an initial/boundary mismatch (a feature the input fields do not reveal).
//   * Locally graded time steps from the temporal structure of the source AND
//     boundary data (which may hide fast oscillations/pulses/ramps), with a
//     geometric mesh near t=0 for the self-similar transient of a sudden jump.
// Cheap cases stay cheap; only genuinely hard features raise the resolution.

#include "thermal_oracle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
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

// ----------------------------- JSON parsing -----------------------------

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

// ----------------------------- Tridiagonal solver -----------------------------
// Factor once (matrix reused across all parallel lines), solve many RHS.
struct Tri {
    int n = 0;
    std::vector<double> a;     // sub-diagonal (a[0] unused)
    std::vector<double> cpr;   // c'_k from forward elimination
    std::vector<double> invb;  // 1 / pivot

    void factor(const std::vector<double>& aa, const std::vector<double>& bb,
                const std::vector<double>& cc) {
        n = static_cast<int>(bb.size());
        a = aa;
        cpr.assign(n, 0.0);
        invb.assign(n, 0.0);
        if (n == 0) return;
        double m = bb[0];
        invb[0] = 1.0 / m;
        if (n > 1) cpr[0] = cc[0] / m;
        for (int k = 1; k < n; ++k) {
            m = bb[k] - aa[k] * cpr[k - 1];
            invb[k] = 1.0 / m;
            if (k < n - 1) cpr[k] = cc[k] / m;
        }
    }
    // Solve in place: d holds RHS on input, solution on output (length n).
    void solve(double* d) const {
        if (n == 0) return;
        d[0] *= invb[0];
        for (int k = 1; k < n; ++k) d[k] = (d[k] - a[k] * d[k - 1]) * invb[k];
        for (int k = n - 2; k >= 0; --k) d[k] -= cpr[k] * d[k + 1];
    }
};

// ----------------------------- Heat solver -----------------------------
struct HeatSolver {
    int Nx, Ny;
    double hx, hy;
    double rc;                    // rho_cp
    std::vector<double> kxf;      // face conductivity, size Nx (face i+1/2)
    std::vector<double> kyf;      // face conductivity, size Ny
    std::vector<double> xs, ys;   // node coordinates

    std::vector<double> u;        // temperature field, (Nx+1)*(Ny+1)
    std::vector<double> dstar;    // intermediate increment
    std::vector<double> delta;    // increment
    std::vector<double> rhs;      // stage-1 RHS (interior)

    // boundary "new" values (at t^{n+1}) along each edge
    std::vector<double> bLeft, bRight;   // size Ny+1 (indexed by j)
    std::vector<double> bBot, bTop;      // size Nx+1 (indexed by i)
    std::vector<double> dLeft, dRight;   // edge increments (size Ny+1)
    std::vector<double> dsLeft, dsRight; // edge intermediate increments (size Ny+1)

    Tri triX, triY;
    double facDt = -1.0, facTheta = -1.0;  // (dt,theta) of cached factorization

    std::vector<double> scratch;  // Thomas RHS scratch
    bool skip_source = false;     // set when the heat source is negligible

    inline int id(int i, int j) const { return j * (Nx + 1) + i; }

    HeatSolver(int nx, int ny) : Nx(nx), Ny(ny) {
        hx = 1.0 / Nx;
        hy = 1.0 / Ny;
        rc = rho_cp();
        if (!(rc > 0.0) || !std::isfinite(rc)) rc = 1.0;
        xs.resize(Nx + 1);
        ys.resize(Ny + 1);
        for (int i = 0; i <= Nx; ++i) xs[i] = i * hx;
        for (int j = 0; j <= Ny; ++j) ys[j] = j * hy;
        kxf.resize(Nx);
        kyf.resize(Ny);
        for (int i = 0; i < Nx; ++i) {
            double k = conductivity_x((i + 0.5) * hx);
            if (!std::isfinite(k) || k < 0.0) k = 0.0;
            kxf[i] = k;
        }
        for (int j = 0; j < Ny; ++j) {
            double k = conductivity_y((j + 0.5) * hy);
            if (!std::isfinite(k) || k < 0.0) k = 0.0;
            kyf[j] = k;
        }
        u.assign((Nx + 1) * (Ny + 1), 0.0);
        dstar = u;
        delta = u;
        rhs.assign((Nx + 1) * (Ny + 1), 0.0);
        bLeft.assign(Ny + 1, 0.0);
        bRight.assign(Ny + 1, 0.0);
        bBot.assign(Nx + 1, 0.0);
        bTop.assign(Nx + 1, 0.0);
        dLeft.assign(Ny + 1, 0.0);
        dRight.assign(Ny + 1, 0.0);
        dsLeft.assign(Ny + 1, 0.0);
        dsRight.assign(Ny + 1, 0.0);
        scratch.assign(std::max(Nx, Ny) + 1, 0.0);
    }

    static inline double safe(double v) { return std::isfinite(v) ? v : 0.0; }

    void set_initial() {
        for (int j = 0; j <= Ny; ++j)
            for (int i = 0; i <= Nx; ++i)
                u[id(i, j)] = safe(initial_temp(xs[i], ys[j]));
    }

    void factorize(double dt, double theta) {
        if (dt == facDt && theta == facTheta) return;
        facDt = dt;
        facTheta = theta;
        // x-direction interior system: unknowns i=1..Nx-1  (n=Nx-1)
        double ax = theta * dt / (rc * hx * hx);
        int nx = Nx - 1;
        std::vector<double> a(nx), b(nx), c(nx);
        for (int k = 0; k < nx; ++k) {
            int i = k + 1;
            a[k] = -ax * kxf[i - 1];
            b[k] = 1.0 + ax * (kxf[i - 1] + kxf[i]);
            c[k] = -ax * kxf[i];
        }
        triX.factor(a, b, c);
        // y-direction interior system: unknowns j=1..Ny-1  (n=Ny-1)
        double ay = theta * dt / (rc * hy * hy);
        int ny = Ny - 1;
        std::vector<double> a2(ny), b2(ny), c2(ny);
        for (int k = 0; k < ny; ++k) {
            int j = k + 1;
            a2[k] = -ay * kyf[j - 1];
            b2[k] = 1.0 + ay * (kyf[j - 1] + kyf[j]);
            c2[k] = -ay * kyf[j];
        }
        triY.factor(a2, b2, c2);
    }

    // One Douglas-Gunn ADI step from t_n to t_n+dt with parameter theta.
    void step(double t_n, double dt, double theta) {
        factorize(dt, theta);
        const double tnp = t_n + dt;
        const double ts = t_n + theta * dt;  // source evaluation time
        const double invx2 = 1.0 / (rc * hx * hx);
        const double invy2 = 1.0 / (rc * hy * hy);

        // New boundary values at t^{n+1}.
        for (int j = 0; j <= Ny; ++j) {
            bLeft[j] = safe(boundary_value(xs[0], ys[j], tnp));
            bRight[j] = safe(boundary_value(xs[Nx], ys[j], tnp));
        }
        for (int i = 0; i <= Nx; ++i) {
            bBot[i] = safe(boundary_value(xs[i], ys[0], tnp));
            bTop[i] = safe(boundary_value(xs[i], ys[Ny], tnp));
        }

        // Stage-1 RHS at interior nodes: dt * ((Lx+Ly)u^n + Q(ts)/rc).
        const double invrc = 1.0 / rc;
        for (int j = 1; j < Ny; ++j) {
            for (int i = 1; i < Nx; ++i) {
                double uij = u[id(i, j)];
                double Lxu = invx2 * (kxf[i] * (u[id(i + 1, j)] - uij) -
                                      kxf[i - 1] * (uij - u[id(i - 1, j)]));
                double Lyu = invy2 * (kyf[j] * (u[id(i, j + 1)] - uij) -
                                      kyf[j - 1] * (uij - u[id(i, j - 1)]));
                double q = skip_source ? 0.0 : safe(heat_source(xs[i], ys[j], ts)) * invrc;
                rhs[id(i, j)] = dt * (Lxu + Lyu + q);
            }
        }

        // Boundary increments Delta = g^{n+1} - u^n on edges.
        // For the intermediate variable at the x-boundaries we use
        //   Delta* = (I - theta*dt*Ly) Delta   (consistent Douglas-Gunn BC).
        // dLeft/dRight include corner nodes so the Ly stencil is defined.
        for (int j = 0; j <= Ny; ++j) {
            dLeft[j] = bLeft[j] - u[id(0, j)];
            dRight[j] = bRight[j] - u[id(Nx, j)];
        }
        for (int j = 1; j < Ny; ++j) {
            double LyL = invy2 * (kyf[j] * (dLeft[j + 1] - dLeft[j]) -
                                  kyf[j - 1] * (dLeft[j] - dLeft[j - 1]));
            double LyR = invy2 * (kyf[j] * (dRight[j + 1] - dRight[j]) -
                                  kyf[j - 1] * (dRight[j] - dRight[j - 1]));
            dsLeft[j] = dLeft[j] - theta * dt * LyL;
            dsRight[j] = dRight[j] - theta * dt * LyR;
        }

        // Stage 1: solve (I - theta*dt*Lx) Delta* = RHS along each interior row.
        double axf = theta * dt / (rc * hx * hx);
        double subX0 = -axf * kxf[0];             // coeff of Delta*_{0,j}
        double supXN = -axf * kxf[Nx - 1];        // coeff of Delta*_{Nx,j}
        int nx = Nx - 1;
        for (int j = 1; j < Ny; ++j) {
            for (int k = 0; k < nx; ++k) scratch[k] = rhs[id(k + 1, j)];
            scratch[0] -= subX0 * dsLeft[j];
            scratch[nx - 1] -= supXN * dsRight[j];
            triX.solve(scratch.data());
            for (int k = 0; k < nx; ++k) dstar[id(k + 1, j)] = scratch[k];
        }

        // Stage 2: solve (I - theta*dt*Ly) Delta = Delta* along each interior col.
        double ayf = theta * dt / (rc * hy * hy);
        double subY0 = -ayf * kyf[0];             // coeff of Delta_{i,0}
        double supYN = -ayf * kyf[Ny - 1];        // coeff of Delta_{i,Ny}
        int ny = Ny - 1;
        for (int i = 1; i < Nx; ++i) {
            double dBot = bBot[i] - u[id(i, 0)];
            double dTop = bTop[i] - u[id(i, Ny)];
            for (int k = 0; k < ny; ++k) scratch[k] = dstar[id(i, k + 1)];
            scratch[0] -= subY0 * dBot;
            scratch[ny - 1] -= supYN * dTop;
            triY.solve(scratch.data());
            for (int k = 0; k < ny; ++k) delta[id(i, k + 1)] = scratch[k];
        }

        // Update interior and set boundary to new Dirichlet values.
        for (int j = 1; j < Ny; ++j)
            for (int i = 1; i < Nx; ++i)
                u[id(i, j)] += delta[id(i, j)];
        for (int j = 0; j <= Ny; ++j) {
            u[id(0, j)] = bLeft[j];
            u[id(Nx, j)] = bRight[j];
        }
        for (int i = 0; i <= Nx; ++i) {
            u[id(i, 0)] = bBot[i];
            u[id(i, Ny)] = bTop[i];
        }
    }

    double interp_field(const std::vector<double>& f, double x, double y) const {
        x = std::min(std::max(x, 0.0), 1.0);
        y = std::min(std::max(y, 0.0), 1.0);
        double fx = x / hx, fy = y / hy;
        int i = static_cast<int>(std::floor(fx));
        int j = static_cast<int>(std::floor(fy));
        if (i >= Nx) i = Nx - 1;
        if (j >= Ny) j = Ny - 1;
        if (i < 0) i = 0;
        if (j < 0) j = 0;
        double tx = fx - i, ty = fy - j;
        double f00 = f[id(i, j)], f10 = f[id(i + 1, j)];
        double f01 = f[id(i, j + 1)], f11 = f[id(i + 1, j + 1)];
        double a = f00 * (1 - tx) + f10 * tx;
        double b = f01 * (1 - tx) + f11 * tx;
        return a * (1 - ty) + b * ty;
    }
    double interp(double x, double y) const { return interp_field(u, x, y); }

    bool finite_field() const {
        for (double v : u)
            if (!std::isfinite(v)) return false;
        return true;
    }
};

// ----------------------------- Adaptive resolution -----------------------------

// Range (max-min) of a scalar field sampled on an m x m grid, and the largest
// bilinear-interpolation residual at cell midpoints (a curvature proxy).
struct FieldProbe {
    double range;
    double maxmid;  // max |value(mid) - avg(4 corners)|
};

template <typename F>
FieldProbe probe_field(F f, int m) {
    double vmin = std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();
    std::vector<double> vals((m + 1) * (m + 1));
    for (int j = 0; j <= m; ++j) {
        for (int i = 0; i <= m; ++i) {
            double v = f(double(i) / m, double(j) / m);
            if (!std::isfinite(v)) v = 0.0;
            vals[j * (m + 1) + i] = v;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
    }
    double maxmid = 0.0;
    for (int j = 0; j < m; ++j) {
        for (int i = 0; i < m; ++i) {
            double c00 = vals[j * (m + 1) + i], c10 = vals[j * (m + 1) + i + 1];
            double c01 = vals[(j + 1) * (m + 1) + i], c11 = vals[(j + 1) * (m + 1) + i + 1];
            double mid = f((i + 0.5) / m, (j + 0.5) / m);
            if (!std::isfinite(mid)) mid = 0.0;
            double avg = 0.25 * (c00 + c10 + c01 + c11);
            maxmid = std::max(maxmid, std::fabs(mid - avg));
        }
    }
    return {vmax - vmin, maxmid};
}

// Multiplier on the internal accuracy targets (1 = default). Smaller -> finer
// discretization; used to build high-resolution references when testing.
double tol_scale() {
    if (const char* e = std::getenv("HEAT_TOL")) {
        double v = std::atof(e);
        if (v > 0) return v;
    }
    return 1.0;
}

// Choose N so the roughest input field (source / boundary / initial) is well
// represented by piecewise-bilinear interpolation on the grid.
int choose_N(double t_probe, double t_max) {
    const int Ncandidates[] = {32, 48, 64, 96, 128, 160, 192, 256};
    const int Nmax = 256, Nmin = 32;
    // Reference scale from the fields themselves.
    auto srcMid = [&](double x, double y) { return heat_source(x, y, t_probe); };
    auto srcEnd = [&](double x, double y) { return heat_source(x, y, t_max); };
    auto bnd = [&](double x, double y) { return boundary_value(x, y, t_probe); };
    auto ini = [&](double x, double y) { return initial_temp(x, y); };

    // Evaluate curvature on a fine probe grid (fine enough to detect the
    // sharpest spatial features; N is extrapolated from the measured residual).
    const int mProbe = 192;
    FieldProbe ps = probe_field(srcMid, mProbe);
    FieldProbe ps2 = probe_field(srcEnd, mProbe);
    FieldProbe pb = probe_field(bnd, mProbe);
    FieldProbe pi = probe_field(ini, mProbe);

    double scale = std::max(std::max(ps.range, ps2.range), std::max(pb.range, pi.range));
    if (!(scale > 0.0)) scale = 1.0;
    double tol = 2e-3 * scale * tol_scale();  // target represention error

    // The midpoint residual scales like h^2 * curvature. Measured at h=1/mProbe,
    // predict the coarsest N whose residual (scaled by (mProbe/N)^2) is under tol.
    double curv = std::max(std::max(ps.maxmid, ps2.maxmid), std::max(pb.maxmid, pi.maxmid));
    for (int N : Ncandidates) {
        double factor = double(mProbe) / double(N);
        double pred = curv * factor * factor;  // residual at grid spacing 1/N
        if (pred <= tol) {
            return std::min(std::max(N, Nmin), Nmax);
        }
    }
    return Nmax;
}

// Overall temperature magnitude, from the initial condition and the boundary
// values sampled over space and time. Used as the reference scale for relative
// tolerances and for boundary-layer sizing.
double temp_scale(double t_max) {
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    const int m = 11;
    for (int j = 0; j <= m; ++j)
        for (int i = 0; i <= m; ++i) {
            double v = initial_temp(double(i) / m, double(j) / m);
            if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
        }
    const double ts[] = {0.0, 0.25, 0.5, 0.75, 1.0};
    for (double tf : ts) {
        double t = tf * t_max;
        for (int i = 0; i <= m; ++i) {
            double c = double(i) / m;
            double vb[] = {boundary_value(c, 0.0, t), boundary_value(c, 1.0, t),
                           boundary_value(0.0, c, t), boundary_value(1.0, c, t)};
            for (double v : vb)
                if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
        }
    }
    double s = hi - lo;
    return (s > 0.0 && std::isfinite(s)) ? s : 1.0;
}

// Maximum magnitude of the heat source over space and time (coarse sample).
double source_magnitude(double t_max) {
    const int m = 6, nt = 6;
    double mx = 0.0;
    for (int s = 0; s <= nt; ++s) {
        double t = t_max * double(s) / nt;
        for (int j = 0; j <= m; ++j)
            for (int i = 0; i <= m; ++i) {
                double q = heat_source(double(i) / m, double(j) / m, t);
                if (std::isfinite(q)) mx = std::max(mx, std::fabs(q));
            }
    }
    return mx;
}

// Largest diffusivity alpha = k / (rho cp) over the domain.
double alpha_max_est() {
    double rc = rho_cp();
    if (!(rc > 0.0) || !std::isfinite(rc)) rc = 1.0;
    double km = 0.0;
    const int m = 64;
    for (int i = 0; i <= m; ++i) {
        double kx = conductivity_x(double(i) / m);
        double ky = conductivity_y(double(i) / m);
        if (std::isfinite(kx)) km = std::max(km, kx);
        if (std::isfinite(ky)) km = std::max(km, ky);
    }
    return km / rc;
}

// Departure of the boundary condition from the initial condition AT time t
// (max over the boundary). The diffusive boundary layer at a near-wall query at
// time t has curvature ~ mismatch(t) / (alpha t); using the mismatch at the
// query's own time correctly distinguishes a sudden jump (mismatch ~ const, so
// sharpness grows as t -> 0) from a slow ramp (mismatch ~ t, bounded sharpness).
double boundary_mismatch_at(double t) {
    const int m = 24;
    double a = 0.0;
    for (int i = 0; i <= m; ++i) {
        double c = double(i) / m;
        double icv[] = {initial_temp(c, 0.0), initial_temp(c, 1.0),
                        initial_temp(0.0, c), initial_temp(1.0, c)};
        double bv[] = {boundary_value(c, 0.0, t), boundary_value(c, 1.0, t),
                       boundary_value(0.0, c, t), boundary_value(1.0, c, t)};
        for (int e = 0; e < 4; ++e)
            if (std::isfinite(bv[e]) && std::isfinite(icv[e]))
                a = std::max(a, std::fabs(bv[e] - icv[e]));
    }
    return a;
}

// Temporal structure of the driving data (source AND boundary values), which
// directly shape the solution's time variation and may hide structure not
// visible from coarse sampling. We sample each densely at several locations and
// record a per-interval piecewise-linear interpolation residual (~ dt^2 *
// curvature), normalized by that driver's own range so the two combine
// scale-invariantly. localDt(t) then returns the largest step resolving the
// data near t, driving graded/locally-adaptive stepping: small steps through
// fast features (pulses, oscillations, ramps), large steps elsewhere.
struct ForcingProfile {
    int Mref;
    double href;
    double tol;
    double maxcurv = 0.0;
    bool active = false;
    std::vector<double> curv;  // size Mref, normalized max residual

    // Accumulate normalized residuals for one sampled time series into curv.
    template <typename F>
    void accumulate(F f, double t_max, std::vector<double>& v) {
        double vmin = std::numeric_limits<double>::infinity();
        double vmax = -std::numeric_limits<double>::infinity();
        for (int k = 0; k <= Mref; ++k) {
            double val = f(t_max * double(k) / Mref);
            if (!std::isfinite(val)) val = 0.0;
            v[k] = val;
            vmin = std::min(vmin, val);
            vmax = std::max(vmax, val);
        }
        double range = vmax - vmin;
        if (!(range > 0.0)) return;  // constant in time -> no constraint
        double inv = 1.0 / range;
        for (int k = 0; k < Mref; ++k) {
            double mid = f(t_max * (k + 0.5) / Mref);
            if (!std::isfinite(mid)) mid = 0.0;
            double r = std::fabs(mid - 0.5 * (v[k] + v[k + 1])) * inv;  // normalized
            if (r > curv[k]) curv[k] = r;
        }
        active = true;
    }

    explicit ForcingProfile(double t_max) {
        Mref = 4096;
        href = t_max / Mref;
        curv.assign(Mref, 0.0);
        std::vector<double> v(Mref + 1);
        // Interior source samples.
        const double xs[] = {0.2, 0.5, 0.8, 0.35, 0.65, 0.12, 0.88};
        const double ys[] = {0.3, 0.5, 0.7, 0.6, 0.25, 0.9, 0.1};
        for (int p = 0; p < 7; ++p)
            accumulate([&](double t) { return heat_source(xs[p], ys[p], t); }, t_max, v);
        // Boundary samples (Dirichlet data drives the solution directly).
        const double bx[] = {0.0, 1.0, 0.0, 1.0, 0.5, 0.5, 0.25, 0.75};
        const double by[] = {0.5, 0.5, 0.25, 0.75, 0.0, 1.0, 0.0, 1.0};
        for (int p = 0; p < 8; ++p)
            accumulate([&](double t) { return boundary_value(bx[p], by[p], t); }, t_max, v);

        for (int k = 0; k < Mref; ++k) maxcurv = std::max(maxcurv, curv[k]);
        tol = 1e-3 * tol_scale();  // dimensionless (residuals are normalized)
    }

    // Largest step near time t that keeps the (normalized) data representation
    // error under tol, scanning curvature over the candidate interval [t, t+dt]
    // (capped by dtCap so the scan and the step stay bounded).
    double localDt(double t, double dtCap) const {
        if (!active || !(maxcurv > 0.0))
            return std::numeric_limits<double>::infinity();
        int k = static_cast<int>(t / href);
        if (k < 0) k = 0;
        if (k >= Mref) k = Mref - 1;
        double c = std::max(curv[k], 1e-300);
        double dt0 = href * std::sqrt(tol / c);
        dt0 = std::min(dt0, dtCap);
        int kw = static_cast<int>((t + dt0) / href) + 1;
        if (kw >= Mref) kw = Mref - 1;
        double cm = c;
        for (int kk = k; kk <= kw; ++kk) cm = std::max(cm, curv[kk]);
        return href * std::sqrt(tol / std::max(cm, 1e-300));
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: solver <queries.json> <predictions.json>\n";
        return 2;
    }
    try {
        std::vector<Point> points = parse_points(read_file(argv[1]));
        const std::size_t nP = points.size();
        std::vector<double> results(nP, 0.0);

        // Split queries into t<=0 (initial condition) and t>0 (needs solve).
        double t_max = 0.0;
        double t_min = std::numeric_limits<double>::infinity();
        for (const auto& p : points) {
            t_max = std::max(t_max, p.t);
            if (p.t > 0.0) t_min = std::min(t_min, p.t);
        }
        if (!std::isfinite(t_min)) t_min = t_max;

        if (t_max <= 0.0) {
            for (std::size_t i = 0; i < nP; ++i)
                results[i] = initial_temp(points[i].x, points[i].y);
        } else {
            double t_probe = 0.5 * t_max;

            // Spatial resolution: input-field complexity, plus the early-time
            // boundary layer implied by any initial/boundary mismatch (which the
            // input fields alone do not reveal). The layer at time t has
            // thickness ~ sqrt(alpha t); a query only needs it resolved if it
            // lies within a few layer thicknesses of a wall, so we size N from
            // the finest such query rather than globally.
            const double scale = temp_scale(t_max);
            const double amax = alpha_max_est();
            double rc_val = rho_cp();
            if (!(rc_val > 0.0) || !std::isfinite(rc_val)) rc_val = 1.0;
            // Skip per-node source evaluation when the source can move the
            // temperature by less than ~1e-6 of its scale over the whole run.
            double srcmag = source_magnitude(t_max);
            bool skip_source =
                srcmag * t_max / (rc_val * std::max(scale, 1e-300)) < 1e-6;

            int N = choose_N(t_probe, t_max);
            {
                int Nbl = 0;
                if (amax > 0.0) {
                    std::map<long long, double> cache;  // boundary mismatch by time
                    for (const auto& p : points) {
                        if (p.t <= 0.0) continue;
                        double d = std::min(std::min(p.x, 1.0 - p.x),
                                            std::min(p.y, 1.0 - p.y));
                        double delta = std::sqrt(amax * p.t);
                        if (d > 4.0 * delta) continue;  // outside boundary-layer zone
                        long long key = static_cast<long long>(p.t / (1e-9 + t_max) * 1e9);
                        auto it = cache.find(key);
                        double mis;
                        if (it == cache.end()) {
                            mis = boundary_mismatch_at(p.t);
                            cache.emplace(key, mis);
                        } else {
                            mis = it->second;
                        }
                        double r = mis / scale;
                        if (r < 0.02) continue;
                        double nq = 28.0 * std::sqrt(r) /
                                    std::sqrt(amax * p.t) / std::sqrt(tol_scale());
                        if (std::isfinite(nq) && nq > Nbl) Nbl = static_cast<int>(nq);
                    }
                }
                N = std::max(N, Nbl);
                N = std::min(N, 256);
                if (std::getenv("HEAT_DEBUG"))
                    std::cerr << "[heat] N=" << N << " Nbl=" << Nbl
                              << " scale=" << scale << " amax=" << amax << "\n";
            }

            // Temporal resolution: local data structure + step-count budget.
            ForcingProfile fp(t_max);
            const int steps_max =         // budget cap on step count
                std::max(200, static_cast<int>(4000.0 / tol_scale()));

            // A sudden initial/boundary mismatch (a jump at t=0+) launches a
            // self-similar diffusive transient that the source/boundary time
            // probes cannot see (the data may be constant in time). Its natural
            // resolution is a geometric mesh dt ~ envGrow*t (constant relative
            // resolution): fine near t=0, coarse later, only ~log-many steps.
            // Detect it and tighten the initial-layer grading accordingly.
            double t_eps = (t_min > 0.0 ? t_min : t_max) * 1e-3;
            double jump0 = boundary_mismatch_at(t_eps);
            bool sudden = (scale > 0.0 && jump0 / scale > 0.03);

            int steps_min = sudden ? 80 : 60;   // >= this many steps across the bulk
            double envGrow = sudden ? 0.05 * std::sqrt(tol_scale()) : 0.5;
            double dt_cap = t_max / steps_min;    // largest step
            double dt_floor = t_max / steps_max;  // smallest (budget) step in the bulk
            // First-step size near t=0. For a sudden transient the geometric mesh
            // keeps the step count log-bounded, so we can start well below the
            // earliest query time to resolve near-wall early-time layers.
            double env0 = sudden ? std::min(dt_floor, t_min / 8.0) : dt_floor;
            int rannacher = 2;                    // backward-Euler startup steps

            if (std::getenv("HEAT_DEBUG")) {
                std::cerr << "[heat] N=" << N << " dt_cap=" << dt_cap
                          << " dt_floor=" << dt_floor << " sudden=" << sudden
                          << " jump0=" << jump0 << " steps_min=" << steps_min
                          << " t_max=" << t_max << "\n";
            }

            // Query times paired with query index, sorted by time (t>0 only).
            // We march on the physics-driven step size and serve each query by
            // interpolating in time between the two bracketing snapshots, so the
            // step count is independent of how many distinct query times exist.
            std::vector<std::pair<double, std::size_t>> qtimes;
            qtimes.reserve(nP);
            for (std::size_t qi = 0; qi < nP; ++qi)
                if (points[qi].t > 0.0) qtimes.emplace_back(points[qi].t, qi);
            std::sort(qtimes.begin(), qtimes.end());
            const double teps = 1e-9 * std::max(1.0, t_max);

            // Build and march the solver, with a robustness fallback.
            for (int attempt = 0; attempt < 2; ++attempt) {
                HeatSolver solver(N, N);
                solver.skip_source = skip_source;
                solver.set_initial();

                // Rolling history of the last three field snapshots (times t0<t1<t2
                // with fields f0,f1,f2) for quadratic-in-time output between steps.
                std::vector<double> f0, f1, f2 = solver.u;
                double t0 = 0.0, t1 = 0.0, t2 = 0.0;
                int nhist = 1;

                double t_cur = 0.0;
                long step_count = 0;
                std::size_t qptr = 0;
                bool ok = true;
                while (qptr < qtimes.size()) {
                    double h = dt_cap;
                    h = std::min(h, fp.localDt(t_cur, dt_cap));  // resolve forcing/data
                    h = std::max(h, dt_floor);                   // budget floor (bulk)
                    // Initial-layer envelope, applied last so it may undercut the
                    // floor near t=0 (the geometric mesh keeps steps bounded).
                    h = std::min(h, env0 + envGrow * t_cur);
                    h = std::min(h, t_max - t_cur);              // do not overshoot
                    if (h <= 0.0) break;
                    double theta = (step_count < rannacher) ? 1.0 : 0.5;
                    solver.step(t_cur, h, theta);
                    double t_new = t_cur + h;
                    if (!solver.finite_field()) { ok = false; break; }

                    // Roll history and store the new snapshot.
                    std::swap(f0, f1);
                    std::swap(f1, f2);
                    f2 = solver.u;
                    t0 = t1; t1 = t_cur; t2 = t_new;
                    nhist = std::min(nhist + 1, 3);

                    // Serve all queries whose time falls in (t_cur, t_new] using
                    // quadratic interpolation through (t0,f0),(t1,f1),(t2,f2)
                    // (linear on the very first step).
                    while (qptr < qtimes.size() && qtimes[qptr].first <= t_new + teps) {
                        double q = qtimes[qptr].first;
                        std::size_t qi = qtimes[qptr].second;
                        double x = points[qi].x, y = points[qi].y;
                        double v1 = solver.interp_field(f1, x, y);
                        double v2 = solver.interp_field(f2, x, y);
                        double res;
                        if (nhist >= 3 && t1 > t0 && t2 > t1) {
                            double v0 = solver.interp_field(f0, x, y);
                            double L0 = (q - t1) * (q - t2) / ((t0 - t1) * (t0 - t2));
                            double L1 = (q - t0) * (q - t2) / ((t1 - t0) * (t1 - t2));
                            double L2 = (q - t0) * (q - t1) / ((t2 - t0) * (t2 - t1));
                            res = v0 * L0 + v1 * L1 + v2 * L2;
                        } else {
                            double frac = (q - t1) / (t2 - t1);
                            frac = std::min(std::max(frac, 0.0), 1.0);
                            res = v1 + frac * (v2 - v1);
                        }
                        results[qi] = res;
                        ++qptr;
                    }
                    t_cur = t_new;
                    ++step_count;
                }

                for (std::size_t qi = 0; qi < nP; ++qi)
                    if (points[qi].t <= 0.0)
                        results[qi] = initial_temp(points[qi].x, points[qi].y);

                if (std::getenv("HEAT_DEBUG"))
                    std::cerr << "[heat] attempt=" << attempt
                              << " steps=" << step_count << " ok=" << ok << "\n";

                bool allfinite = ok;
                for (double v : results)
                    if (!std::isfinite(v)) allfinite = false;
                if (allfinite) break;
                // Fallback: refine steps, more BE damping, retry once.
                dt_cap *= 0.5;
                dt_floor *= 0.5;
                env0 *= 0.5;
                rannacher = 4;
            }
        }

        std::ofstream out(argv[2]);
        if (!out) throw std::runtime_error("cannot open output");
        out << "{\"temperatures\":[";
        for (std::size_t i = 0; i < nP; ++i) {
            if (i != 0) out << ',';
            double v = results[i];
            if (!std::isfinite(v)) v = 0.0;
            out << std::setprecision(17) << v;
        }
        out << "]}\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "solver failed: " << ex.what() << "\n";
        return 1;
    }
}
