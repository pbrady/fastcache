#include <Python.h>
#include "structmember.h"
#include "pythread.h"

#ifdef __cplusplus
extern "C" {
#endif

#if PY_MAJOR_VERSION == 2
#define _PY2
typedef long Py_hash_t;
#endif

#if PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION == 2
#define _PY32
#endif

#ifdef LLTRACE
#define TBEGIN(x, line) printf("Beginning Trace of %s at lineno %d....", x);
#define TEND(x) printf("Finished!\n")
#else
#define TBEGIN(x, line)
#define TEND(x)
#endif

#ifdef WITH_THREAD
#ifdef _PY2
typedef int PyLockStatus;
static PyLockStatus PY_LOCK_FAILURE = 0;
static PyLockStatus PY_LOCK_ACQUIRED = 1;
static PyLockStatus PY_LOCK_INTR = -999999;
#endif

static int
rlock_acquire(PyThread_type_lock lock, long* rlock_owner, unsigned long* rlock_count)
{
    long tid;
    PyLockStatus r;

    tid = PyThread_get_thread_ident();
    if (*rlock_count > 0 && tid == (*rlock_owner)) {
        unsigned long count = *rlock_count + 1;
        if (count <= *rlock_count) {
            PyErr_SetString(PyExc_OverflowError,
                            "Internal lock count overflowed");
            return -1;
        }
        *rlock_count = count;
        return 1;
    }
    /* do/while loop from acquire_timed */
    do {
        /* first a simple non-blocking try without releasing the GIL */
#ifdef _PY2
        r = PyThread_acquire_lock(lock, 0);
#else
        r = PyThread_acquire_lock_timed(lock, 0, 0);
#endif
        if (r == PY_LOCK_FAILURE) {
            Py_BEGIN_ALLOW_THREADS
#ifdef _PY2
            r = PyThread_acquire_lock(lock, 1);
#else
            r = PyThread_acquire_lock_timed(lock, -1, 1);
#endif
            Py_END_ALLOW_THREADS
        }

        if (r == PY_LOCK_INTR) {
            /* Run signal handlers if we were interrupted.  Propagate
             * exceptions from signal handlers, such as KeyboardInterrupt, by
             * passing up PY_LOCK_INTR.  */
            if (Py_MakePendingCalls() < 0) {
                return -1;
            }
        }
    } while (r == PY_LOCK_INTR);  /* Retry if we were interrupted. */
    if (r == PY_LOCK_ACQUIRED) {
        *rlock_owner = tid;
        *rlock_count = 1;
        return 1;
    }
    return -1;
}

static int
rlock_release(PyThread_type_lock lock, long* rlock_owner, unsigned long* rlock_count)
{
    long tid = PyThread_get_thread_ident();

    if (*rlock_count == 0 || *rlock_owner != tid) {
        PyErr_SetString(PyExc_RuntimeError,
                        "cannot release un-acquired lock");
        return -1;
    }

    if (--(*rlock_count) == 0) {
        *rlock_owner = 0;
        PyThread_release_lock(lock);
    }
    return 1;
}

#define ACQUIRE_LOCK(obj) rlock_acquire((obj)->lock, &((obj)->rlock_owner), &((obj)->rlock_count))
#define RELEASE_LOCK(obj) rlock_release((obj)->lock, &((obj)->rlock_owner), &((obj)->rlock_count))
#define FREE_LOCK(obj) PyThread_free_lock((obj)->lock)
#else
#define ACQUIRE_LOCK(obj) 1
#define RELEASE_LOCK(obj) 1
#define FREE_LOCK(obj)
#endif

#define INC_RETURN(op) return Py_INCREF(op), (op)

// THREAD SAFETY NOTES:
// Python bytecode instructions are atomic but the GIL may switch between
// threads in between instructions.
// To make this threadsafe care needs to be taken one such that global objects
// are left in a consistent between calls to python bytecode.
// The relevant global objects are co->root, and co->cache_dict
// The stats are global as well but are modified in one line: stat++

/* HashedArgs -- internal *****************************************/
typedef struct {
  PyObject_HEAD
  PyObject *args;
  Py_hash_t hashvalue;
} HashedArgs;


static void
HashedArgs_dealloc(HashedArgs *self)
{
  Py_XDECREF(self->args);
  Py_TYPE(self)->tp_free(self);
  return;
}


/* return precomputed tuple hash for speed */
static Py_hash_t
HashedArgs_hash(HashedArgs *self)
{
  return self->hashvalue;
}


/* Delegate comparison to tuples */
static PyObject *
HashedArgs_richcompare(PyObject *v, PyObject *w, int op)
{
  HashedArgs *hv = (HashedArgs *) v;
  HashedArgs *hw = (HashedArgs *) w;
  PyObject *res = PyObject_RichCompare(hv->args, hw->args, op);
  return res;
}


static PyTypeObject HashedArgs_type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "_lrucache.HashedArgs",          /* tp_name */
  sizeof(HashedArgs),              /* tp_basicsize */
  0,                            /* tp_itemsize */
  (destructor)HashedArgs_dealloc,  /* tp_dealloc */
  0,                            /* tp_print */
  0,                            /* tp_getattr */
  0,                            /* tp_setattr */
  0,                            /* tp_reserved */
  0,                            /* tp_repr */
  0,                            /* tp_as_number */
  0,                            /* tp_as_sequence */
  0,                            /* tp_as_mapping */
  (hashfunc)HashedArgs_hash,    /* tp_hash */
  0,                            /* tp_call */
  0,                            /* tp_str */
  0,                            /* tp_getattro */
  0,                            /* tp_setattro */
  0,                            /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,             /* tp_flags */
  0,                              /* tp_doc */
  0,                        /* tp_traverse */
  0,                       /* tp_clear */
  HashedArgs_richcompare,     /* tp_richcompare */
};

