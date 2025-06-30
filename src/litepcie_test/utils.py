import logging
from functools import wraps
import time
from contextlib import contextmanager
import cProfile
import timeit
import pstats
import io
import sys
from typing import Callable, Any, Optional

from loguru import logger


def measure_time(func):
    @wraps(func)
    def wrapper(*args, **kwargs):
        start_time = time.perf_counter()
        try:
            result = func(*args, **kwargs)
        except Exception as e:
            logger.exception(e)
            result = None

        end_time = time.perf_counter()

        elapsed_time = end_time - start_time
        hours, remainder = divmod(elapsed_time, 3600)
        minutes, seconds = divmod(remainder, 60)

        time_units = []
        if hours > 0:
            time_units.append(f"{int(hours)}h")
        if minutes > 0:
            time_units.append(f"{int(minutes)}m")
        time_units.append(f"{seconds:.2f}s")

        elapsed_time_str = " ".join(time_units)
        print(f"{func.__name__} - Elapsed time: {elapsed_time_str}")
        try:
            logger.info(f"{func.__name__} - Elapsed time: {elapsed_time_str}")
        except Exception as e:
            print(f"Logger error: {e}")

        return result

    return wrapper


def measure_time_new(func):
    """Decorator to measure the execution time of a function using standard logging."""

    @wraps(func)
    def wrapper(*args, **kwargs):
        # Use a dedicated logger for this decorator
        decorator_logger = logging.getLogger(f"measure_time.{func.__module__}.{func.__name__}")
        decorator_logger.setLevel(logging.INFO)

        # Add a handler if one doesn't exist for this logger
        if not decorator_logger.handlers:
            handler = logging.StreamHandler()
            formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
            handler.setFormatter(formatter)
            decorator_logger.addHandler(handler)

        decorator_logger.info(f"Starting execution of {func.__name__}")
        start_time = time.perf_counter()
        try:
            result = func(*args, **kwargs)
            elapsed = time.perf_counter() - start_time
            decorator_logger.info(f"{func.__name__} completed in {elapsed:.2f} seconds")
            return result
        except Exception as e:
            elapsed = time.perf_counter() - start_time
            decorator_logger.error(f"{func.__name__} failed after {elapsed:.2f} seconds: {e}")
            raise

    return wrapper


@contextmanager
def timer(name: str = "Operation"):
    """Context manager for timing code blocks."""
    logger.info(f"Starting {name}")
    start = time.perf_counter()
    try:
        yield
        elapsed = time.perf_counter() - start
        logger.success(f"{name} completed in {elapsed:.2f}s")
    except Exception as e:
        elapsed = time.perf_counter() - start
        logger.error(f"{name} failed after {elapsed:.2f}s: {e}")
        raise


# 8. Context manager for profiling code blocks
@contextmanager
def profiler(name: str = "Code block", sort_by: str = 'cumulative',
             print_stats: Optional[int] = 30):
    """
    Context manager for profiling code blocks with cProfile.

    Usage:
        with profiler("My operation"):
            # code to profile
    """
    prof = cProfile.Profile()
    logger.info(f"Starting profile: {name}")

    prof.enable()
    try:
        yield prof
        prof.disable()

        # Capture and print stats
        s = io.StringIO()
        stats = pstats.Stats(prof, stream=s).sort_stats(sort_by)
        stats.print_stats(print_stats)

        logger.info(f"\nProfile results for {name}:")
        print(s.getvalue())

    except Exception as e:
        prof.disable()
        logger.error(f"Error during profiling {name}: {e}")
        raise
