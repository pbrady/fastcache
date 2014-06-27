import unittest
from fastcache import clru_cache as lru_cache
import itertools

try:
    itertools.count(start=0, step=-1)
    count = itertools.count
except TypeError:
    def count(start=0, step=1):
        i = step-1
        for j, c in enumerate(itertools.count(start)):
            yield c + i*j

class TestCLru_Cache(unittest.TestCase):
    """ Tests for lru_cache. """

    def setUp(self):

        def arg_gen(min=1, max=100, repeat=3):
            for i in range(min, max):
                for r in range(repeat):
                    for j, k in zip(range(i), count(i, -1)):
                        yield j, k

        def func(a, b):
            return a+b

        self.func = func
        self.arg_gen = arg_gen

    def test_function_attributes(self):
        """ Simple tests for attribute preservation. """

        def tfunc(a, b):
            """test function docstring."""
            return a + b
        cfunc = lru_cache()(tfunc)
        self.assertEqual(cfunc.__doc__,
                         tfunc.__doc__)

        self.assertTrue(hasattr(cfunc, 'cache_info'))
        self.assertTrue(hasattr(cfunc, 'cache_clear'))
        self.assertTrue(hasattr(cfunc, '__wrapped__'))

    def test_function_cache(self):
        """ Test that cache returns appropriate values. """

        cat_tuples = [True]

        def tfunc(a, b, c=None):
            if (cat_tuples[0] == True):
                return (a, b, c) + (c, a)
            else:
                return 2*a-10*b

        cfunc = lru_cache(maxsize=100,state=cat_tuples)(tfunc)

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

        cfunc = lru_cache(maxsize=2000)(tfunc)

        for i, j in self.arg_gen(max=1500, repeat=5):
            self.assertEqual(cfunc(i, j, c=i-j), tfunc(i, j, c=i-j))

    def test_hashable_args(self):
        """ Function arguments must be hashable. """

        @lru_cache()
        def f(a, b):
            return (a, ) + (b, )

        if hasattr(self, 'assertWarns'):
            with self.assertWarns(UserWarning) as cm:
                self.assertEqual(f([1], 2), f.__wrapped__([1], 2))
        else:
            #with self.assertRaises(UserWarning) as cm:
            self.assertEqual(f([1], 2), f.__wrapped__([1], 2))

    def test_state_type(self):
        """ State must be a list. """

        self.assertRaises(TypeError, lru_cache, state=(1))
        self.assertRaises(TypeError, lru_cache, state=-1)

    def test_typed_False(self):
        """ Verify typed==False. """

        cfunc = lru_cache(typed=False)(self.func)
        # initialize cache with integer args
        cfunc(1,2)
        if hasattr(self, 'assertIs'):
            myAssert = self.assertIs
        else:
            myAssert = self.assertEqual
        myAssert(cfunc(1, 2), cfunc(1.0, 2))
        myAssert(cfunc(1, 2), cfunc(1, 2.0))
        # test keywords
        cfunc(1,b=2)
        myAssert(cfunc(1,b=2), cfunc(1.0,b=2))
        myAssert(cfunc(1,b=2), cfunc(1,b=2.0))

    def test_typed_True(self):
        """ Verify typed==True. """

        cfunc = lru_cache(typed=True)(self.func)
        if hasattr(self, 'assertIsNot'):
            myAssert = self.assertIsNot
        else:
            myAssert = self.assertEqual
        myAssert(cfunc(1, 2), cfunc(1.0, 2))
        myAssert(cfunc(1, 2), cfunc(1, 2.0))
        # test keywords
        myAssert(cfunc(1,b=2), cfunc(1.0,b=2))
        myAssert(cfunc(1,b=2), cfunc(1,b=2.0))