/***************************************************
 End of HashedArgs
***************************************************/

/***********************************************************
 circular doubly linked list
************************************************************/
typedef struct clist{
  PyObject_HEAD
  struct clist *prev;
  struct clist *next;
  PyObject *key;
  PyObject *result;
} clist;


static void
clist_dealloc(clist *co)
{
  clist *prev = co->prev;
  clist *next = co->next;

  // THREAD SAFETY NOTES:
  // Calls to DECREF can result in bytecode and thread switching.
  // Do DECREF after the linked list has been modified and is in
  // an acceptable state.
  if(prev != co){
    // adjust neighbor pointers
    prev->next = next;
    next->prev = prev;
  }
  co->prev = NULL;
  co->next = NULL;
  Py_XDECREF(co->key);
  Py_XDECREF(co->result);
  Py_TYPE(co)->tp_free(co);
  return;
}


static PyTypeObject clist_type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "_lrucache.clist",   /* tp_name */
  sizeof(clist),       /* tp_basicsize */
  0,                       /* tp_itemsize */
  (destructor)clist_dealloc,   /* tp_dealloc */
  0,                       /* tp_print */
  0,                       /* tp_getattr */
  0,                       /* tp_setattr */
  0,                       /* tp_reserved */
  0,                       /* tp_repr */
  0,                       /* tp_as_number */
  0,                       /* tp_as_sequence */
  0,                       /* tp_as_mapping */
  0,                       /* tp_hash */
  0,                       /* tp_call */
  0,                       /* tp_str */
  0,                       /* tp_getattro */
  0,                       /* tp_setattro */
  0,                       /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,      /* tp_flags */
};


static int
insert_first(clist *root, PyObject *key, PyObject *result){
  // first element will be inserted at root->next
  clist *first = PyObject_New(clist, &clist_type);
  clist *oldfirst = root->next;

  if(!first)
    return -1;

  first->result = result;
  // This will be the only reference to key (HashedArgs), do not INCREF
  first->key = key;

  root->next = first;
  first->next = oldfirst;
  first->prev = root;
  oldfirst->prev = first;
  // INCREF result since it will be used by clist and returned to the caller
  return  Py_INCREF(result), 1;
}


static PyObject *
make_first(clist *root, clist *node){
  // make node the first node and return new reference to result
  // save previous first position
  clist *oldfirst = root->next;

  if (oldfirst != node) {
    // first adjust pointers around node's position
    node->prev->next = node->next;
    node->next->prev = node->prev;

    root->next = node;
    node->next = oldfirst;
    node->prev = root;
    oldfirst->prev = node;
  }
  INC_RETURN(node->result);
}

/**********************************************************
 cachedobject is the actual function with the cached results
***********************************************************/

/* how will unhashable arguments be handled */
enum unhashable {FC_ERROR, FC_WARNING, FC_IGNORE, FC_FAIL};


typedef struct {
  PyObject_HEAD
  PyObject *fn ; // original function
  PyObject *func_module, *func_name, *func_qualname, *func_annotations;
  PyObject *func_dict;
  PyObject *cache_dict;
  PyObject *ex_state;
  int typed;
  enum unhashable err;
  PyObject *cinfo; // named tuple constructor
  Py_ssize_t maxsize, hits, misses;
  clist *root;
  // lock for cache access
#ifdef WITH_THREAD
  PyThread_type_lock lock;
  long rlock_owner;
  unsigned long rlock_count;
#endif
} cacheobject ;


