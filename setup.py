import sys
from os import getenv

# use setuptools by default as per the official advice at:
# packaging.python.org/en/latest/current.html#packaging-tool-recommendations
use_setuptools = True
# set the environment variable USE_DISTUTILS=True to force the use of distutils
use_distutils = getenv('USE_DISTUTILS')
if use_distutils is not None:
    if use_distutils.lower() == 'true':
        use_setuptools = False
    else:
        print("Value {} for USE_DISTUTILS treated as False".\
              format(use_distutils))

from distutils.command.build import build as _build

if use_setuptools:
    try:
        from setuptools import setup, Extension
        from setuptools.command.install import install as _install
        from setuptools.command.build_ext import build_ext as _build_ext
    except ImportError:
        use_setuptools = False

if not use_setuptools:
    from distutils.core import setup, Extension
    from distutils.command.install import install as _install
    from distutils.command.build_ext import build_ext as _build_ext

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
             >>> from fastcache import clru_cache, __version__
             >>> __version__
             '1.1.0'
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
             >>> type(fib)
             >>> <class 'fastcache.clru_cache'>

  lru_cache  - python wrapper around clru_cache
             >>> from fastcache import lru_cache
             >>> @lru_cache(maxsize=128, typed=False)
             ... def f(a, b):
             ...     pass
             ...
             >>> type(f)
             >>> <class 'function'>


  (c)lru_cache(maxsize=128, typed=False, state=None, unhashable='error')

      Least-recently-used cache decorator.

      If *maxsize* is set to None, the LRU features are disabled and the cache
      can grow without bound.

      If *typed* is True, arguments of different types will be cached separately.
      For example, f(3.0) and f(3) will be treated as distinct calls with
      distinct results.

      If *state* is a list or dict, the items will be incorporated into the
      argument hash.

      The result of calling the cached function with unhashable (mutable)
      arguments depends on the value of *unhashable*:

          If *unhashable* is 'error', a TypeError will be raised.

          If *unhashable* is 'warning', a UserWarning will be raised, and
          the wrapped function will be called with the supplied arguments.
          A miss will be recorded in the cache statistics.

          If *unhashable* is 'ignore', the wrapped function will be called
          with the supplied arguments. A miss will will be recorded in
          the cache statistics.

      View the cache statistics named tuple (hits, misses, maxsize, currsize)
      with f.cache_info().  Clear the cache and statistics with f.cache_clear().
      Access the underlying function with f.__wrapped__.

      See:  http://en.wikipedia.org/wiki/Cache_algorithms#Least_Recently_Used
'''

# the overall logic here is that by default macros can be only be passed if
# one does 'python setup.py build_ext --define=MYMACRO'
# If one attempts 'build' or 'install' with the --define flag, an error will
# appear saying that --define is not an option
# To get around this issue, we subclass build and install to capture --define
# as well as build_ext which will use the --define arguments passed to
# build or install

define_opts = []

class BuildWithDefine(_build):

    _build_opts = _build.user_options
    user_options = [
        ('define=', 'D',
         "C preprocessor macros to define"),
    ]
    user_options.extend(_build_opts)

    def initialize_options(self):
        _build.initialize_options(self)
        self.define = None

    def finalize_options(self):
        _build.finalize_options(self)
        # The argument parsing will result in self.define being a string, but
        # it has to be a list of 2-tuples.  All the preprocessor symbols
        # specified by the 'define' option without an '=' will be set to '1'.
        # Multiple symbols can be separated with commas.
        if self.define:
            defines = self.define.split(',')
            self.define = [(s.strip(), 1) if '=' not in s else
                           tuple(ss.strip() for ss in s.split('='))
                           for s in defines]
            define_opts.extend(self.define)

    def run(self):
        _build.run(self)

class InstallWithDefine(_install):

    _install_opts = _install.user_options
    user_options = [
        ('define=', 'D',
         "C preprocessor macros to define"),
    ]
    user_options.extend(_install_opts)

    def initialize_options(self):
        _install.initialize_options(self)
        self.define = None

    def finalize_options(self):
        _install.finalize_options(self)
        # The argument parsing will result in self.define being a string, but
        # it has to be a list of 2-tuples.  All the preprocessor symbols
        # specified by the 'define' option without an '=' will be set to '1'.
        # Multiple symbols can be separated with commas.
        if self.define:
            defines = self.define.split(',')
            self.define = [(s.strip(), 1) if '=' not in s else
                           tuple(ss.strip() for ss in s.split('='))
                           for s in defines]
            define_opts.extend(self.define)

    def run(self):
        _install.run(self)

class BuildExt(_build_ext):

    def initialize_options(self):
        _build_ext.initialize_options(self)

    def finalize_options(self):
        _build_ext.finalize_options(self)
        if self.define is not None:
            self.define.extend(define_opts)
        elif define_opts:
            self.define = define_opts

    def run(self):
        _build_ext.run(self)


setup(name = "fastcache",
      version = "1.1.0",
      description = "C implementation of Python 3 functools.lru_cache",
      long_description = long_description,
      author = "Peter Brady",
      author_email = "petertbrady@gmail.com",
      license = "MIT",
      url = "https://github.com/pbrady/fastcache",
      packages = ["fastcache", "fastcache.tests"],
      ext_modules = [Extension("fastcache._lrucache",["src/_lrucache.c"])],
      classifiers = classifiers,
      cmdclass={
          'build' : BuildWithDefine,
          'install' : InstallWithDefine,
          'build_ext' : BuildExt,
      }

)
