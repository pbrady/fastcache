#include <Python.h>
#include "structmember.h"

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

/* hashseq -- internal *****************************************/

typedef struct {
  PyListObject list;
  Py_hash_t hashvalue;
} hashseq;

/* hashseq_{traverse, clear, dealloc} are copied from PyListObject 
 * are they needed here?
 */
static int
hashseq_traverse(hashseq *self, visitproc visit, void *arg)
{
  Py_ssize_t i;
  
  for (i = Py_SIZE(self); --i >= 0; )
    Py_VISIT(((PyListObject *)self)->ob_item[i]);
  return 0;

}

static int
hashseq_clear(hashseq *self)
{
    Py_ssize_t i;
    PyListObject *a = (PyListObject *)self;
    PyObject **item = a->ob_item;
    if (item != NULL) {
        /* Because XDECREF can recursively invoke operations on
           this list, we make it empty first. */
        i = Py_SIZE(a);
        Py_SIZE(a) = 0;
        a->ob_item = NULL;
        a->allocated = 0;
        while (--i >= 0) {
            Py_XDECREF(item[i]);
        }
        PyMem_FREE(item);
    }
    /* Never fails; the return value can be ignored.
       Note that there is no guarantee that the list is actually empty
       at this point, because XDECREF may have populated it again! */
    return 0;
}

static void
hashseq_dealloc(hashseq *self)
{
  PyListObject *lo;
  Py_ssize_t i;

  lo = (PyListObject *)self;

  PyObject_GC_UnTrack(self);
  Py_TRASHCAN_SAFE_BEGIN(self);

  if (lo->ob_item != NULL){
    i = Py_SIZE(lo);
    while(--i >=0) {
      Py_XDECREF(lo->ob_item[i]);
    }
    PyMem_FREE(lo->ob_item);
  }
  Py_TYPE(self)->tp_free((PyObject *)self);

  Py_TRASHCAN_SAFE_END(self)
}

/* return precomputed tuple hash for speed */
static Py_hash_t
HS_hash(hashseq *self)
{
  return self->hashvalue;
}

/* copied from PyListObject
 * hashseq's are internal objects which are only compared with 
 * other hashseq's when the hashes are the same.  
 * Furthermore, lookdict only calls this routine with op==Py_EQ */
static PyObject *
hashseq_richcompare(PyObject *v, PyObject *w, int op)
{
    PyListObject *vl, *wl;
    Py_ssize_t i;

    // should never happen
    if (op != Py_EQ){
      PyErr_SetString(PyExc_TypeError, "HashSeq object only support == comparison.");
      return NULL;      
    }

    vl = (PyListObject *)v;
    wl = (PyListObject *)w;

    if (Py_SIZE(vl) != Py_SIZE(wl)) {
        /* Shortcut: if the lengths differ, the lists differ */
      return Py_INCREF(Py_False), Py_False;
    }

    /* Search for the first index where items are different */
    for (i = 0; i < Py_SIZE(vl) && i < Py_SIZE(wl); i++) {
        int k = PyObject_RichCompareBool(vl->ob_item[i],
                                         wl->ob_item[i], Py_EQ);
        if (k < 0)
	  return NULL;
        if (!k)
	  return Py_INCREF(Py_False), Py_False;
    }
    
    // if we got here all items are equal
    return Py_INCREF(Py_True), Py_True;
}


