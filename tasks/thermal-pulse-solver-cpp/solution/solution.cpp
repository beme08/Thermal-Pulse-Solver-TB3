#include "thermal_oracle.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTwoPi = 2.0 * kPi;

struct Point {
    double x;
    double y;
    double t;
};

double instance_freq() {
    const char* raw = std::getenv("THERMAL_INSTANCE");
    return raw && std::atoi(raw) == 1 ? 192.0 : 96.0;
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

double spatial(double x, double y) {
    const double sharp = 36.0;
    const double cx = 0.43;
    const double cy = 0.58;
    const double shape_b = 0.25;
    return std::sin(kPi * x)
        * std::sin(kPi * y)
        * (1.0 + shape_b * std::cos(kTwoPi * x) * std::cos(kTwoPi * y))
        * std::exp(-sharp * ((x - cx) * (x - cx) + (y - cy) * (y - cy)));
}

double temporal(double freq, double t) {
    return (1.0 + 0.05 * t) * (1.0 + 0.75 * std::sin(kTwoPi * freq * t));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: solver <queries.json> <predictions.json>\n";
        return 2;
    }
    try {
        const std::vector<Point> points = parse_points(read_file(argv[1]));
        const double freq = instance_freq();
        std::ofstream out(argv[2]);
        if (!out) {
            throw std::runtime_error("cannot open output");
        }
        out << "{\"temperatures\":[";
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            out << std::setprecision(17) << spatial(points[i].x, points[i].y) * temporal(freq, points[i].t);
        }
        out << "]}\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "reference solution failed: " << ex.what() << "\n";
        return 1;
    }
}
