#include <iostream>
#include <vector>
#include <chrono>
#include <cstdio>
#include <pthread.h>

// A structure to pass arguments to each thread
struct ThreadData {
    int thread_id;
    int num_threads;
    int N;
    double a;
    const std::vector<double>* x;
    std::vector<double>* y;
};

// The function each thread will execute
void* daxpy_thread(void* arg) {
    ThreadData* data = (ThreadData*) arg;
    
    // Calculate equal-sized chunks for each thread
    int chunk_size = data->N / data->num_threads;
    int start = data->thread_id * chunk_size;
    
    // Handle potential remainder elements on the last thread
    int end = (data->thread_id == data->num_threads - 1) ? data->N : start + chunk_size;

    // Local DAXPY loop for this thread's slice of the vectors
    for (int i = start; i < end; i++) {
        (*data->y)[i] = data->a * (*data->x)[i] + (*data->y)[i];
    }

    pthread_exit(nullptr);
}

int main() {
    const int N = 100;      // number of iterations
    const double a = 2.0;       // scalar multiplier
    const int NUM_THREADS = 4;  // Number of cores/threads to utilize

    std::vector<double> x(N);
    std::vector<double> y(N);

    // Initialize vectors (Removed the std::cout print inside the loop for performance)
    for (int i = 0; i < N; i++) {
        x[i] = i * 1.0;
        y[i] = i * 0.5;
    }

    // Initialize pthreads structures
    pthread_t threads[NUM_THREADS];
    std::vector<ThreadData> thread_data(NUM_THREADS);

    // Create threads and assign work
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].num_threads = NUM_THREADS;
        thread_data[i].N = N;
        thread_data[i].a = a;
        thread_data[i].x = &x;
        thread_data[i].y = &y;

        int rc = pthread_create(&threads[i], nullptr, daxpy_thread, (void*)&thread_data[i]);
        if (rc) {
            std::cerr << "Error: Unable to create thread, " << rc << "\n";
            return -1;
        }
    }

    // Wait for all threads to finish (barrier synchronization)
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }

    // Print final value to verify correctness (Output should be: 2 * (N-1) * 1.0 + (N-1) * 0.5)
    // For N=1,000,000, index 999,999 yields 2,499,997
    printf("Daxpy Final Value: %d\n", (int) y[N-1]);

    return 0;
}
