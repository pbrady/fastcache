from distutils.core import setup, Extension
setup(name="fastcache", version="0.1",
      packages = ["fastcache", "fastcache.tests"],
      ext_modules= [Extension("fastcache._lrucache",
                    ["src/_lrucache.c"],
                    ),
                ]
      
  )
