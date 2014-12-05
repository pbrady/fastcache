fastcache
=========
[![Gitter](https://badges.gitter.im/Join Chat.svg)](https://gitter.im/pbrady/fastcache?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

C implementation of Python 3 lru_cache for Python 2.6, 2.7, 3.2, 3.3, 3.4

Passes all tests in the standard library for functools.lru_cache.

Obeys same API as Python 3.3/3.4 functools.lru_cache with 2 enhancements:

1.  An additional argument `state` may be supplied which must be a `list` or `dict`.  This allows one to safely cache functions for which the result depends on some context which is not a part of the function call signature.
2.  An additional argument `unhashable` may be supplied to control how the cached function responds to unhashable arguments.  The options are:
  *  "error" (default) - Raise a `TypeError`
  *  "warning"         - Raise a `UserWarning` and call the wrapped function with the supplied arguments.
  *  "ignore"          - Just call the wrapped function with the supplied arguments.

Install
-------

Via [pip](https://pypi.python.org/pypi/fastcache):

    pip install fastcache

Manually :

    git clone https://github.com/pbrady/fastcache.git
    cd fastcache
    python setup.py install

Via [conda](http://conda.pydata.org/docs/index.html) :
  
  * build latest and greatest github version

```bash  
git clone https://github.com/pbrady/fastcache.git
conda-build fastcache
conda install --use-local fastcache
```

  * build latest released version on pypi
```bash
git clone https://github.com/conda/conda-recipes.git
conda-build conda-recipes/fastcache
conda install --use-local fastcache
```

Test
----

```python
>>> import fastcache
>>> fastcache.test()
```

Travis CI status :  [![alt text][2]][1]

[2]: https://travis-ci.org/pbrady/fastcache.svg?branch=master (Travis build status)
[1]: http://travis-ci.org/pbrady/fastcache

Tests include the official suite of tests from Python standard library for functools.lru_cache

Use
---

    >>> from fastcache import clru_cache, __version__
    >>> __version__
    '0.3.3'
    >>> @clru_cache(maxsize=325, typed=False)
    ... def fib(n):
    ...     """Terrible Fibonacci number generator."""
    ...     return n if n < 2 else fib(n-1) + fib(n-2)
    ...
    >>> fib(300)
    222232244629420445529739893461909967206666939096499764990979600
    >>> fib.cache_info()
    CacheInfo(hits=298, misses=301, maxsize=325, currsize=301)
    >>> print(fib.__doc__)
    Terrible Fibonacci number generator.
    >>> fib.cache_clear()
    >>> fib.cache_info()
    CacheInfo(hits=0, misses=0, maxsize=325, currsize=0)
    >>> fib.__wrapped__(300)
    222232244629420445529739893461909967206666939096499764990979600


Speed
-----

The speed up vs `lru_cache` provided by `functools` in 3.3 or 3.4 is 10x-30x depending on the function signature and whether one is comparing with 3.3 or 3.4.  A sample run of the benchmarking suite for 3.3 is

	>>> import sys
	>>> sys.version_info
	sys.version_info(major=3, minor=3, micro=5, releaselevel='final', serial=0)
	>>> from fastcache import benchmark
	>>> benchmark.run()
	Test Suite 1 :

	Primarily tests cost of function call, hashing and cache hits.
	Benchmark script based on
		http://bugs.python.org/file28400/lru_cache_bench.py

	function call                 speed up
	untyped(i)                       11.31, typed(i)                         31.20
	untyped("spam", i)               16.71, typed("spam", i)                 27.50
	untyped("spam", "spam", i)       14.24, typed("spam", "spam", i)         22.62
	untyped(a=i)                     13.25, typed(a=i)                       23.92
	untyped(a="spam", b=i)           10.51, typed(a="spam", b=i)             18.58
	untyped(a="spam", b="spam", c=i)  9.34, typed(a="spam", b="spam", c=i)   16.40

				 min   mean    max
	untyped    9.337 12.559 16.706
	typed     16.398 23.368 31.197


	Test Suite 2 :

	Tests millions of misses and millions of hits to quantify
	cache behavior when cache is full.

	function call                 speed up
	untyped(i, j, a="spammy")         8.94, typed(i, j, a="spammy")          14.09

A sample run of the benchmarking suite for 3.4 is

	>>> import sys
	>>> sys.version_info
	sys.version_info(major=3, minor=4, micro=1, releaselevel='final', serial=0)
	>>> from fastcache import benchmark
	>>> benchmark.run()
	Test Suite 1 :

	Primarily tests cost of function call, hashing and cache hits.
	Benchmark script based on
		http://bugs.python.org/file28400/lru_cache_bench.py

	function call                 speed up
	untyped(i)                        9.74, typed(i)                         23.31
	untyped("spam", i)               15.21, typed("spam", i)                 20.82
	untyped("spam", "spam", i)       13.35, typed("spam", "spam", i)         17.43
	untyped(a=i)                     12.27, typed(a=i)                       19.04
	untyped(a="spam", b=i)            9.81, typed(a="spam", b=i)             14.25
	untyped(a="spam", b="spam", c=i)  7.77, typed(a="spam", b="spam", c=i)   11.61

				 min   mean    max
	untyped    7.770 11.359 15.210
	typed     11.608 17.743 23.311


	Test Suite 2 :

	Tests millions of misses and millions of hits to quantify
	cache behavior when cache is full.

	function call                 speed up
	untyped(i, j, a="spammy")         8.27, typed(i, j, a="spammy")          11.18
