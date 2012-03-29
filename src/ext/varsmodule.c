#include "python_zibopt.h"
#include "python_zibopt_error.h"

static PyObject *error;

/*****************************************************************************/
/* PYTHON TYPE METHODS                                                       */
/*****************************************************************************/
static int variable_init(variable *self, PyObject *args, PyObject *kwds) {
    static char *argnames[] = {"solver", "vartype", "coefficient", "lower", "upper", "priority", NULL};
    PyObject *s;     // solver Python object
    solver *solv;    // solver C object
    double c;        // coefficient
    int t;           // integer / binary / continuous
    double lhs, rhs; // lhs <= a'x <= rhs
    int priority;    // variable branching priority
    
    // SCIPinfinity requires self->scip, so we have to parse the args twice
    if (!PyArg_ParseTuple(args, "O|idddi", &s, &t, &c, &lhs, &rhs, &priority))
        return -1;

    // Check solver type in the best way we seem to have available
    if (strcmp(s->ob_type->tp_name, SOLVER_TYPE_NAME)) {
        PyErr_SetString(error, "invalid solver type");
        return -1;
    }
    
    solv = (solver *) s;
    self->scip = solv->scip;

    // Defaults
    t = SCIP_VARTYPE_CONTINUOUS;
    lhs = -SCIPinfinity(self->scip);
    rhs = SCIPinfinity(self->scip);
    priority = 0;
    
    // This time is just to get the upper and lower bounds out, plus priority
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|idddi", argnames, &s, &t, &c, &lhs, &rhs, &priority))
        return -1;

    // Variable type
    if (t != SCIP_VARTYPE_BINARY && t != SCIP_VARTYPE_INTEGER && t != SCIP_VARTYPE_IMPLINT) {
        t = SCIP_VARTYPE_CONTINUOUS;
    } else if (t == SCIP_VARTYPE_BINARY) {
        if (lhs < 0)
            lhs = 0;
        if (rhs > 1)
            rhs = 1;
    }
    
    // SCIPcreateVar Arguments:
    // scip         SCIP data structure
    // var          pointer to variable object
    // name         name of variable, or NULL for automatic name creation
    // lb           lower bound of variable
    // ub           upper bound of variable
    // obj          objective function value
    // vartype      type of variable
    // initial      should var's column be present in the initial root LP?
    // removable    is var's column removable from the LP?
    // vardelorig   frees user data of original variable, or NULL
    // vartrans     creates transformed user data by transforming original user data, or NULL
    // vardeltrans  frees user data of transformed variable, or NULL
    // varcopy      copies variable data if wanted to subscip, or NULL
    // vardata      user data for this specific variable 
    PY_SCIP_CALL(error, -1, 
        SCIPcreateVar(self->scip, &self->variable, NULL, lhs, rhs, c, t,
            TRUE, FALSE, NULL, NULL, NULL, NULL, NULL)
    );

    self->lower = lhs;
    self->upper = rhs;

    PY_SCIP_CALL(error, -1, SCIPaddVar(self->scip, self->variable));

    if (priority != 0)
        PY_SCIP_CALL(error, -1, SCIPchgVarBranchPriority(self->scip, self->variable, priority));

    // Put new variable at head of linked list
    self->next = (struct variable *) solv->first_var;
    solv->first_var = self;

    return 0;
}

static void variable_dealloc(variable *self) {
    ((PyObject *) self)->ob_type->tp_free(self);
}

static PyObject* variable_getattr(variable *self, PyObject *attr_name) {
    // Check and make sure we have a string as attribute name...
    if (PyUnicode_Check(attr_name)) {
        if (PyUnicode_CompareWithASCIIString(attr_name, "priority") == 0)
            return Py_BuildValue("i", SCIPvarGetBranchPriority(self->variable));
    }
    return PyObject_GenericGetAttr((PyObject *) self, attr_name);
}

static int variable_setattr(variable *self, PyObject *attr_name, PyObject *value) {
    // Check and make sure we have a string as attribute name...
    if (PyUnicode_Check(attr_name)) {
        if (PyUnicode_CompareWithASCIIString(attr_name, "priority") == 0) {
            if (PyLong_Check(value)) {
                PY_SCIP_CALL(error, -1, SCIPchgVarBranchPriority(self->scip, self->variable, PyLong_AsLong(value)));
                return 0;
            } else {
                PyErr_SetString(error, "invalid value for variable branching priority");
                return -1;
            }
        }
    }
    return PyObject_GenericSetAttr((PyObject *) self, attr_name, value);
}

