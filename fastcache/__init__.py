""" Wrapper for C implementation of LRU caching. """

from ._lrucache import lrucache as _ccache
from functools import wraps

def lrucache(maxsize=256, typed=False, state=None):
    """Least-recently-used cache decorator.

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
        _cached_func = _ccache(maxsize, typed, state)(func)

        @wraps(func)
        def wrapper(*args, **kwargs):
            return _cached_func(*args, **kwargs)

        wrapper.cache_info = _cached_func.cache_info
        wrapper.cache_clear = _cached_func.cache_clear

        return wrapper

    return func_wrapper
