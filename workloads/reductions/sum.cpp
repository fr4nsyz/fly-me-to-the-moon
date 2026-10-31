#include "runtime.hpp"
#include <vector>
#include <iostream>

double sum_array(const std::vector<double>& v) {
    double s = 0.0;
    for (size_t i = 0; i < v.size(); ++i) {
        s = sdc::runtime_f64_add(1, s, v[i]);
    }
    return s;
}

int main() {
    std::vector<double> v = {1.0, 2.0, 3.0, 4.0, 5.0};
    double r = sum_array(v);
    std::cout << r << std::endl;
    return 0;
}