#define OFF(x) offsetof(cacheobject, x)
// attributes from wrapped function
static PyMemberDef cache_memberlist[] = {
  {"__wrapped__", T_OBJECT, OFF(fn), RESTRICTED | READONLY},
  {"__module__",  T_OBJECT, OFF(func_module), RESTRICTED | READONLY},
  {"__name__",    T_OBJECT, OFF(func_name), RESTRICTED | READONLY},
  {"__qualname__",T_OBJECT, OFF(func_qualname), RESTRICTED | READONLY},
  {"__annotations__", T_OBJECT, OFF(func_annotations), RESTRICTED | READONLY},
  {NULL} /* Sentinel */
};


// getsetters from wrapped function
static PyObject *
cache_get_doc(cacheobject * co, void *closure)
{
  PyFunctionObject * fn = (PyFunctionObject *) co->fn;
  if (fn->func_doc == NULL)
    Py_RETURN_NONE;

  INC_RETURN(fn->func_doc);
}

#if defined(_PY2) || defined (_PY32)

static int
restricted(void)
{
#ifdef _PY2
    if (!PyEval_GetRestricted())
#endif
        return 0;
    PyErr_SetString(PyExc_RuntimeError,
        "function attributes not accessible in restricted mode");
    return 1;
}


static PyObject *
func_get_dict(PyFunctionObject *op)
{
    if (restricted())
        return NULL;
    if (op->func_dict == NULL) {
        op->func_dict = PyDict_New();
        if (op->func_dict == NULL)
            return NULL;
    }
    Py_INCREF(op->func_dict);
    return op->func_dict;
}

static int
func_set_dict(PyFunctionObject *op, PyObject *value)
{
    PyObject *tmp;

    if (restricted())
        return -1;
    /* It is illegal to del f.func_dict */
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "function's dictionary may not be deleted");
        return -1;
    }
    /* Can only set func_dict to a dictionary */
    if (!PyDict_Check(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "setting function's dictionary to a non-dict");
        return -1;
    }
    tmp = op->func_dict;
    Py_INCREF(value);
    op->func_dict = value;
    Py_XDECREF(tmp);
    return 0;
}

static PyGetSetDef cache_getset[] = {
  {"__doc__", (getter)cache_get_doc, NULL, NULL, NULL},
  {"__dict__", (getter)func_get_dict, (setter)func_set_dict},
  {NULL} /* Sentinel */
};

#else