static PyTypeObject hashseq_type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "_lrucache.hashseq",          /* tp_name */
  sizeof(hashseq),              /* tp_basicsize */
  0,                            /* tp_itemsize */
  (destructor)hashseq_dealloc,  /* tp_dealloc */
  0,                            /* tp_print */
  0,                            /* tp_getattr */
  0,                            /* tp_setattr */
  0,                            /* tp_reserved */
  0,                            /* tp_repr */
  0,                            /* tp_as_number */
  0,                            /* tp_as_sequence */
  0,                            /* tp_as_mapping */
  (hashfunc)HS_hash,            /* tp_hash */
  0,                            /* tp_call */
  0,                            /* tp_str */
  0,                            /* tp_getattro */
  0,                            /* tp_setattro */
  0,                            /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT |
  Py_TPFLAGS_HAVE_GC,             /* tp_flags */
  0,                              /* tp_doc */
  (traverseproc)hashseq_traverse, /* tp_traverse */
  (inquiry)hashseq_clear,         /* tp_clear */
  hashseq_richcompare,     /* tp_richcompare */
  0,                       /* tp_weaklistoffset */
  0,                       /* tp_iter */
  0,                       /* tp_iternext */
  0,                       /* tp_methods */
  0,                       /* tp_members */
  0,                       /* tp_getset */
  0,                       /* tp_base */
  0,                       /* tp_dict */
  0,                       /* tp_descr_get */
  0,                       /* tp_descr_set */
  0,                       /* tp_dictoffset */
  0,                       /* tp_init */
  0,                       /* tp_alloc */
  0,                       /* tp_new */
  PyObject_GC_Del,         /* tp_free */
};

/***************************************************
 End of _hashedseq
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

  Py_XDECREF(co->key);
  Py_XDECREF(co->result);

  if(prev == co){
    // remove self referencing node
    co->prev = NULL;
    co->next = NULL;
    Py_TYPE(co)->tp_free(co);
  }
  else{
    // adjust neighbor pointers
    prev->next = next;
    next->prev = prev;
    co->prev = NULL;
    co->next = NULL;
    Py_TYPE(co)->tp_free(co);
  }

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
  0,                       /* tp_doc */
  0,                       /* tp_traverse */
  0,                       /* tp_clear */
  0,                      /* tp_richcompare */
  0,                       /* tp_weaklistoffset */
  0,                       /* tp_iter */
  0,                       /* tp_iternext */
  0,                       /* tp_methods */
  0,                       /* tp_members */
  0,                       /* tp_getset */
  0,                       /* tp_base */
  0,                       /* tp_dict */
  0,                       /* tp_descr_get */
  0,                       /* tp_descr_set */
  0,                       /* tp_dictoffset */
  0,                       /* tp_init */
  0,                       /* tp_alloc */
  0,                       /* tp_new */
};

static int
insert_first(clist *root, PyObject *key, PyObject *result){
  // first element will be inserted at root->next
  clist *oldfirst = root->next;
  clist *first = PyObject_New(clist, &clist_type);
  if(first == NULL)
    return -1;
  // INCREF result since it will be used by clist and returned to the caller
  Py_INCREF(result);
  first->result = result;
  // This will be the only reference to key (hashseq), do not INCREF
  first->key = key;

  root->next = first;
  first->next = oldfirst;
  first->prev = root;
  oldfirst->prev = first;

  return 1;
  
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

  Py_INCREF(node->result);
  return node->result;
}

/**********************************************************
 cachedobject is the actual function with the cached results
***********************************************************/

typedef struct {
  PyObject_HEAD
  PyObject *fn ; // original function
  PyObject *func_module, *func_name, *func_qualname, *func_annotations;
  PyObject *func_dict;
  PyObject *cache_dict; 
  PyObject *ex_state; 
  int typed;
  PyObject *cinfo; // named tuple constructor
  Py_ssize_t maxsize, hits, misses;
  clist *root;
} cacheobject ;

#define OFF(x) offsetof(cacheobject, x)

// attributes from wrapped function
static PyMemberDef cache_memberlist[] = {
  {"__wrapped__", T_OBJECT, OFF(fn), RESTRICTED | READONLY},
  {"__module__",  T_OBJECT, OFF(func_module), RESTRICTED | READONLY},
  {"__name__",    T_OBJECT, OFF(func_name), RESTRICTED | READONLY},
  {"__qualname__",T_OBJECT, OFF(func_qualname), RESTRICTED | READONLY},
  {"__annotations__", T_OBJECT, OFF(func_annotations), RESTRICTED | READONLY},
  {"__dict__", T_OBJECT, OFF(func_dict), 0},
  {NULL} /* Sentinel */
};
// getsetters from wrapped function
static PyObject *
cache_get_doc(cacheobject * co, void *closure)
{
  PyFunctionObject * fn = (PyFunctionObject *) co->fn;
  if (fn->func_doc == NULL)
    Py_RETURN_NONE;
  
  return Py_INCREF(fn->func_doc), fn->func_doc;
}

