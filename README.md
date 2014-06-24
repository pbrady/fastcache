fastcache
=========

C implementation of Python lru_cache 

Install
-------
Only tested on Python 3.4

`python setup.py install`

Use
---

    >>> from fastcache import clrucache
    >>> @clrucache(maxsize=256)
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
