#include "thermal_oracle.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: solver <queries.json> <predictions.json>\n";
        return 2;
    }
    try {
        const std::vector<Point> points = parse_points(read_file(argv[1]));
        std::ofstream out(argv[2]);
        if (!out) {
            throw std::runtime_error("cannot open output");
        }
        out << "{\"temperatures\":[";
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            out << std::setprecision(17) << 0.0;
        }
        out << "]}\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "starter solution failed: " << ex.what() << "\n";
        return 1;
    }
}
