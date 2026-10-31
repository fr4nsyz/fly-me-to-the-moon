// A plain numerical workload. No annotations, no macros, no runtime calls:
// the instrumentation is added entirely by the build (see docs/architecture.md,
// design principle "Workload source stays plain").
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <vector>

double sum_squares(const std::vector<double>& values) {
    double acc = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        acc = acc + values[i] * values[i];
    }
    return acc;
}

int main(int argc, char** argv) {
    const std::size_t n = (argc > 1) ? static_cast<std::size_t>(std::atol(argv[1])) : 1024;
    std::vector<double> values(n);
    for (std::size_t i = 0; i < n; ++i) {
        values[i] = 1.0 + static_cast<double>(i % 17) * 0.125;
    }
    const double result = sum_squares(values);
    std::cout << result << std::endl;
    return 0;
}
