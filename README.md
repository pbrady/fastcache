fastcache
=========

C implementation of Python 3 lru_cache 

Install
-------
Tested on Python >= 3.3

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

Use
---

    >>> from fastcache import clru_cache
    >>> @clru_cache(maxsize=256)
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
Benchmarks for this and other caching [here](http://nbviewer.ipython.org/gist/pbrady/916495198910e7d7c713/Benchmark.ipynb).