/*****************************************************************************/
/* ADDITONAL METHODS                                                         */
/*****************************************************************************/
static PyObject *variable_set_coefficient(variable *self, PyObject *arg) {
    if (PyFloat_Check(arg) || PyLong_Check(arg)) {
        // SCIPvarChgObj Arguments:
        // var          variable to change
        // blkmem       block memory
        // set          global SCIP settings
        // primal       primal data
        // lp           current LP data
        // eventqueue 	event queue
        // newobj       new objective value for variable 
        PY_SCIP_CALL(error, NULL, 
            SCIPvarChgObj(self->variable, NULL, self->scip->set, NULL, NULL, NULL, 
                (SCIP_Real) PyFloat_AsDouble(arg))
        );
        Py_RETURN_NONE;
        
    } else {
        PyErr_SetString(error, "invalid objective coefficient");
        return NULL;
    }
}

static PyObject *variable_tighten_lower(variable *self, PyObject *arg) {
    double d;
    if (PyFloat_Check(arg) || PyLong_Check(arg)) {
        d = PyFloat_AsDouble(arg);
        if (d > self->lower) {
            PY_SCIP_CALL(error, NULL, 
                SCIPchgVarLb(self->scip, self->variable, (SCIP_Real) d)
            );
            self->lower = d;
        }
        Py_RETURN_NONE;
        
    } else {
        PyErr_SetString(error, "invalid lower bound");
        return NULL;
    }
}

static PyObject *variable_tighten_upper(variable *self, PyObject *arg) {
    double d;
    if (PyFloat_Check(arg) || PyLong_Check(arg)) {
        d = PyFloat_AsDouble(arg);
        if (d < self->upper) {
            PY_SCIP_CALL(error, NULL, 
                SCIPchgVarUb(self->scip, self->variable, (SCIP_Real) d)
            );
            self->upper = d;
        }
        Py_RETURN_NONE;
        
    } else {
        PyErr_SetString(error, "invalid upper bound");
        return NULL;
    }
}

/*****************************************************************************/
/* MODULE INITIALIZATION                                                     */
/*****************************************************************************/
static PyMethodDef variable_methods[] = {
    {"set_coefficient", (PyCFunction) variable_set_coefficient, METH_O, "updates objective coefficient for a variable"},
    {"tighten_lower_bound", (PyCFunction) variable_tighten_lower, METH_O, "adds a possible tightened lower bound for a variable"},
    {"tighten_upper_bound", (PyCFunction) variable_tighten_upper, METH_O, "adds a possible tightened upper bound for a variable"},
    {NULL} /* Sentinel */
};

static PyTypeObject variable_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_vars.variable",              /* tp_name */
    sizeof(variable),              /* tp_basicsize */
    0,                             /* tp_itemsize */
    (destructor) variable_dealloc, /* tp_dealloc */
    0,                             /* tp_print */
    0,                             /* tp_getattr */
    0,                             /* tp_setattr */
    0,                             /* tp_compare */
    0,                             /* tp_repr */
    0,                             /* tp_as_number */
    0,                             /* tp_as_sequence */
    0,                             /* tp_as_mapping */
    0,                             /* tp_hash */
    0,                             /* tp_call */
    0,                             /* tp_str */
    (getattrofunc) variable_getattr, /* tp_getattro */
    (setattrofunc) variable_setattr, /* tp_setattro */
    0,                             /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    "SCIP variable objects",       /* tp_doc */
    0,                             /* tp_traverse */
    0,                             /* tp_clear */
    0,                             /* tp_richcompare */
    0,                             /* tp_weaklistoffset */
    0,                             /* tp_iter */
    0,                             /* tp_iternext */
    variable_methods,              /* tp_methods */
    0,                             /* tp_members */
    0,                             /* tp_getset */
    0,                             /* tp_base */
    0,                             /* tp_dict */
    0,                             /* tp_descr_get */
    0,                             /* tp_descr_set */
    0,                             /* tp_dictoffset */
    (initproc) variable_init,      /* tp_init */
    0,                             /* tp_alloc */
    0,                             /* tp_new */
};

#if PY_MAJOR_VERSION >= 3
static PyModuleDef vars_module = {
    PyModuleDef_HEAD_INIT,
    "_vars",
    "SCIP Variable",
    -1,
    NULL, NULL, NULL, NULL, NULL
};
#endif

#ifndef PyMODINIT_FUNC    /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC PyInit__vars(void) {
    PyObject* m;

    variable_type.tp_new = PyType_GenericNew;
    if (PyType_Ready(&variable_type) < 0)
#if PY_MAJOR_VERSION >= 3
        return NULL;
#else
        return;
#endif

#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&vars_module); 
#else
    m = Py_InitModule3("_vars", NULL, "SCIP Variable");
#endif

    Py_INCREF(&variable_type);
    PyModule_AddObject(m, "variable", (PyObject *) &variable_type);

    // Initialize exception type
    error = PyErr_NewException("_vars.error", NULL, NULL);
    Py_INCREF(error);
    PyModule_AddObject(m, "error", error);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}

