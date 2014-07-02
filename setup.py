from distutils.core import setup, Extension
import sys

vinfo = sys.version_info[:2]
if vinfo < (2, 6):
    print("Fastcache currently requires Python 2.6 or newer.  "+
          "Python {}.{} detected".format(*vinfo))
    sys.exit(-1)
if vinfo[0] == 3 and vinfo < (3, 2):
    print("Fastcache currently requires Python 3.2 or newer.  "+
          "Python {}.{} detected".format(*vinfo))
    sys.exit(-1)

classifiers = [
    'License :: OSI Approved :: MIT License',
    'Operating System :: OS Independent',
    'Programming Language :: Python',
    'Programming Language :: Python :: 2',
    'Programming Language :: Python :: 2.6',
    'Programming Language :: Python :: 2.7',
    'Programming Language :: Python :: 3',
    'Programming Language :: Python :: 3.2',
    'Programming Language :: Python :: 3.3',
    'Programming Language :: Python :: 3.4',
    'Programming Language :: C',

]

long_description = '''
C implementation of Python 3 functools.lru_cache.  Provides speedup of 10-30x
over standard library.  Passes test suite from standard library for lru_cache.

Provides 2 Least Recently Used caching function decorators:
    
    clru_cache - built-in (faster)
               >>> from fastcache import clru_cache
               >>> @clru_cache(maxsize=128,typed=False,state=None)
               ... def f(a, b):
               ...     return (a, )+(b, )
               ...
               >>> type(f)
               >>> <class '_lrucache.cache'>
    
    lru_cache  - python wrapper around clru_cache (slower)
               >>> from fastcache import lru_cache
               >>> @lru_cache(maxsize=128,typed=False,state=None)
               ... def f(a, b):
               ...     return (a, )+(b, )
               ...
               >>> type(f)
               >>> <class 'function'>


    (c)lru_cache(maxsize=128, typed=False, state=None)
        
        Least-recently-used cache decorator.
        
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
'''

setup(name = "fastcache", 
      version = "0.3.1",
      description = "C implementation of Python 3 functools.lru_cache",
      long_description = long_description,
      author = "Peter Brady",
      author_email = "petertbrady@gmail.com",
      license = "MIT",
      url = "https://github.com/pbrady/fastcache",
      packages = ["fastcache", "fastcache.tests"],
      ext_modules = [Extension("fastcache._lrucache",["src/_lrucache.c"])],
      classifiers = classifiers,
      
  )
