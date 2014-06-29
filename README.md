fastcache
=========

C implementation of Python 3 lru_cache for Python 2.6, 2.7, 3.2, 3.3, 3.4 

Install
-------

Via pip: 

    pip install fastcache

Manually : 

    git clone https://github.com/pbrady/fastcache.git
    cd fastcache
    python setup.py install

Test
----

`py.test --pyargs fastcache`

Travis CI status :  [![alt text][2]][1]

[2]: https://travis-ci.org/pbrady/fastcache.svg?branch=master (Travis build status)
[1]: http://travis-ci.org/pbrady/fastcache

Tests include the official suite of tests from Python standard library for functools.lru_cache

Use
---

    >>> from fastcache import clru_cache
    >>> @clru_cache(maxsize=256, typed=True)
    >>> def f(a, b):
    ...     """Test function."""
    ...     return (a, ) + (b, )
    >>> f(1,2)
    (1, 2)
    >>> f.cache_info()
    CacheInfo(hits=0, misses=1, maxsize=256, currsize=1)
    >>> f(1,2)
    (1, 2)
    >>> f.cache_info()
    CacheInfo(hits=1, misses=1, maxsize=256, currsize=1)
    >>> f.cache_clear()
    >>> f.cache_info()
    CacheInfo(hits=0, misses=0, maxsize=256, currsize=0)

Speed
-----

The speed up vs `lru_cache` provided by `functools` in 3.3 or 3.4 is 10x-25x depending on the function signature.  A sample run of the benchmarking suite is 

	>>> from fastcache import benchmark
	>>> benchmark.run()
	Test Suite 1 : 

	Primarily tests cost of function call, hashing and cache hits.
	Benchmark script based on
		http://bugs.python.org/file28400/lru_cache_bench.py

	function call                 speed up
	untyped(i)                        9.82, typed(i)                         22.61
	untyped("spam", i)               15.49, typed("spam", i)                 20.96
	untyped("spam", "spam", i)       13.49, typed("spam", "spam", i)         17.74
	untyped(a=i)                     12.10, typed(a=i)                       18.47
	untyped(a="spam", b=i)            9.75, typed(a="spam", b=i)             13.83
	untyped(a="spam", b="spam", c=i)  7.79, typed(a="spam", b="spam", c=i)   11.34

				 min   mean    max
	untyped    7.788 11.406 15.489
	typed     11.340 17.490 22.608


	Test Suite 2 :

	Tests millions of misses and millions of hits to quantify
	cache behavior when cache is full.

	function call                 speed up
	untyped(i, j, a="spammy")         7.87, typed(i, j, a="spammy")          10.71

