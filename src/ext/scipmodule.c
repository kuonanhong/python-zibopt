#include "python_zibopt.h"
#include "python_zibopt_error.h"

static PyObject *error;

/*****************************************************************************/
/* PYTHON TYPE METHODS                                                       */
/*****************************************************************************/
static PyObject *solver_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    solver *self;
    self = (solver *) type->tp_alloc(type, 0);
    if (self != NULL) {
        // Initialize SCIP
        PY_SCIP_CALL(error, NULL, SCIPcreate(&self->scip));
        
        // Default plugins, heuristics, etc
        PY_SCIP_CALL(error, NULL, SCIPincludeDefaultPlugins(self->scip));
        
        // SCIPcreateProb Arguments:
        // scip         SCIP data structure
        // name         name of problem
        // probdelorig  callback to free original problem data
        // probtrans    callback to create transformed problem
        // probdeltrans callback to free that transformed problem
        // probinitsol  callback to create initial solution
        // probexitsol  callback to free initial solution
        // probcopy     callback to copy data to a subscip
        // probdata     initial problem data (vars & constraints)
        PY_SCIP_CALL(error, NULL, 
            SCIPcreateProb(self->scip, "python-zibobt", NULL, NULL, NULL, NULL, NULL, NULL, NULL)
        );

        // Keep SCIP from catching keyboard interrupts.  These go to python.
        self->scip->set->misc_catchctrlc = FALSE;
    }

    return (PyObject *) self;
}

static int solver_init(solver *self, PyObject *args, PyObject *kwds) {
    static char *argnames[] = {"quiet", NULL};
    bool quiet;
    
    quiet = true;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|b", argnames, &quiet))
        return -1;

    // Turn on/off solver chatter
    if (quiet) {
        PY_SCIP_CALL(error, -1, SCIPsetMessagehdlr(NULL));
    } else {
        PY_SCIP_CALL(error, -1, SCIPsetDefaultMessagehdlr());
    }

    return 0;
}

static void solver_dealloc(solver *self) {
    if (self->scip) {
        // Free all variables
        while (self->first_var != NULL) {
            SCIPreleaseVar(self->scip, &self->first_var->variable);
            self->first_var = (variable *) self->first_var->next;
        }
        self->first_var = NULL;
        
        // Free constraints
        while (self->first_cons != NULL) {
            SCIPreleaseCons(self->scip, &self->first_cons->constraint);
            self->first_cons = (constraint *) self->first_cons->next;
        }
        self->first_cons = NULL;
        
        // Free the solver itself
        SCIPfree(&self->scip);
        self->scip = NULL;
    }

    ((PyObject *) self)->ob_type->tp_free(self);
}

/*****************************************************************************/
/* ADDITONAL METHODS                                                         */
/*****************************************************************************/
static int _seed_primal(solver *self, PyObject *solution) {
    // Extracts data for a primal solution and hands it to SCIP
    PyObject *key, *value;
    Py_ssize_t pos;
    double d;
    SCIP_Bool feasible, stored;
    SCIP_SOL *sol = NULL;

    if (solution && PyObject_Length(solution) > 0) {
        // We were given a primal solution.  Feed it to the solver. But first,
        // check and make sure we got a dictionary of variables and numbers.
        pos = 0;
        while (PyDict_Next(solution, &pos, &key, &value)) {
            // Check and make sure we have a real variable type
            if (strcmp(key->ob_type->tp_name, VARIABLE_TYPE_NAME)) {
                PyErr_SetString(error, "invalid variable type");
                return 0;
            }
            
            // Verify that the variable is associated with this solver
            if (((variable *) key)->scip != self->scip) {
                PyErr_SetString(error, "variable not associated with solver");
                return 0;
            }
        
            // Check and make sure we have a number as the value
            if (!(PyFloat_Check(value) || PyLong_Check(value))) {
                PyErr_SetString(error, "solution values must be numeric");
                return 0;
            }
        }

        // Passed validation.  Now we can create the SCIP solution.
        SCIPtransformProb(self->scip);
        SCIPcreateSol(self->scip, &sol, NULL);

        // Add all the variables to it            
        pos = 0;
        while (PyDict_Next(solution, &pos, &key, &value)) {
            // Convert the value to a C double
            if (PyFloat_Check(value))
                d = PyFloat_AsDouble(value);
            else
                d = (double) PyLong_AsLong(value);
            
            // Only set nonzero values
            if (d)
                SCIPsetSolVal(self->scip, sol, ((variable *) key)->variable, d); 
        }

        PY_SCIP_CALL(error, 0, SCIPcheckSolOrig(self->scip, sol, &feasible, TRUE, FALSE));
        if (!feasible)
            PyErr_SetString(error, "infeasible primal solution");
        
        // SCIPtrySolFree Arguments:
        // scip 	        SCIP data structure
        // sol           	pointer to primal CIP solution; is cleared in function call
        // printreason      should all reasons of violations be printed
        // checkbounds 	    should the bounds of the variables be checked?
        // checkintegrality has integrality to be checked?
        // checklprows 	    have current LP rows to be checked?
        // stored           stores whether solution was feasible and good enough to keep 
        SCIPtrySolFree(self->scip, &sol, FALSE, FALSE, FALSE, FALSE, &stored);
        
        // We don't actually do anything with the value of stored for the
        // time being.  Other programs do an assert(stored) but we have
        // already tested it for feasibility and don't want to raise an
        // error if the primal objective value is too poor to use.
    }
    
    return 0;
}