static PyGetSetDef cache_getset[] = {
  {"__doc__", (getter)cache_get_doc, NULL, NULL, NULL},
  {"__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict},
  {NULL} /* Sentinel */
};

#endif


/* Bind a function to an object */
static PyObject *
cache_descr_get(PyObject *func, PyObject *obj, PyObject *type)
{
    if (obj == Py_None || obj == NULL)
      INC_RETURN(func);

#ifdef _PY2
    return PyMethod_New(func, obj, type);
#else
    return PyMethod_New(func, obj);
#endif
}


static void
cache_dealloc(cacheobject *co)
{
  Py_CLEAR(co->fn);
  Py_CLEAR(co->func_module);
  Py_CLEAR(co->func_name);
  Py_CLEAR(co->func_qualname);
  Py_CLEAR(co->func_annotations);
  Py_CLEAR(co->func_dict);
  Py_CLEAR(co->cache_dict);
  Py_CLEAR(co->ex_state);
  Py_CLEAR(co->cinfo);
  Py_CLEAR(co->root);
  FREE_LOCK(co);
  Py_TYPE(co)->tp_free(co);

}


/*
 * attempt to set hs->hashvalue to hash(hs->args)  Does not do alter any
 * reference counts.  Returns NULL on error.  If hs->hashvalue==-1 on return
 * then hs->args is Unhashable
 */
static PyObject *
set_hash_value(cacheobject *co, HashedArgs *hs)
{
  if ((hs->hashvalue = PyObject_Hash(hs->args)) == -1) {
    // unhashable
    if (co->err == FC_ERROR) {
      return NULL;
    }
    // if error was something other than a TypeError, exit
    if (!PyErr_GivenExceptionMatches(PyErr_Occurred(), PyExc_TypeError)) {
      return NULL;
    }
    PyErr_Clear();

    if (co->err == FC_WARNING) {
      // try to issue warning
      if( PyErr_WarnEx(PyExc_UserWarning,
          "Unhashable arguments cannot be cached",1) < 0){
        // warning becomes exception
        PyErr_SetString(PyExc_TypeError,
                        "Cached function arguments must be hashable");
        return NULL;
      }
    }
  }
  // success!
  return (PyObject *) hs;
}

// compute the hash of function args and kwargs
// THREAD SAFTEY NOTES:
// We access global data: co->ex_state and co->typed.
// These data are defined at co creation time and are not
// changed so we do not need to worry about thread safety here
static PyObject *
make_key(cacheobject *co, PyObject *args, PyObject *kw)
{
  PyObject *item, *keys, *key;
  Py_ssize_t ex_size = 0;
  Py_ssize_t arg_size = 0;
  Py_ssize_t kw_size = 0;
  Py_ssize_t i, size, off;
  HashedArgs *hs;
  int is_list = 1;

  // determine size of arguments and types
  if (PyList_Check(co->ex_state))
    ex_size = Py_SIZE(co->ex_state);
  else if (PyDict_CheckExact(co->ex_state)){
    is_list = 0;
    ex_size = PyDict_Size(co->ex_state);
  }
  if (args && PyTuple_CheckExact(args))
    arg_size = PyTuple_GET_SIZE(args);
  if (kw && PyDict_CheckExact(kw))
    kw_size = PyDict_Size(kw);

  // allocate HashedArgs Object
  if(!(hs = PyObject_New(HashedArgs, &HashedArgs_type)))
    return NULL;

  // total size
  if (co->typed)
    size = (2-is_list)*ex_size+2*arg_size+3*kw_size;
  else
    size = (2-is_list)*ex_size+arg_size+2*kw_size;
  // initialize new tuple
  if(!(hs->args = PyTuple_New(size))){
    return NULL;
  }
  // incorporate extra state
  if(is_list){
    for(i = 0; i < ex_size; i++){
      PyObject *tmp = PyList_GET_ITEM(co->ex_state, i);
      PyTuple_SET_ITEM(hs->args, i, tmp);
      Py_INCREF(tmp);
    }
  }
  else if(ex_size > 0){
    if(!(keys = PyDict_Keys(co->ex_state))){
      Py_DECREF(hs);
      return NULL;
    }
    if( PyList_Sort(keys) < 0){
      Py_DECREF(keys);
      Py_DECREF(hs);
      return NULL;
    }
    for(i = 0; i < ex_size; i++){
      key = PyList_GET_ITEM(keys, i);
      Py_INCREF(key);
      PyTuple_SET_ITEM(hs->args, 2*i, key);

      if(!(item = PyDict_GetItem(co->ex_state, key))){
        Py_DECREF(keys);
        Py_DECREF(hs);
        return NULL;
      }
      Py_INCREF(item);
      PyTuple_SET_ITEM(hs->args, 2*i+1, item);
    }
    Py_DECREF(keys);
  }
  off = (2-is_list)*ex_size;

  // incorporate arguments
  for(i = 0; i < arg_size; i++){
    PyObject *tmp = PyTuple_GET_ITEM(args, i);
    PyTuple_SET_ITEM(hs->args, off+i, tmp);
    Py_INCREF(tmp);
    if(co->typed) {
      off += 1;
      tmp = (PyObject *)Py_TYPE(tmp);
      Py_INCREF(tmp);
      PyTuple_SET_ITEM(hs->args, off+i, tmp);
    }
  }
  off += arg_size;

  // incorporate keyword arguments
  if(kw_size > 0){
    if(!(keys = PyDict_Keys(kw))){
      Py_DECREF(hs);
      return NULL;
    }
    if( PyList_Sort(keys) < 0){
      Py_DECREF(keys);
      Py_DECREF(hs);
      return NULL;
    }
    for(i = 0; i < kw_size; i++){
      key = PyList_GET_ITEM(keys, i);
      Py_INCREF(key);
      PyTuple_SET_ITEM(hs->args, off+i, key);
      if(!(item = PyDict_GetItem(kw, key))){
        Py_DECREF(keys);
        Py_DECREF(hs);
        return NULL;
      }
      off += 1;
      Py_INCREF(item);
      PyTuple_SET_ITEM(hs->args, off+i, item);
      if (co->typed){
          off += 1;
          item = (PyObject *)Py_TYPE(item);
          Py_INCREF(item);
          PyTuple_SET_ITEM(hs->args, off+i, item);
      }
    }
    Py_DECREF(keys);
  }
  // check for an error we may have missed
  if( PyErr_Occurred() ){
    Py_DECREF(hs);
    return NULL;
  }
  // set hash value
  if( !set_hash_value(co, hs) ) {
    Py_DECREF(hs);
    return NULL;
  }

  return (PyObject *)hs;
}


/***********************************************************
 * All calls to the cached function go through cache_call
 * Handles: (1) Generation of key (via make_key)
 *          (2) Maintenance of circular doubly linked list
 *          (3) Actual updates to cache dictionary
 * THREAD SAFETY NOTES:
 * 1. The GIL may switch threads between all PyDict_Get/Set/DelItem
 *    If another thread were to call cache_clear while the dict was in
 *    an indetermined state, that could be very very bad.  Must lock all
 *    updates to cache_dict
 ***********************************************************/
static PyObject *
cache_call(cacheobject *co, PyObject *args, PyObject *kw)
{
  PyObject *key, *result, *link, *first;

  /* no cache, just update stats and return */
  if (co->maxsize == 0) {
    co->misses++;
    return PyObject_Call(co->fn, args, kw);
  }

  // generate a key from hashing the arguments
  // THREAD SAFETY NOTES:
  // Computing the hash will result in many potential calls to __hash__
  // methods, allowing the GIL to switch threads.  Thus it is possible that
  // two threads have called this function with the exact same arguments
  // and are constructing keys
  key = make_key(co, args, kw);
  if (!key)
    return NULL;

  /* check for unhashable type */
  if ( ((HashedArgs *)key)->hashvalue == -1){
    // no locking neccessary here
    Py_DECREF(key);
    co->misses++;
    return PyObject_Call(co->fn, args, kw);
  }

  /* For an unbounded cache, link is simply the result of the function call
   * For an LRU cache, link is a pointer to a clist node */
  if(ACQUIRE_LOCK(co) == -1){
    Py_DECREF(key);
    return NULL;
  }
  link = PyDict_GetItem(co->cache_dict, key);
  if(PyErr_Occurred()){
    RELEASE_LOCK(co);
    Py_XDECREF(link);
    Py_DECREF(key);
    return NULL;
  }
  if(RELEASE_LOCK(co) == -1){
    Py_XDECREF(link);
    Py_DECREF(key);
    return NULL;
  }

  if (!link){
    result = PyObject_Call(co->fn, args, kw); // result refcount is one
    if(PyErr_Occurred() || !result){
      Py_XDECREF(result);
      Py_DECREF(key);
      return NULL;
    }
    /* Unbounded cache, no clist maintenance, no locks needed */
    if (co->maxsize < 0){
      if( PyDict_SetItem(co->cache_dict, key, result) == -1 ||
          PyErr_Occurred()){
        Py_DECREF(key);
        Py_DECREF(result);
        return NULL;
      }
      Py_DECREF(key);
      return co->misses++, result;
    }
    /* Least Recently Used cache */
    /* Need to reacquire the lock here and make sure that the key,result were
     * not added to the cache while we were waiting */
    if(ACQUIRE_LOCK(co) == -1){
      Py_DECREF(key);
      Py_DECREF(result);
      return NULL;
    }
#ifdef WITH_THREAD
    link = PyDict_GetItem(co->cache_dict, key);
    if(PyErr_Occurred()){
      RELEASE_LOCK(co);
      Py_DECREF(key);
      Py_DECREF(result);
      Py_XDECREF(link);
      return NULL;
    }
    if(link){
      Py_DECREF(key);
      if(RELEASE_LOCK(co) == -1){
        Py_DECREF(result);
        return NULL;
      }
      return co->hits++, result;
    }
#endif
    /* if cache is full, repurpose the last link rather than
     * passing it off to garbage collection.  */
    if (((PyDictObject *)co->cache_dict)->ma_used == co->maxsize){
      /* Note that the old key will be used to delete the link from the dictionary
       * Be sure to INCREF old link so we don't lose it before
       * we add it when the PyDict_DelItem occurs */
      clist *last = co->root->prev;
      PyObject *old_key = last->key;
      PyObject *old_res = last->result;
      // set new items
      last->key = key;
      last->result = result;
      // bump to the front (get back the result we just set).
      result = make_first(co->root, last);
      // Increase ref count of repurposed link so we don't trigger GC
      // save the first position since the global co->root->next may change
      first = (PyObject *) co->root->next;
      // Increases first->refcount to 2
      if(PyDict_SetItem(co->cache_dict, key, first) == -1){
        Py_DECREF(first);
        Py_DECREF(first);
        Py_DECREF(key);
        Py_DECREF(old_key);
        Py_DECREF(old_res);
        Py_DECREF(result);
        RELEASE_LOCK(co);
        return NULL;
      }
      // handle deletions
      if(PyDict_DelItem(co->cache_dict, old_key) == -1){
        Py_DECREF(old_key);
        Py_DECREF(old_res);
        Py_DECREF(result);
        RELEASE_LOCK(co);
        return NULL;
      }
      // These would have been decrefed had we simply deleted the link
      Py_DECREF(old_key);
      Py_DECREF(old_res);
      if(PyErr_Occurred()){
        Py_DECREF(result);
        RELEASE_LOCK(co);
        return NULL;
      }
      if(RELEASE_LOCK(co) == -1){
        Py_DECREF(result);
        return NULL;
      }
      return co->misses++, result;
    }
    else {
      if(insert_first(co->root, key, result) < 0) {
        Py_DECREF(key);
        Py_DECREF(result);
        RELEASE_LOCK(co);
        return NULL;
      }
      first = (PyObject *) co->root->next; // insert_first sets refcount to 1
      // key and first count++
      if(PyDict_SetItem(co->cache_dict, key, first) == -1 || PyErr_Occurred()){
        Py_DECREF(first);
        Py_DECREF(result);
        RELEASE_LOCK(co);
        return NULL;
      }
      Py_DECREF(first);
      // Don't DECREF key here since we want both the dict and the node 'first'
      // To be able to have a valid copy
      co->misses++;
      if(RELEASE_LOCK(co) == -1){
        Py_DECREF(result);
        return NULL;
      }
      return result;
    }
  } // link != NULL
  else {
    if( co->maxsize < 0){
      Py_DECREF(key);
      co->hits++;
      INC_RETURN(link);
    }
    /* bump link to the front of the list and get result from link */
    result = make_first(co->root, (clist *) link);
    Py_DECREF(key);
    co->hits++;
    return result;
  }
}


PyDoc_STRVAR(cacheclear__doc__,
"cache_clear(self)\n\
\n\
Clear the cache and cache statistics.");
static PyObject *
cache_clear(PyObject *self)
{
  cacheobject *co = (cacheobject *)self;
  // delete dictionary - use a lock to keep dict in a fully determined state
  if(ACQUIRE_LOCK(co) == -1)
    return NULL;
  PyDict_Clear(co->cache_dict);
  co->hits = 0;
  co->misses = 0;
  if(RELEASE_LOCK(co) == -1)
    return NULL;
  Py_RETURN_NONE;
}


PyDoc_STRVAR(cacheinfo__doc__,
"cache_info(self)\n\
\n\
Report cache statistics.");
static PyObject *
cache_info(PyObject *self)
{
  cacheobject * co = (cacheobject *) self;
  if (co->maxsize >= 0)
    return PyObject_CallFunction(co->cinfo,"nnnn",co->hits,
                                 co->misses, co->maxsize,
                                 ((PyDictObject *)co->cache_dict)->ma_used);
  else
    return PyObject_CallFunction(co->cinfo,"nnOn",co->hits,
                                 co->misses, Py_None,
                                 ((PyDictObject *)co->cache_dict)->ma_used);
}


static PyMethodDef cache_methods[] = {
  {"cache_clear", (PyCFunction) cache_clear, METH_NOARGS,
   cacheclear__doc__},
  {"cache_info", (PyCFunction) cache_info, METH_NOARGS,
   cacheinfo__doc__},
  {NULL, NULL} /* sentinel */
};


PyDoc_STRVAR(fn_doc,
             "Cached function.");


static PyTypeObject cache_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "fastcache.clru_cache",                /* tp_name */
    sizeof(cacheobject),                       /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)cache_dealloc,            /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    (ternaryfunc)cache_call,              /* tp_call */
    0,                                  /* tp_str */
    0,                                  /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT ,                /* tp_flags */
    fn_doc,                                  /* tp_doc */
    0,                                  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    cache_methods,                      /* tp_methods */
    cache_memberlist,                   /* tp_members */
    cache_getset,                       /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    cache_descr_get,                    /* tp_descr_get */
    0,                                  /* tp_descr_set */
    OFF(func_dict),                     /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    0,                                  /* tp_new */
    0,                                  /* tp_free */
};