static PyGetSetDef cache_getset[] = {
  {"__doc__", (getter)cache_get_doc, NULL, NULL, NULL},
  {NULL} /* Sentinel */
};


/* Bind a function to an object */
static PyObject *
cache_descr_get(PyObject *func, PyObject *obj, PyObject *type)
{
    if (obj == Py_None || obj == NULL) {
        Py_INCREF(func);
        return func;
    }
#ifdef _PY2
    return PyMethod_New(func, obj, type);
#else
    return PyMethod_New(func, obj);
#endif
}

static void cache_dealloc(cacheobject *co)
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
  Py_TYPE(co)->tp_free(co);
  
}

static PyObject *
make_key(cacheobject *co, PyObject *args, PyObject *kw)
{
  PyObject *tup, *item, *keys, *key;
  Py_ssize_t ex_size = 0;
  Py_ssize_t arg_size = 0;
  Py_ssize_t kw_size = 0;
  Py_ssize_t i,size,off;
  size_t nbytes;
  hashseq *hs;
  PyListObject *lo;

  // determine size of arguments and types
  if (PyList_Check(co->ex_state))
    ex_size = Py_SIZE(co->ex_state);
  if (args != NULL && PyTuple_CheckExact(args))
    arg_size = PyTuple_GET_SIZE(args);
  if (kw != NULL && PyDict_CheckExact(kw))
    kw_size = PyDict_Size(kw);

  // allocate hashseq
  hs = PyObject_GC_New(hashseq, &hashseq_type);
  if( hs == NULL)
    return NULL;
  // total size
  if (co->typed)
    size = ex_size+2*arg_size+3*kw_size;
  else
    size = ex_size+arg_size+2*kw_size;
  // cast hashseq to list and initialize
  lo = (PyListObject *) hs;
  nbytes = size * sizeof(PyObject *);
  lo->ob_item = (PyObject **) PyMem_MALLOC(nbytes);
  if(lo->ob_item == NULL){
    Py_DECREF(hs);
    return PyErr_NoMemory();
  }
  memset(lo->ob_item, 0, nbytes);
  
  Py_SIZE(lo) = size;
  lo->allocated = size;
  _PyObject_GC_TRACK(hs);
  // incorporate extra state
  for(i = 0; i < ex_size; i++){
    item = PyList_GET_ITEM(co->ex_state, i);
    Py_INCREF(item);
    PyList_SET_ITEM((PyObject *)hs, i, item);
  }
  // incorporate arguments
  for(i = 0; i < arg_size; i++){
    item = PyTuple_GET_ITEM(args, i);
    Py_INCREF(item);
    PyList_SET_ITEM((PyObject *)hs, ex_size+i, item);
  }
  off = ex_size + arg_size;
  // incorporate type
  if (co->typed){
    for(i = 0; i < arg_size; i++){
      item = (PyObject *)Py_TYPE(PyTuple_GET_ITEM(args, i));
      Py_INCREF(item);
      PyList_SET_ITEM((PyObject *)hs, off+i, item);
    }
    off += arg_size;
  }
  
  // incorporate keyword arguments
  if(kw_size > 0){
    keys = PyDict_Keys(kw);
    if( PyList_Sort(keys) < 0){
      Py_DECREF(hs);
      return NULL;
    }
    for(i = 0; i < kw_size; i++){
      key = PyList_GET_ITEM(keys, i);
      item = PyDict_GetItem(kw, key);
      Py_INCREF(key);
      Py_INCREF(item);
      PyList_SET_ITEM((PyObject *)hs, off+2*i  , key);
      PyList_SET_ITEM((PyObject *)hs, off+2*i+1, item);
    }
    Py_DECREF(keys);
    // type info
    if (co->typed){
      for(i = 0; i < kw_size; i++){
	item = (PyObject *)Py_TYPE(PyList_GET_ITEM((PyObject *)hs, off+2*i+1));
	Py_INCREF(item);
	PyList_SET_ITEM((PyObject *)hs, off+2*kw_size+i, item);
      }
    }

  }

  tup = PyList_AsTuple((PyObject *)hs);
  if (tup == NULL){
    Py_DECREF(hs);
    return NULL;
  }
  // set hashvalue
  hs->hashvalue = PyTuple_Type.tp_hash(tup);
  if( hs->hashvalue == -1)
    PyErr_Clear();
  Py_DECREF(tup);

  return (PyObject *)hs;
}