static int _optimize(solver *self, PyObject *args, PyObject *kwds) {
    // Runs components of max/min that are the same
    static char *argnames[] = {"solution", "time", "gap", "absgap", "nsol", "offset", NULL};
    PyObject *solution;
    double time   = SCIP_DEFAULT_LIMIT_TIME;
    double gap    = SCIP_DEFAULT_LIMIT_GAP;
    double absgap = SCIP_DEFAULT_LIMIT_GAP;
    int nsol      = SCIP_DEFAULT_LIMIT_SOLUTIONS;
    double offset = 0;
    
    // See if we were given a primal solution dict
    solution = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O!dddid", argnames, &PyDict_Type, &solution, &time, &gap, &absgap, &nsol, &offset))
        return 0;

    _seed_primal(self, solution);
    if (PyErr_Occurred())
        return 0;

    // Set timeout & gap values, etc
    SCIPclockReset(self->scip->stat->solvingtime);
    self->scip->set->limit_time   = time;
    self->scip->set->limit_gap    = gap;
    self->scip->set->limit_absgap = absgap;
    self->scip->set->limit_solutions = nsol;
    self->scip->origprob->objoffset = offset;
    
    // This calls the actual optimization routine
    PY_SCIP_CALL(error, 0, SCIPsolve(self->scip));
    
    return 0;
}

