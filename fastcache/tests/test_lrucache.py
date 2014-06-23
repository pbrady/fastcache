import unittest
from fastcache import lrucache
from itertools import count

class TestLRUCache(unittest.TestCase):
    """ Tests for lrucache. """

    def setUp(self):

        def arg_gen(min=1, max=100, repeat=3):
            for i in range(min, max):
                for r in range(repeat):
                    yield from zip(range(i), count(i, -1))

        self.arg_gen = arg_gen

    def test_function_attributes(self):
        """ Simple tests for attribute preservation. """

        def tfunc(a, b):
            """test function docstring."""
            return a + b
        cfunc = lrucache()(tfunc)
        self.assertEqual(cfunc.__doc__,
                         tfunc.__doc__)

        self.assertTrue(hasattr(cfunc, 'cache_info'))
        self.assertTrue(hasattr(cfunc, 'cache_clear'))

    def test_function_cache(self):
        """ Test that cache returns appropriate values. """

        cat_tuples = [True]

        def tfunc(a, b, c=None):
            if (cat_tuples[0] == True):
                return (a, b, c) + (c, a)
            else:
                return 2*a-10*b

        cfunc = lrucache(maxsize=100,state=cat_tuples)(tfunc)

        for i, j in self.arg_gen(max=75, repeat=5):
            self.assertEqual(cfunc(i, j), tfunc(i, j))

        # change extra state
        cat_tuples[0] = False

        for i, j in self.arg_gen(max=75, repeat=5):
            self.assertEqual(cfunc(i, j), tfunc(i, j))

    def test_memory_leaks(self):
        """ Longer running test to check for memory leaks. """

        def tfunc(a, b, c):
            return (a-1, 2*c) + (10*b-1, a*b, a*b+c)

        cfunc = lrucache(maxsize=2000)(tfunc)

        for i, j in self.arg_gen(max=1500, repeat=5):
            self.assertEqual(cfunc(i, j, c=i-j), tfunc(i, j, c=i-j))

        print(cfunc.cache_info())