/* lruobject -
 * the callable object returned by lrucache(all, my, cache, args)
 * [lrucache is known as clru_cache in python land]
 * records arguments to clru_cache and passes them along to the
 * cacheobject created when lruobject is called with a function
 * as an argument */
typedef struct {
  PyObject_HEAD
  Py_ssize_t maxsize;
  PyObject *state;
  int typed;
  enum unhashable err;
} lruobject;


static void lru_dealloc(lruobject *lru)
{
  Py_CLEAR(lru->state);
  Py_TYPE(lru)->tp_free(lru);
}


static PyObject *
get_func_attr(PyObject *fo, const char *name)
{
  if( !PyObject_HasAttrString(fo,name))
    Py_RETURN_NONE;
  else{
    PyObject *attr = PyObject_GetAttrString(fo, name);
    if (attr == NULL)
      return NULL;
    return attr;
  }
}


/* takes a function as an argument and returns a cacheobject */
static PyObject *
lru_call(lruobject *lru, PyObject *args, PyObject *kw)
{
  PyObject *fo, *mod, *nt;
  cacheobject *co;

  if(! PyArg_ParseTuple(args, "O", &fo))
    return NULL;

  if(! PyCallable_Check(fo)){
    PyErr_SetString(PyExc_TypeError, "Argument must be callable.");
    return NULL;
  }
  co = PyObject_New(cacheobject, &cache_type);
  if (co == NULL)
    return NULL;

#ifdef WITH_THREAD
  if ((co->lock = PyThread_allocate_lock()) == NULL){
    Py_DECREF(co);
    return NULL;
  }
  // We need to initialize the rlock count and owner here
  co->rlock_count = 0;
  co->rlock_owner = 0;
#endif
  if ((co->cache_dict = PyDict_New()) == NULL){
    Py_DECREF(co);
    return NULL;
  }

  // initialize circular doubly linked list
  co->root = PyObject_New(clist, &clist_type);
  if(co->root == NULL){
    Py_DECREF(co);
    return NULL;
  }

  // get namedtuple for cache_info()
  mod = PyImport_ImportModule("collections");
  if (mod == NULL){
    Py_DECREF(co);
    return NULL;
  }
  nt = PyObject_GetAttrString(mod, "namedtuple");
  if (nt == NULL){
    Py_DECREF(co);
    return NULL;
  }
  co->cinfo = PyObject_CallFunction(nt,"ss","CacheInfo",
                                    "hits misses maxsize currsize");
  if (co->cinfo == NULL){
    Py_DECREF(co);
    return NULL;
  }

  co->func_dict = get_func_attr(fo, "__dict__");

  co->fn = fo; // __wrapped__
  Py_INCREF(co->fn);

  co->func_module = get_func_attr(fo, "__module__");
  co->func_annotations = get_func_attr(fo, "__annotations__");
  co->func_name = get_func_attr(fo, "__name__");
  co->func_qualname = get_func_attr(fo, "__qualname__");

  co->ex_state = lru->state;
  Py_INCREF(co->ex_state);
  co->maxsize = lru->maxsize;
  co->hits = 0;
  co->misses = 0;
  co->typed = lru->typed;
  co->err = lru->err;
  // start with self-referencing root node
  co->root->prev = co->root;
  co->root->next = co->root;
  co->root->key = Py_None;
  co->root->result = Py_None;
  Py_INCREF(co->root->key);
  Py_INCREF(co->root->result);

  return (PyObject *)co;
}


