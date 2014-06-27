""" C implementation of LRU caching. 

Provides 2 LRU caching function decorators:

clru_cache - built-in (faster)
           >>> from fastcache import clru_cache
           >>> @clru_cache(maxsize=256,typed=False,state=None)
           ... def f(a, b):
           ...     return (a, )+(b, )
           ...
           >>> type(f)
           >>> <class '_lrucache.cache'>

lru_cache  - python wrapper around clru_cache (slower)
           >>> from fastcache import lru_cache
           >>> @lru_cache(maxsize=256,typed=False,state=None)
           ... def f(a, b):
           ...     return (a, )+(b, )
           ...
           >>> type(f)
           >>> <class 'function'>
"""

from ._lrucache import lrucache as clru_cache
from functools import update_wrapper

def lru_cache(maxsize=256, typed=False, state=None):
    """Least-recently-used cache decorator.

    If *maxsize* is set to None, the LRU features are disabled and the cache
    can grow without bound.

    If *typed* is True, arguments of different types will be cached separately.
    For example, f(3.0) and f(3) will be treated as distinct calls with
    distinct results.

    If *state* is a list, the items in the list will be incorporated into 
    argument hash.

    Arguments to the cached function must be hashable.

    View the cache statistics named tuple (hits, misses, maxsize, currsize)
    with f.cache_info().  Clear the cache and statistics with f.cache_clear().
    Access the underlying function with f.__wrapped__.

    See:  http://en.wikipedia.org/wiki/Cache_algorithms#Least_Recently_Used

    """
    def func_wrapper(func):
        _cached_func = clru_cache(maxsize, typed, state)(func)

        def wrapper(*args, **kwargs):
            return _cached_func(*args, **kwargs)
            
        wrapper.__wrapped__ = func
        wrapper.cache_info = _cached_func.cache_info
        wrapper.cache_clear = _cached_func.cache_clear

        return update_wrapper(wrapper,func)

    return func_wrapper
    
