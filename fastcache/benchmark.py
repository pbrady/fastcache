""" Benchmark against functools.lru_cache.  

    Benchmark script from http://bugs.python.org/file28400/lru_cache_bench.py
    with a few modifications.

    Not available for Py < 3.3.
"""

import sys

if sys.version_info[:2] >= (3, 3):
   
    import functools
    import fastcache
    import timeit

    def _untyped(*args, **kwargs):
        pass

    def _typed(*args, **kwargs):
        pass


    _py_untyped = functools.lru_cache()(_untyped)
    _c_untyped =  fastcache.clru_cache()(_untyped)

    _py_typed = functools.lru_cache(typed=True)(_typed)
    _c_typed =  fastcache.clru_cache(typed=True)(_typed)

    def _print_speedup(results):
        print('')
        print('{:9s} {:>6s} {:>6s} {:>6s}'.format('','min', 'mean', 'max'))
        def print_stats(name,off0, off1):
            arr = [py[0]/c[0] for py, c in zip(results[off0::4],
                                              results[off1::4])]
            print('{:9s} {:6.3f} {:6.3f} {:6.3f}'.format(name,
                                                         min(arr),
                                                         sum(arr)/len(arr),
                                                         max(arr)))
        print_stats('untyped', 0, 1)
        print_stats('typed', 2, 3)

    def run():

        results = []
        args = ['i', '"spam", i', '"spam", "spam", i',
                'a=i', 'a="spam", b=i', 'a="spam", b="spam", c=i']
        for a in args:
            for f in ['_py_untyped', '_c_untyped', '_py_typed', '_c_typed']:
                s = '%s(%s)' % (f, a)
                t = min(timeit.repeat('''
                for i in range(100):
                    %s
                ''' % s, setup='from fastcache.benchmark import %s' % f, 
                                      repeat=10, number=1000))
                print('%6.3f  %s' % (t, s[1:]))
                results.append([t, s])

        _print_speedup(results)