static PyTypeObject lru_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "fastcache.lru",                /* tp_name */
    sizeof(lruobject),                       /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)lru_dealloc,            /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    (ternaryfunc)lru_call,              /* tp_call */
    0,                                  /* tp_str */
    0,                                  /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT ,                /* tp_flags */
};


/* helper function for processing 'unhashable' */
enum unhashable
process_uh(PyObject *arg, PyObject *(*f)(const char *))
{
  PyObject *uh[3] = {f("error"), f("warning"), f("ignore")};
  int i, j;
  if (arg != NULL){

    enum unhashable vals[3] = {FC_ERROR, FC_WARNING, FC_IGNORE};

    for(i=0; i<3; i++){
      int k = PyObject_RichCompareBool(arg, uh[i], Py_EQ);
      if (k < 0){
        for(j=0; j<3; j++)
          Py_DECREF(uh[j]);
        return FC_FAIL;
      }
      if (k){
        /* DECREF objects and return value */
        for(j=0; j<3; j++)
          Py_DECREF(uh[j]);
        return vals[i];
      }
    }
  }
  for(j=0; j<3; j++)
    Py_DECREF(uh[j]);
  PyErr_SetString(PyExc_TypeError,
               "Argument <unhashable> must be 'error', 'warning', or 'ignore'");
  return FC_FAIL;
}


