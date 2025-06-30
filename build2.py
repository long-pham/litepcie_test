import time
import timeit
import cProfile
import random
import io
import pstats

# A sample function to measure.
# This function simulates some work by sorting a list of random numbers.
def some_function():
    """
    A sample function that does some work to be measured.
    """
    time.sleep(0.1) # Simulate I/O or other delays
    # Simulate CPU-bound work
    data = [random.random() for _ in range(10000)]
    sorted_data = sorted(data)
    return len(sorted_data)

# --- Method 1: Using time.perf_counter() ---
def measure_with_perf_counter():
    """
    Measures the execution time of a function using time.perf_counter.
    This is a reliable way to measure wall-clock time.
    """
    print("--- 1. Measuring with time.perf_counter() ---")
    start_time = time.perf_counter()
    result = some_function()
    end_time = time.perf_counter()
    print(f"some_function() returned: {result}")
    print(f"Execution time: {end_time - start_time:.4f} seconds")
    print("\n" + "="*50 + "\n")

# --- Method 2: Using the timeit module ---
def measure_with_timeit():
    """
    Uses the timeit module for more accurate benchmarking of small code snippets.
    It runs the code multiple times to get a more stable average.
    """
    print("--- 2. Measuring with timeit ---")
    # Setup code to be run once before the timing starts
    setup_code = "from __main__ import some_function"
    # The statement to be timed
    main_code = "some_function()"

    # Run the test 10 times and get the total time
    number_of_runs = 10
    total_time = timeit.timeit(stmt=main_code, setup=setup_code, number=number_of_runs)
    average_time = total_time / number_of_runs

    print(f"Ran {number_of_runs} times with timeit.")
    print(f"Total time: {total_time:.4f} seconds")
    print(f"Average execution time: {average_time:.4f} seconds")
    print("\n" + "="*50 + "\n")

# --- Method 3: Using cProfile for detailed profiling ---
def measure_with_cprofile():
    """
    Uses cProfile to get a detailed report of where time is spent.
    This is excellent for identifying performance bottlenecks in your code.
    """
    print("--- 3. Profiling with cProfile ---")
    # Create a Profile object
    profiler = cProfile.Profile()

    # Run the function under the profiler
    profiler.enable()
    result = some_function()
    profiler.disable()

    print(f"some_function() returned: {result}")

    # Create a stream to capture the output
    s = io.StringIO()
    # Sort the stats by cumulative time and print to the stream
    stats = pstats.Stats(profiler, stream=s).sort_stats('cumulative')
    stats.print_stats()

    print("cProfile Results:")
    print(s.getvalue())
    print("="*50 + "\n")


if __name__ == "__main__":
    measure_with_perf_counter()
    measure_with_timeit()
    measure_with_cprofile()