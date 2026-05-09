#include <iostream>
#include <vector>
#include <chrono>

int main() {
    const int N = 100000;      // number of iterations
    const double a = 2.5;     // scalar multiplier

    std::vector<double> x(N);
    std::vector<double> y(N);

    // initialize vectors
    for (int i = 0; i < N; i++) {
        x[i] = i * 1.0;
        y[i] = i * 0.5;
    }

    // DAXPY loop: y = a*x + y
    for (int i = 0; i < N; i++) {
        y[i] = a * x[i] + y[i];
    }

    // print one value so compiler doesn't optimize everything away
    std::cout << "y[9999] = " << y[9999] << "\n";

    return 0;
}