static PyObject *
cache_call(cacheobject *co, PyObject *args, PyObject *kw)
{
  PyObject *key, *result, *link;

  /* no cache, just update stats and return */
  if (co->maxsize == 0) {
    co->misses++;
    return PyObject_Call(co->fn, args, kw);
  }

  /* generate a key from hashing the arguments */
  key = make_key(co,args,kw);
  if (key == NULL)
    return NULL;

  /* check for unhashable type, error has already been cleared in make_key */
  if ( ((hashseq *)key)->hashvalue == -1){
    Py_DECREF(key);

    // try to issue warning
    if( PyErr_WarnEx(PyExc_UserWarning, 
		     "Unhashable arguments cannot be cached",1) < 0){
      // warning becomes exception
      PyErr_SetString(PyExc_TypeError, "Cached function arguments must be hashable");
      return NULL;
    }
    co->misses++;
    return PyObject_Call(co->fn, args, kw);
  }

  /* For an unbounded cache, link is simply the result of the function call
   * For an LRU cache, link is a pointer to a clist node */
  link = PyDict_GetItem(co->cache_dict, key);

  if (link == NULL){
    result = PyObject_Call(co->fn, args, kw);
    if(result == NULL){
      Py_DECREF(key);
      return NULL;
    }
    /* Unbounded cache, no clist maintenance */
    if (co->maxsize < 0){
      PyDict_SetItem(co->cache_dict, key, result);
      Py_DECREF(key);
      }
    /* Least Recently Used cache */
    else {
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
	Py_INCREF(co->root->next);
	// handle deletions
	PyDict_DelItem(co->cache_dict,old_key);
	Py_DECREF(old_key);
	Py_DECREF(old_res);
      }
      else {
	if(insert_first(co->root, key, result) < 0) {
	  Py_DECREF(key);
	  Py_DECREF(result);
	  return NULL;
	}
      }
      PyDict_SetItem(co->cache_dict, key, (PyObject *) co->root->next);
      Py_DECREF(co->root->next);
    }
    co->misses++;

    return result;
    
  } // link != NULL
  else {
    Py_DECREF(key);
    if( co->maxsize < 0){
      result = link;
      Py_INCREF(result);
    }
    else
      /* bump link to the front of the list and get result from link */
      result = make_first(co->root, (clist *) link);

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
  // delete current dictionary
  PyDict_Clear(co->cache_dict);
  co->hits = 0;
  co->misses = 0;

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
    return PyObject_CallFunction(co->cinfo,"nnnn",co->hits, co->misses, co->maxsize,
				 ((PyDictObject *)co->cache_dict)->ma_used);
  else
    return PyObject_CallFunction(co->cinfo,"nnOn",co->hits, co->misses, Py_None,
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
    "_lrucache.cache",                /* tp_name */
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
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    0,                                  /* tp_new */
    0,                                  /* tp_free */
};




/***********************************************************
 LRU cache returns a simple callable which will take a function
 as an argument
************************************************************/

typedef struct {
  PyObject_HEAD
  Py_ssize_t maxsize;
  PyObject *state;
  int typed;
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

// simple caller which prints some business and returns the arg if
// it is a function
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

  if ((co->cache_dict = PyDict_New()) == NULL)
    return NULL;

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
  co->cinfo = PyObject_CallFunction(nt,"ss","CacheInfo","hits misses maxsize currsize");
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
    "_lrucache.lru",                /* tp_name */
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
    0,                                  /* tp_doc */
    0,                                  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    0,                                  /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    0,                                  /* tp_new */
    0,                                  /* tp_free */
};

/* LRU cache decorator */
PyDoc_STRVAR(lrucache__doc__,
"lrucache(maxsize=128, typed=False, state=None)\n\
\n\
Least-recently-used cache decorator.\n\
\n\
If *maxsize* is set to None, the LRU features are disabled and the cache\n\
can grow without bound.\n\
\n\
If *typed* is True, arguments of different types will be cached separately.\n\
For example, f(3.0) and f(3) will be treated as distinct calls with\n\
distinct results.\n\
\n\
If *state* is a list, the items in the list will be incorporated into\n\
argument hash.\n\
\n\
Arguments to the cached function must be hashable.\n\
\n\
View the cache statistics named tuple (hits, misses, maxsize, currsize)\n\
with f.cache_info().  Clear the cache and statistics with f.cache_clear().\n\
Access the underlying function with f.__wrapped__.\n\
\n\
See:  http://en.wikipedia.org/wiki/Cache_algorithms#Least_Recently_Used");
static PyObject *
lrucache(PyObject *self, PyObject *args, PyObject *kwargs)
{
  PyObject *state = Py_None;
  int typed = 0;
  PyObject *omaxsize = Py_False;
  Py_ssize_t maxsize = 128;
  static char *kwlist[] = {"maxsize", "typed", "state"};
  lruobject *lru;
#if defined(_PY2) || defined (_PY32)
  PyObject *otyped = Py_False;
  if(! PyArg_ParseTupleAndKeywords(args, kwargs, "|OOO:lrucache",
				   kwlist,
				   &omaxsize, &otyped, &state))
    return NULL;
  typed = PyObject_IsTrue(otyped);
  if (typed < -1)
    return NULL;
#else
  if(! PyArg_ParseTupleAndKeywords(args, kwargs, "|OpO:lrucache",
				   kwlist,
				   &omaxsize, &typed, &state))
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
    
  // ensure state is a list
  if (state != Py_None && !PyList_Check(state)){
    PyErr_SetString(PyExc_TypeError,
		    "Argument <state> must be a list.");
    return NULL;
  }

  lru = PyObject_New(lruobject, &lru_type);
  if (lru == NULL)
    return NULL;
  
  lru->maxsize = maxsize;
  lru->state = state;
  lru->typed = typed;
  Py_INCREF(lru->state);

  return (PyObject *) lru;
  
  
}

static PyMethodDef lrucachemethods[] = {
  {"lrucache", (PyCFunction) lrucache, METH_VARARGS | METH_KEYWORDS,
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

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
#ifdef _PY2
init_lrucache(void)
#else
PyInit__lrucache(void)
#endif
{
  PyObject *m;

  lru_type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&lru_type) < 0){
#ifdef _PY2
    return;
#else
    return NULL;
#endif
  }
  cache_type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&cache_type) < 0){
#ifdef _PY2
    return;
#else
    return NULL;
#endif
  }

  hashseq_type.tp_base = &PyList_Type;
  if (PyType_Ready(&hashseq_type) < 0){
#ifdef _PY2
    return;
#else
    return NULL;
#endif
  }

  clist_type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&clist_type) < 0){
#ifdef _PY2
    return;
#else
    return NULL;
#endif
  }

#ifdef _PY2
  m = Py_InitModule3("_lrucache", lrucachemethods,
                       "Least recently used cache.");
#else
  m = PyModule_Create(&lrucachemodule);

  if (m == NULL)
    return NULL;
#endif
  Py_INCREF(&lru_type);
  Py_INCREF(&cache_type);
  Py_INCREF(&hashseq_type);
  Py_INCREF(&clist_type);

#ifndef _PY2
  return m;
#endif
}

#ifdef __cplusplus
}
#endif
