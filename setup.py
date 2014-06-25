from distutils.core import setup, Extension
import sys

if sys.version_info[:2] < (3, 3):
    print("Fastcache currently requires Python 3.3 or newer.  "+
          "Python {}.{} detected".format(*sys.version_info[:2]))
    sys.exit(-1)

classifiers = [
    'License :: OSI Approved :: MIT License',
    'Operating System :: OS Independent',
    'Programming Language :: Python',
    'Programming Language :: Python :: 3.3',
    'Programming Language :: Python :: 3.4',
    'Programming Language :: C',

]

setup(name = "fastcache", 
      version = "0.1",
      description = "C implementation of Python 3 functools.lru_cache",
      long_description = "C implementation of Python 3 functools.lru_cache",
      author = "Peter Brady",
      author_email = "petertbrady@gmail.com",
      license = "MIT",
      url = "https://github.com/pbrady/fastcache",
      packages = ["fastcache", "fastcache.tests"],
      ext_modules = [Extension("fastcache._lrucache",["src/_lrucache.c"])],
      classifiers = classifiers,
      
  )
