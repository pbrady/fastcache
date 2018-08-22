from __future__ import division

"""The Python interpreter may switch between threads inbetween bytecode
execution.  Bytecode execution in fastcache may occur during:
(1) Calls to make_key which will call the __hash__ methods of the args and
(2) `PyDict_Get(Set)Item` calls rely on Python comparisons (i.e, __eq__)
    to determine if a match has been found

A good test for threadsafety is then to cache a function which takes user
defined Python objects that have __hash__ and __eq__ methods which live in
Python land rather built-in land.

The test should not only ensure that the correct result is acheived (and no
segfaults) but also assess memory leaks.

The thread switching interval can be altered using sys.setswitchinterval.
"""

class PythonInt:
    """ Wrapper for an integer with python versions of __eq__ and __hash__."""

    def __init__(self, val):
        self.value = val

    def __hash__(self):
        return hash(self.value)

    def __eq__(self, other):
        # only compare with other instances of PythonInt
        if not isinstance(other, PythonInt):
            raise TypeError("PythonInt cannot be compared to %s" % type(other))
        return self.value == other.value

from fastcache import clru_cache
#from functools import lru_cache as clru_cache
from random import randint

CACHE_SIZE=301
FIB=CACHE_SIZE-1
RAND_MIN, RAND_MAX = 1, 10

@clru_cache(maxsize=CACHE_SIZE, typed=False)
def fib(n):
    """Terrible Fibonacci number generator."""
    v = n.value
    return v if v < 2 else fib2(PythonInt(v-1)) + fib(PythonInt(v-2))

@clru_cache(maxsize=CACHE_SIZE, typed=False)
def fib2(n):
    """Terrible Fibonacci number generator."""
    v = n.value
    return v if v < 2 else fib(PythonInt(v-1)) + fib2(PythonInt(v-2))



# establish correct result from single threaded exectution
RESULT = fib(PythonInt(FIB))

def run_fib_with_clear(r):
    """ Run Fibonacci generator r times. """
    for i in range(r):
        if randint(RAND_MIN, RAND_MAX) == RAND_MIN:
            fib.cache_clear()
            fib2.cache_clear()
        res = fib(PythonInt(FIB))
        if RESULT != res:
            raise ValueError("Expected %d, Got %d" % (RESULT, res))

def run_fib_with_stats(r):
    """ Run Fibonacci generator r times. """
    for i in range(r):
        res = fib(PythonInt(FIB))
        if RESULT != res:
            raise ValueError("Expected %d, Got %d" % (RESULT, res))

from threading import Thread
try:
    from sys import setswitchinterval as setinterval
except ImportError:
    from sys import setcheckinterval
    def setinterval(i):
        return setcheckinterval(int(i))


def run_threads(threads):
    for t in threads:
        t.start()
    for t in threads:
        t.join()

def run_test(n, r, i):
    """ Run thread safety test with n threads r times using interval i. """
    setinterval(i)
    threads = [Thread(target=run_fib_with_clear, args=(r, )) for _ in range(n)]
    run_threads(threads)

def run_test2(n, r, i):
    """ Run thread safety test to make sure the cache statistics
    are correct."""
    fib.cache_clear()
    setinterval(i)
    threads = [Thread(target=run_fib_with_stats, args=(r, )) for _ in range(n)]
    run_threads(threads)

    hits, misses, maxsize, currsize = fib.cache_info()
    if misses != CACHE_SIZE//2+1:
        raise ValueError("Expected %d misses, Got %d" %
                         (CACHE_SIZE//2+1, misses))
    if maxsize != CACHE_SIZE:
        raise ValueError("Expected %d maxsize, Got %d" %
                         (CACHE_SIZE, maxsize))
    if currsize != CACHE_SIZE//2+1:
        raise ValueError("Expected %d currsize, Got %d" %
                         (CACHE_SIZE//2+1, currsize))

import argparse

def main():
    parser = argparse.ArgumentParser(description='Run threadsafety test.')
    parser.add_argument('-n,--numthreads',
                        type=int,
                        default=2,
                        dest='n',
                        help='Number of threads.')
    parser.add_argument('-r,--repeat',
                        type=int,
                        default=5000,
                        dest='r',
                        help='Number of times to repeat test.  Larger numbers '+
                        'will make it easier to spot memory leaks.')
    parser.add_argument('-i,--interval',
                        type=float,
                        default=1e-6,
                        dest='i',
                        help='Time in seconds for sys.setswitchinterval.')

    run_test(**dict(vars(parser.parse_args())))
    run_test2(**dict(vars(parser.parse_args())))


if __name__ == "__main__":
    main()