/* LRU cache decorator */
PyDoc_STRVAR(lrucache__doc__,
"clru_cache(maxsize=128, typed=False, state=None, unhashable='error')\n\n"
"Least-recently-used cache decorator.\n\n"
"If *maxsize* is set to None, the LRU features are disabled and the\n"
"cache can grow without bound.\n\n"
"If *typed* is True, arguments of different types will be cached\n"
"separately.  For example, f(3.0) and f(3) will be treated as distinct\n"
"calls with distinct results.\n\n"
"If *state* is a list or dict, the items will be incorporated into the\n"
"argument hash.\n\n"
"The result of calling the cached function with unhashable (mutable)\n"
"arguments depends on the value of *unhashable*:\n\n"
"    If *unhashable* is 'error', a TypeError will be raised.\n\n"
"    If *unhashable* is 'warning', a UserWarning will be raised, and\n"
"    the wrapped function will be called with the supplied arguments.\n"
"    A miss will be recorded in the cache statistics.\n\n"
"    If *unhashable* is 'ignore', the wrapped function will be called\n"
"    with the supplied arguments. A miss will will be recorded in\n"
"    the cache statistics.\n\n"
"View the cache statistics named tuple (hits, misses, maxsize, currsize)\n"
"with f.cache_info().  Clear the cache and statistics with\n"
"f.cache_clear(). Access the underlying function with f.__wrapped__.\n\n"
"See:  http://en.wikipedia.org/wiki/Cache_algorithms#Least_Recently_Used");

