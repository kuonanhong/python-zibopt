from zibopt import scip, _vars, _cons
import unittest

class ScipTest(unittest.TestCase):
    def testLoadSolver(self):
        '''Try loading the SCIP solver'''
        solver = scip.solver()

    def testMax(self):
        '''Maximize an objective subject to integer constraints'''
        solver = scip.solver()
        x1 = solver.variable(scip.INTEGER)
        x2 = solver.variable(scip.INTEGER)
        x3 = solver.variable(scip.INTEGER)
        
        solver += x1 <= 2
        solver += x1 + x2 + 3*x3 <= 3
        solution = solver.maximize(objective=x1+x2+2*x3)

        self.assertTrue(solution)
        self.assertAlmostEqual(solution.objective, 3)
        self.assertAlmostEqual(solution[x3], 0)
        
    def testAddVarConsError(self):
        '''Test that out-of-stage operations raise appropriate errors'''
        solver = scip.solver()
        solver.minimize()
        self.assertRaises(scip.VariableError, solver.variable)

    def testBadSolverType(self):
        '''Test that solvers must be properly passed'''
        solver = scip.solver()
        self.assertRaises(scip.VariableError, _vars.variable, object(), 0)
        self.assertRaises(scip.ConstraintError, _cons.constraint, object(), 
            [], [], [], [], [])

    def testVariableInfeasible(self):
        '''Solutions should raise error on infeasible variables'''
        solver = scip.solver()
        x1 = solver.variable()
        self.assertRaises(scip.ConstraintError, solver.constraint, 2 <= x1 <= 0)

    def testExpressionInfeasible(self):
        '''Solutions should be false for infeasibility on expressions'''
        solver = scip.solver()
        x1 = solver.variable()
        x2 = solver.variable()
        solver += x1 + x2  <= 1
        solver += x1 + x2 >= 2
        solution = solver.maximize()
        self.assertFalse(solution)

    def testUnbounded(self):
        '''Solutions should be false when unbounded'''
        solver = scip.solver()
        solver.variable(coefficient=1)
        solution = solver.maximize() 
        self.assertFalse(solution)
        self.assertTrue(solution.unbounded or solution.inforunbd)
        self.assertTrue(solution.objective > 0)
    
    def testRestart(self):
        '''Test solver restart'''
        solver = scip.solver()
        solver.variable(coefficient=1, vartype=scip.INTEGER, upper=2)
        solution = solver.maximize() 
        self.assertAlmostEqual(solution.objective, 2)

        solver.restart()
        solver.variable(coefficient=1, vartype=scip.INTEGER, upper=2)
        solution = solver.maximize() 
        self.assertAlmostEqual(solution.objective, 4)
        
    def testPrimal(self):
        '''Test feeding of primal solutions to the solver'''
        solver = scip.solver()
        v1 = solver.variable(coefficient=1, vartype=scip.INTEGER, upper=2)
        v2 = solver.variable(vartype=scip.BINARY)
        v3 = solver.variable()
        solver.constraint(v1 <= 2)
        
        # Pass known solution to the solver
        solution = solver.maximize(solution={v1:2, v2:1, v3:5.4})
        self.assertAlmostEqual(solution.objective, 2)
    
    def testPrimalErrors(self):
        '''Test feeding of primal with invalid key/value types'''
        solver = scip.solver()
        v = solver.variable(coefficient=1, vartype=scip.INTEGER, upper=2)
        self.assertRaises(scip.SolverError, solver.maximize, {'x':3})
        self.assertRaises(scip.SolverError, solver.maximize, {v:'y'})
    
    def testPrimalInfeasible(self):
        '''Test passing of infeasible solution to solver'''
        solver = scip.solver()
        v = solver.variable(coefficient=1, vartype=scip.INTEGER, upper=2)
        self.assertRaises(scip.SolverError, solver.maximize, {v:3})
    
    def testWrongSolver(self):
        '''Test incorrect mixing of variables and solvers'''
        solver1 = scip.solver()
        solver2 = scip.solver()
        v1 = solver1.variable()
        self.assertRaises(scip.ConstraintError, solver2.constraint, v1 <= 1)
        self.assertRaises(scip.SolverError, solver2.maximize, objective=v1<=3)
        
    def testConstantInMax(self):
        '''Test a constant in maximization, like maximize(objective=x+4)'''
        solver = scip.solver()
        x = solver.variable()
        solver += x <= 1
        solution = solver.maximize(objective=x+4)
        self.assertAlmostEqual(solution.objective, 5)
        
        solution = solver.maximize(objective=x-7)
        self.assertAlmostEqual(solution.objective, -6)
    
    def testConstantInMin(self):
        '''Test a constant in minimization, like minimize(objective=x-4)'''
        solver = scip.solver()
        x = solver.variable(lower=-1)
        solution = solver.minimize(objective=x+4)
        self.assertAlmostEqual(solution.objective, 3)
        
        solution = solver.minimize(objective=x-5)
        self.assertAlmostEqual(solution.objective, -6)
    
    def testConstantObjective(self):
        '''Constant objectives shouldn't raise errors, at least'''
        scip.solver().maximize(objective=3)
        scip.solver().minimize(objective=3)
        
    def testBoundedVariableObjective(self):
        '''Bounds on a single variable should not matter for min/max'''
        solver = scip.solver()
        x = solver.variable()
        y = solver.variable()
        solver += y >= 3
        solver += x >= y
        solution = solver.minimize(objective=x)
        self.assertAlmostEqual(solution.objective, 3)
        
if __name__ == '__main__':
    unittest.main()