static PyObject *solver_maximize(solver *self, PyObject *args, PyObject *kwds) {
    PY_SCIP_CALL(error, NULL, SCIPsetObjsense(self->scip, SCIP_OBJSENSE_MAXIMIZE));
    _optimize(self, args, kwds);
    if (PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *solver_minimize(solver *self, PyObject *args, PyObject *kwds) {
    PY_SCIP_CALL(error, NULL, SCIPsetObjsense(self->scip, SCIP_OBJSENSE_MINIMIZE));
    _optimize(self, args, kwds);
    if (PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *solver_restart(solver *self) {
    PY_SCIP_CALL(error, NULL, SCIPfreeTransform(self->scip));
    Py_RETURN_NONE;
}

static PyObject *solver_unconstrain(solver *self, PyObject *c) {
    // Removes a constraint from the solver
    constraint *cons; // constraint C object

    // Check solver type in the best way we seem to have available
    if (strcmp(c->ob_type->tp_name, CONSTRAINT_TYPE_NAME)) {
        PyErr_SetString(error, "invalid constraint type");
        return NULL;
    }
    
    cons = (constraint *) c;

    // Restart solver prior to removing the constraint so state is ok
    solver_restart(self);
    PY_SCIP_CALL(error, NULL, SCIPdelCons(self->scip, cons->constraint));

    Py_RETURN_NONE;
}

// Functions for pulling out lists of setting names by type
PY_SCIP_SETTING_NAMES(branching_names, nbranchrules, branchrules);
PY_SCIP_SETTING_NAMES(conflict_names, nconflicthdlrs, conflicthdlrs);
PY_SCIP_SETTING_NAMES(display_names, ndisps, disps);
PY_SCIP_SETTING_NAMES(heuristic_names, nheurs, heurs);
PY_SCIP_SETTING_NAMES(presolver_names, npresols, presols);
PY_SCIP_SETTING_NAMES(propagator_names, nprops, props);
PY_SCIP_SETTING_NAMES(selector_names, nnodesels, nodesels);
PY_SCIP_SETTING_NAMES(separator_names, nsepas, sepas);

/*****************************************************************************/
/* MODULE INITIALIZATION                                                     */
/*****************************************************************************/
static PyMethodDef solver_methods[] = {
    {"maximize", (PyCFunction) solver_maximize, METH_VARARGS | METH_KEYWORDS, "maximize the objective value"},
    {"minimize", (PyCFunction) solver_minimize, METH_VARARGS | METH_KEYWORDS, "minimize the objective value"},
    {"restart",  (PyCFunction) solver_restart,  METH_NOARGS,   "restart the solver"},
    {"unconstrain",  (PyCFunction) solver_unconstrain,  METH_O,   "remove a constraint"},
    {"branching_names",  (PyCFunction) branching_names,  METH_NOARGS, "returns a list of branching rule names"},
    {"conflict_names",   (PyCFunction) conflict_names,   METH_NOARGS, "returns a list of conflict handler names"},
    {"display_names",    (PyCFunction) display_names,    METH_NOARGS, "returns a list of display column names"},
    {"heuristic_names",  (PyCFunction) heuristic_names,  METH_NOARGS, "returns a list of heuristic names"},
    {"presolver_names",  (PyCFunction) presolver_names,  METH_NOARGS, "returns a list of presolver names"},
    {"propagator_names", (PyCFunction) propagator_names, METH_NOARGS, "returns a list of propagator names"},
    {"selector_names",   (PyCFunction) selector_names,   METH_NOARGS, "returns a list of node selector names"},
    {"separator_names",  (PyCFunction) separator_names,  METH_NOARGS, "returns a list of separator names"},
    {NULL} /* Sentinel */
};

static PyTypeObject solver_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_scip.solver",              /* tp_name */
    sizeof(solver),              /* tp_basicsize */
    0,                           /* tp_itemsize */
    (destructor) solver_dealloc, /* tp_dealloc */
    0,                           /* tp_print */
    0,                           /* tp_getattr */
    0,                           /* tp_setattr */
    0,                           /* tp_compare */
    0,                           /* tp_repr */
    0,                           /* tp_as_number */
    0,                           /* tp_as_sequence */
    0,                           /* tp_as_mapping */
    0,                           /* tp_hash */
    0,                           /* tp_call */
    0,                           /* tp_str */
    0,                           /* tp_getattro */
    0,                           /* tp_setattro */
    0,                           /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    "SCIP solver objects",       /* tp_doc */
    0,                           /* tp_traverse */
    0,                           /* tp_clear */
    0,                           /* tp_richcompare */
    0,                           /* tp_weaklistoffset */
    0,                           /* tp_iter */
    0,                           /* tp_iternext */
    solver_methods,              /* tp_methods */
    0,                           /* tp_members */
    0,                           /* tp_getset */
    0,                           /* tp_base */
    0,                           /* tp_dict */
    0,                           /* tp_descr_get */
    0,                           /* tp_descr_set */
    0,                           /* tp_dictoffset */
    (initproc) solver_init,      /* tp_init */
    0,                           /* tp_alloc */
    solver_new,                  /* tp_new */
};

#if PY_MAJOR_VERSION >= 3
static PyModuleDef scip_module = {
    PyModuleDef_HEAD_INIT,
    "_scip",
    "SCIP Solver",
    -1,
    NULL, NULL, NULL, NULL, NULL
};
#endif

#ifndef PyMODINIT_FUNC    /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC PyInit__scip(void) {
    PyObject* m;

    if (PyType_Ready(&solver_type) < 0)
#if PY_MAJOR_VERSION >= 3
        return NULL;
#else
        return;
#endif

#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&scip_module); 
#else
    m = Py_InitModule3("_scip", NULL, "SCIP Solver");
#endif

    // Constants on scip module
    PyModule_AddIntConstant(m, "BINARY", SCIP_VARTYPE_BINARY);
    PyModule_AddIntConstant(m, "INTEGER", SCIP_VARTYPE_INTEGER);
    PyModule_AddIntConstant(m, "IMPLINT", SCIP_VARTYPE_IMPLINT);
    PyModule_AddIntConstant(m, "CONTINUOUS", SCIP_VARTYPE_CONTINUOUS);

    Py_INCREF(&solver_type);
    PyModule_AddObject(m, "solver", (PyObject *) &solver_type);

    // Initialize exception type
    error = PyErr_NewException("_scip.error", NULL, NULL);
    Py_INCREF(error);
    PyModule_AddObject(m, "error", error);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}