static PyObject *
lrucache(PyObject *self, PyObject *args, PyObject *kwargs)
{
  PyObject *state = Py_None;
  int typed = 0;
  PyObject *omaxsize = Py_False;
  PyObject *oerr = Py_None;
  Py_ssize_t maxsize = 128;
  static char *kwlist[] = {"maxsize", "typed", "state", "unhashable", NULL};
  lruobject *lru;
  enum unhashable err;
#if defined(_PY2) || defined (_PY32)
  PyObject *otyped = Py_False;
  if(! PyArg_ParseTupleAndKeywords(args, kwargs, "|OOOO:lrucache",
                                   kwlist,
                                   &omaxsize, &otyped, &state, &oerr))
    return NULL;
  typed = PyObject_IsTrue(otyped);
  if (typed < -1)
    return NULL;
#else
  if(! PyArg_ParseTupleAndKeywords(args, kwargs, "|OpOO:lrucache",
                                   kwlist,
                                   &omaxsize, &typed, &state, &oerr))
    return NULL;
#endif
  if (omaxsize != Py_False){
    if (omaxsize == Py_None)
      maxsize = -1;
#ifdef _PY2
    else if (PyInt_Check(omaxsize)){
      maxsize = PyInt_AsSsize_t(omaxsize);
      if (maxsize < 0)
        maxsize = -1;
    }
#endif
    else {
      if( ! PyLong_Check(omaxsize)){
        PyErr_SetString(PyExc_TypeError,
                        "Argument <maxsize> must be an int.");
        return NULL;
      }
      maxsize = PyLong_AsSsize_t(omaxsize);
      if (maxsize < 0)
        maxsize = -1;
    }
  }

  // ensure state is a list or dict
  if (state != Py_None && !(PyList_Check(state) || PyDict_CheckExact(state))){
    PyErr_SetString(PyExc_TypeError,
                    "Argument <state> must be a list or dict.");
    return NULL;
  }

  // check unhashable
  if (oerr == Py_None)
    err = FC_ERROR;
  else{
#ifdef _PY2
    if(PyString_Check(oerr))
      err = process_uh(oerr, PyString_FromString);
    else
#endif
    if(PyUnicode_Check(oerr))
      err = process_uh(oerr, PyUnicode_FromString);
    else
      err = process_uh(NULL, NULL); // set error properly
  }
  if (err == FC_FAIL)
    return NULL;

  lru = PyObject_New(lruobject, &lru_type);
  if (lru == NULL)
    return NULL;

  lru->maxsize = maxsize;
  lru->state = state;
  lru->typed = typed;
  lru->err = err;
  Py_INCREF(lru->state);

  return (PyObject *) lru;
}


static PyMethodDef lrucachemethods[] = {
  {"clru_cache", (PyCFunction) lrucache, METH_VARARGS | METH_KEYWORDS,
   lrucache__doc__},
  {NULL, NULL} /* sentinel */
};


#ifndef _PY2
static PyModuleDef lrucachemodule = {
  PyModuleDef_HEAD_INIT,
  "_lrucache",
  "Least Recently Used cache",
  -1,
  lrucachemethods,
  NULL, NULL, NULL, NULL
};
#endif


#ifndef PyMODINIT_FUNC  /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
#ifdef _PY2
init_lrucache(void)
{
#define _PYINIT_ERROR_RET return
#else
PyInit__lrucache(void)
{
  PyObject *m;
#define _PYINIT_ERROR_RET return NULL
#endif

  lru_type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&lru_type) < 0)
    _PYINIT_ERROR_RET;

  cache_type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&cache_type) < 0)
    _PYINIT_ERROR_RET;

  HashedArgs_type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&HashedArgs_type) < 0)
    _PYINIT_ERROR_RET;

  clist_type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&clist_type) < 0)
    _PYINIT_ERROR_RET;

#ifdef _PY2
  Py_InitModule3("_lrucache", lrucachemethods,
                 "Least recently used cache.");
#else
  m = PyModule_Create(&lrucachemodule);
  if (m == NULL)
    return NULL;
#endif

  Py_INCREF(&lru_type);
  Py_INCREF(&cache_type);
  Py_INCREF(&HashedArgs_type);
  Py_INCREF(&clist_type);

#ifndef _PY2
  return m;
#endif
}

#ifdef __cplusplus
}
#endif
