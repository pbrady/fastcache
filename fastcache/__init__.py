from ._lrucache import lrucache as _ccache
from functools import wraps

def lrucache(maxsize=256, state=None):

    def func_wrapper(func):
        _cached_func = _ccache(maxsize, state)(func)

        @wraps(func)
        def wrapper(*args, **kwargs):
            return _cached_func(*args, **kwargs)

        wrapper.cache_info = _cached_func.cache_info
        wrapper.cache_clear = _cached_func.cache_clear

        return wrapper

    return func_wrapper
