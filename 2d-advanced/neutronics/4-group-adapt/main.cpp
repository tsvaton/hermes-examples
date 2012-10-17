#define HERMES_REPORT_ALL
#define HERMES_REPORT_FILE "application.log"
#include "definitions.h"
#include "problem_data.h"



// This example uses automatic adaptivity to solve a 4-group neutron diffusion equation in the reactor core.
// The eigenproblem is solved using power interations.
//
// The reactor neutronics in a general coordinate system is given by the following eigenproblem:
//
//  - \nabla \cdot D_g \nabla \phi_g + \Sigma_{Rg}\phi_g - \sum_{g' \neq g} \Sigma_s^{g'\to g} \phi_{g'} =
//  = \frac{\chi_g}{k_{eff}} \sum_{g'} \nu_{g'} \Sigma_{fg'}\phi_{g'}
//
// where 1/k_{eff} is eigenvalue and \phi_g, g = 1,...,4 are eigenvectors (neutron fluxes). The current problem
// is posed in a 3D cylindrical axisymmetric geometry, leading to a 2D problem with r-z as the independent spatial
// coordinates. Identifying r = x, z = y, the gradient in the weak form has the same components as in the
// x-y system, while all integrands are multiplied by 2\pi x (determinant of the transformation matrix).
//
// BC:
//
// homogeneous neumann on symmetry axis
// d D_g\phi_g / d n = - 0.5 \phi_g   elsewhere
//
// The eigenproblem is numerically solved using common technique known as the power method (power iterations):
//
//  1) Make an initial estimate of \phi_g and k_{eff}
//  2) For n = 1, 2,...
//         solve for \phi_g using previous k_prev
//         solve for new k_{eff}
//                                \int_{Active Core} \sum^4_{g = 1} \nu_{g} \Sigma_{fg}\phi_{g}_{new}
//               k_new =  k_prev -------------------------------------------------------------------------
//                                \int_{Active Core} \sum^4_{g = 1} \nu_{g} \Sigma_{fg}\phi_{g}_{prev}
//  3) Stop iterations when
//
//     |   k_new - k_prev  |
//     | ----------------- |  < epsilon
//     |       k_new       |
//
//  Author: Milan Hanus (University of West Bohemia, Pilsen, Czech Republic).

// Initial uniform mesh refinement for the individual solution components.
const int INIT_REF_NUM[N_GROUPS] = {     
  1, 1, 1, 1                             
};
// Initial polynomial orders for the individual solution components. 
const int P_INIT[N_GROUPS] = {           
  1, 1, 1, 1                              
};      
// This is a quantitative parameter of the adapt(...) function and
// it has different meanings for various adaptive strategies.
const double THRESHOLD = 0.3;            
// Adaptive strategy:
// STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
//   error is processed. If more elements have similar errors, refine
//   all to keep the mesh symmetric.
// STRATEGY = 1 ... refine all elements whose error is larger
//   than THRESHOLD times maximum element error.
// STRATEGY = 2 ... refine all elements whose error is larger
//   than THRESHOLD.
const int STRATEGY = 1;                  
// Predefined list of element refinement candidates. Possible values are
// H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
// H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
const CandList CAND_LIST = H2D_HP_ANISO; 
// Maximum allowed level of hanging nodes:
// MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
// MESH_REGULARITY = 1 ... at most one-level hanging nodes,
// MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
// Note that regular meshes are not supported, this is due to
// their notoriously bad performance.
const int MESH_REGULARITY = -1;          
// This parameter influences the selection of
// candidates in hp-adaptivity. Default value is 1.0. 
const double CONV_EXP = 1.0;             
// Stopping criterion for adaptivity.
const double ERR_STOP = 0.5;             
// Adaptivity process stops when the number of degrees of freedom grows over
// this limit. This is mainly to prevent h-adaptivity to go on forever.
const int NDOF_STOP = 60000;             
// Adaptivity process stops when the number of adaptation steps grows over
// this limit.
const int MAX_ADAPT_NUM = 30;            
// Matrix solver: SOLVER_AMESOS, SOLVER_AZTECOO, SOLVER_MUMPS,
// SOLVER_PETSC, SOLVER_SUPERLU, SOLVER_UMFPACK.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  
                                                  
// Power iteration control.
// Initial eigenvalue approximation.
double k_eff = 1.0;         
// Tolerance for eigenvalue convergence on the coarse mesh.
double TOL_PIT_CM = 5e-5;   
// Tolerance for eigenvalue convergence on the fine mesh.
double TOL_PIT_RM = 5e-6;   

// Macros for simpler reporting (four group case).
#define report_num_dofs(spaces) spaces[0]->get_num_dofs(), spaces[1]->get_num_dofs(),\
                                spaces[2]->get_num_dofs(), spaces[3]->get_num_dofs(),\
                                spaces[0]->get_num_dofs() + spaces[1]->get_num_dofs() \
                                + spaces[2]->get_num_dofs() + spaces[3]->get_num_dofs()
#define report_errors(errors) errors[0], errors[1], errors[2], errors[3]

int main(int argc, char* argv[])
{
  // Time measurement.
  Hermes::Mixins::TimeMeasurable cpu_time;
  cpu_time.tick();
  
  // Load physical data of the problem.
  MaterialPropertyMaps matprop(N_GROUPS);
  matprop.set_D(D);
  matprop.set_Sigma_r(Sr);
  matprop.set_Sigma_s(Ss);
  matprop.set_Sigma_a(Sa);
  matprop.set_Sigma_f(Sf);
  matprop.set_nu(nu);
  matprop.set_chi(chi);
  matprop.validate();
  
  std::cout << matprop;

  // Use multimesh, i.e. create one mesh for each energy group.
  Hermes::vector<Mesh *> meshes;
  for (unsigned int g = 0; g < matprop.get_G(); g++) 
    meshes.push_back(new Mesh());
  
  // Load the mesh for the 1st group.
  MeshReaderH2D mloader;
  mloader.load(mesh_file.c_str(), meshes[0]);
 
  for (unsigned int g = 1; g < matprop.get_G(); g++) 
  {
    // Obtain meshes for the 2nd to 4th group by cloning the mesh loaded for the 1st group.
    meshes[g]->copy(meshes[0]);
    // Initial uniform refinements.
    for (int i = 0; i < INIT_REF_NUM[g]; i++) 
      meshes[g]->refine_all_elements();
  }
  for (int i = 0; i < INIT_REF_NUM[0]; i++) 
    meshes[0]->refine_all_elements();
  
  // Create pointers to solutions on coarse and fine meshes and from the latest power iteration, respectively.
  Hermes::vector<Solution<double>*> coarse_solutions, fine_solutions;
  Hermes::vector<MeshFunction<double>*> power_iterates;

  // Initialize all the new solution variables.
  for (unsigned int g = 0; g < matprop.get_G(); g++) 
  {
    coarse_solutions.push_back(new Solution<double>());
    fine_solutions.push_back(new Solution<double>());
    power_iterates.push_back(new ConstantSolution<double>(meshes[g], 1.0));   
  }
  
  // Create the approximation spaces with the default shapeset.
  H1Space<double> space1(meshes[0], P_INIT[0]);
  H1Space<double> space2(meshes[1], P_INIT[1]);
  H1Space<double> space3(meshes[2], P_INIT[2]);
  H1Space<double> space4(meshes[3], P_INIT[3]);
  Hermes::vector<const Space<double>*> const_spaces(&space1, &space2, &space3, &space4);
  Hermes::vector<Space<double>*> spaces(&space1, &space2, &space3, &space4);

  // Initialize the weak formulation.
  CustomWeakForm wf(matprop, power_iterates, k_eff, bdy_vacuum);
    
  // Initialize the discrete algebraic representation of the problem and its solver.
  //
  // Create the matrix and right-hand side vector for the solver.
  SparseMatrix<double>* mat = create_matrix<double>();
  Vector<double>* rhs = create_vector<double>();
  // Instantiate the solver itself.
  LinearMatrixSolver<double>* solver = create_linear_solver<double>( mat, rhs);

  // Initialize views.
  /* for 1280x800 display */
  ScalarView view1("Neutron flux 1", new WinGeom(0, 0, 320, 400));
  ScalarView view2("Neutron flux 2", new WinGeom(330, 0, 320, 400));
  ScalarView view3("Neutron flux 3", new WinGeom(660, 0, 320, 400));
  ScalarView view4("Neutron flux 4", new WinGeom(990, 0, 320, 400));
  OrderView oview1("Mesh for group 1", new WinGeom(0, 450, 320, 500));
  OrderView oview2("Mesh for group 2", new WinGeom(330, 450, 320, 500));
  OrderView oview3("Mesh for group 3", new WinGeom(660, 450, 320, 500));
  OrderView oview4("Mesh for group 4", new WinGeom(990, 450, 320, 500));

  /* for adjacent 1280x800 and 1680x1050 displays
  ScalarView view1("Neutron flux 1", new WinGeom(0, 0, 640, 480));
  ScalarView view2("Neutron flux 2", new WinGeom(650, 0, 640, 480));
  ScalarView view3("Neutron flux 3", new WinGeom(1300, 0, 640, 480));
  ScalarView view4("Neutron flux 4", new WinGeom(1950, 0, 640, 480));
  OrderView oview1("Mesh for group 1", new WinGeom(1300, 500, 340, 500));
  OrderView oview2("Mesh for group 2", new WinGeom(1650, 500, 340, 500));
  OrderView oview3("Mesh for group 3", new WinGeom(2000, 500, 340, 500));
  OrderView oview4("Mesh for group 4", new WinGeom(2350, 500, 340, 500));
  */

  Hermes::vector<ScalarView *> sviews(&view1, &view2, &view3, &view4);
  Hermes::vector<OrderView *> oviews(&oview1, &oview2, &oview3, &oview4); 
  for (unsigned int g = 0; g < matprop.get_G(); g++) 
  { 
    sviews[g]->show_mesh(false);
    sviews[g]->set_3d_mode(true);
  }
  
  // DOF and CPU convergence graphs
  GnuplotGraph graph_dof("Error convergence", "NDOF", "log(error)");
  graph_dof.add_row("H1 err. est. [%]", "r", "-", "o");
  graph_dof.add_row("L2 err. est. [%]", "g", "-", "s");
  graph_dof.add_row("Keff err. est. [milli-%]", "b", "-", "d");
  graph_dof.set_log_y();
  graph_dof.show_legend();
  graph_dof.show_grid();

  GnuplotGraph graph_dof_evol("Evolution of NDOF", "Adaptation step", "NDOF");
  graph_dof_evol.add_row("group 1", "r", "-", "o");
  graph_dof_evol.add_row("group 2", "g", "-", "x");
  graph_dof_evol.add_row("group 3", "b", "-", "+");
  graph_dof_evol.add_row("group 4", "m", "-", "*");
  graph_dof_evol.set_log_y();
  graph_dof_evol.set_legend_pos("bottom right");
  graph_dof_evol.show_grid();

  GnuplotGraph graph_cpu("Error convergence", "CPU time [s]", "log(error)");
  graph_cpu.add_row("H1 err. est. [%]", "r", "-", "o");
  graph_cpu.add_row("L2 err. est. [%]", "g", "-", "s");
  graph_cpu.add_row("Keff err. est. [milli-%]", "b", "-", "d");
  graph_cpu.set_log_y();
  graph_cpu.show_legend();
  graph_cpu.show_grid();

  // Initialize the refinement selectors.
  H1ProjBasedSelector<double> selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);
  Hermes::vector<RefinementSelectors::Selector<double>*> selectors;
  for (unsigned int g = 0; g < matprop.get_G(); g++) 
    selectors.push_back(&selector);
  
  Hermes::vector<MatrixFormVol<double>*> projection_jacobian;
  Hermes::vector<VectorFormVol<double>*> projection_residual;
  for (unsigned int g = 0; g < matprop.get_G(); g++) 
  {
    projection_jacobian.push_back(new H1AxisymProjectionJacobian(g));
    projection_residual.push_back(new H1AxisymProjectionResidual(g, power_iterates[g]));
  }
  
  Hermes::vector<ProjNormType> proj_norms_h1, proj_norms_l2;
  for (unsigned int g = 0; g < matprop.get_G(); g++)
  {
    proj_norms_h1.push_back(HERMES_H1_NORM);
    proj_norms_l2.push_back(HERMES_L2_NORM);
  }
  
  // Initial power iteration to obtain a coarse estimate of the eigenvalue and the fission source.
  Hermes::Mixins::Loggable::static_info("Coarse mesh power iteration, %d + %d + %d + %d = %d ndof:", report_num_dofs(spaces));
  power_iteration(matprop, const_spaces, &wf, power_iterates, core, TOL_PIT_CM, matrix_solver);
  
  // Adaptivity loop:
  int as = 1; bool done = false;
  do 
  {
    Hermes::Mixins::Loggable::static_info("---- Adaptivity step %d:", as);
    
    // Construct globally refined meshes and setup reference spaces on them.
    Hermes::vector<const Space<double>*> ref_spaces_const;
    Hermes::vector<Mesh *> ref_meshes;
    for (unsigned int g = 0; g < matprop.get_G(); g++) 
    { 
      ref_meshes.push_back(new Mesh());
      Mesh *ref_mesh = ref_meshes.back();      
      ref_mesh->copy(spaces[g]->get_mesh());
      ref_mesh->refine_all_elements();
      
      int order_increase = 1;
      ref_spaces_const.push_back(spaces[g]->dup(ref_mesh, order_increase));
    }

#ifdef WITH_PETSC    
    // PETSc assembling is currently slow for larger matrices, so we switch to 
    // UMFPACK when matrices of order >8000 start to appear.
    if (Space<double>::get_num_dofs(ref_spaces_const) > 8000 && matrix_solver == SOLVER_PETSC)
    {
      // Delete the old solver.
      delete mat;
      delete rhs;
      delete solver;
      
      // Create a new one.
      matrix_solver = SOLVER_UMFPACK;
      mat = create_matrix<double>();
      rhs = create_vector<double>();
      solver = create_linear_solver<double>( mat, rhs);
    }
#endif    

    // Solve the fine mesh problem.
    Hermes::Mixins::Loggable::static_info("Fine mesh power iteration, %d + %d + %d + %d = %d ndof:", report_num_dofs(ref_spaces_const));
    power_iteration(matprop, ref_spaces_const, &wf, power_iterates, core, TOL_PIT_RM, matrix_solver);
    
    // Store the results.
    for (unsigned int g = 0; g < matprop.get_G(); g++) 
      fine_solutions[g]->copy((static_cast<Solution<double>*>(power_iterates[g])));

    Hermes::Mixins::Loggable::static_info("Projecting fine mesh solutions on coarse meshes.");
    // This is commented out as the appropriate method was deleted in the commit
    // "Cleaning global projections" (b282194946225014faa1de37f20112a5a5d7ab5a).
    //OGProjection<double> ogProjection; ogProjection.project_global(spaces, projection_jacobian, projection_residual, coarse_solutions);

    // Time measurement.
    cpu_time.tick();

    // View the coarse mesh solution and meshes.
    for (unsigned int g = 0; g < matprop.get_G(); g++) 
    { 
      sviews[g]->show(coarse_solutions[g]); 
      oviews[g]->show(spaces[g]);
    }

    // Skip visualization time.
    cpu_time.tick(Hermes::Mixins::TimeMeasurable::HERMES_SKIP);

    // Report the number of negative eigenfunction values.
    Hermes::Mixins::Loggable::static_info("Num. of negative values: %d, %d, %d, %d",
         get_num_of_neg(coarse_solutions[0]), get_num_of_neg(coarse_solutions[1]),
         get_num_of_neg(coarse_solutions[2]), get_num_of_neg(coarse_solutions[3]));

    // Calculate element errors and total error estimate.
    Adapt<double> adapt_h1(spaces);
    Adapt<double> adapt_l2(spaces);    
    for (unsigned int g = 0; g < matprop.get_G(); g++)
    {
      adapt_h1.set_error_form(g, g, new ErrorForm(proj_norms_h1[g]));
      adapt_l2.set_error_form(g, g, new ErrorForm(proj_norms_l2[g]));
    }
    
    // Calculate element errors and error estimates in H1 and L2 norms. Use the H1 estimate to drive adaptivity.
    Hermes::Mixins::Loggable::static_info("Calculating errors.");
    Hermes::vector<double> h1_group_errors, l2_group_errors;
    double h1_err_est = adapt_h1.calc_err_est(coarse_solutions, fine_solutions, &h1_group_errors) * 100;
    double l2_err_est = adapt_l2.calc_err_est(coarse_solutions, fine_solutions, &l2_group_errors, false) * 100;

    // Time measurement.
    cpu_time.tick();
    double cta = cpu_time.accumulated();
    
    // Report results.
    Hermes::Mixins::Loggable::static_info("ndof_coarse: %d + %d + %d + %d = %d", report_num_dofs(spaces));

    // Millipercent eigenvalue error w.r.t. the reference value (see physical_parameters.cpp). 
    double keff_err = 1e5*fabs(wf.get_keff() - REF_K_EFF)/REF_K_EFF;

    Hermes::Mixins::Loggable::static_info("per-group err_est_coarse (H1): %g%%, %g%%, %g%%, %g%%", report_errors(h1_group_errors));
    Hermes::Mixins::Loggable::static_info("per-group err_est_coarse (L2): %g%%, %g%%, %g%%, %g%%", report_errors(l2_group_errors));
    Hermes::Mixins::Loggable::static_info("total err_est_coarse (H1): %g%%", h1_err_est);
    Hermes::Mixins::Loggable::static_info("total err_est_coarse (L2): %g%%", l2_err_est);
    Hermes::Mixins::Loggable::static_info("k_eff err: %g milli-percent", keff_err);

    // Add entry to DOF convergence graph.
    int ndof_coarse = spaces[0]->get_num_dofs() + spaces[1]->get_num_dofs() 
      + spaces[2]->get_num_dofs() + spaces[3]->get_num_dofs();
    graph_dof.add_values(0, ndof_coarse, h1_err_est);
    graph_dof.add_values(1, ndof_coarse, l2_err_est);
    graph_dof.add_values(2, ndof_coarse, keff_err);

    // Add entry to CPU convergence graph.
    graph_cpu.add_values(0, cta, h1_err_est);
    graph_cpu.add_values(1, cta, l2_err_est);
    graph_cpu.add_values(2, cta, keff_err);

    for (unsigned int g = 0; g < matprop.get_G(); g++)
      graph_dof_evol.add_values(g, as, Space<double>::get_num_dofs(spaces[g]));

    cpu_time.tick(Hermes::Mixins::TimeMeasurable::HERMES_SKIP);

    // If err_est too large, adapt the mesh (L2 norm chosen since (weighted integrals of) solution values
    // are more important for further analyses than the derivatives. 
    if (l2_err_est < ERR_STOP) 
      done = true;
    else 
    {
      Hermes::Mixins::Loggable::static_info("Adapting the coarse mesh.");
      done = adapt_h1.adapt(selectors, THRESHOLD, STRATEGY, MESH_REGULARITY);
      if (spaces[0]->get_num_dofs() + spaces[1]->get_num_dofs() 
          + spaces[2]->get_num_dofs() + spaces[3]->get_num_dofs() >= NDOF_STOP) 
        done = true;
    }

    // Free reference meshes and spaces.
    for (unsigned int g = 0; g < matprop.get_G(); g++) 
    {
      delete ref_spaces_const[g];
      delete ref_meshes[g];
    }

    as++;
        
    if (as >= MAX_ADAPT_NUM) done = true;
  }
  while(done == false);

  Hermes::Mixins::Loggable::static_info("Total running time: %g s", cpu_time.accumulated());
  
  for (unsigned int g = 0; g < matprop.get_G(); g++) 
  {
    delete spaces[g]; delete meshes[g];
    delete coarse_solutions[g], delete fine_solutions[g]; delete power_iterates[g];
  }
  
  delete mat;
  delete rhs;
  delete solver;

  graph_dof.save("conv_dof.gp");
  graph_cpu.save("conv_cpu.gp");
  graph_dof_evol.save("dof_evol.gp");

  // Wait for all views to be closed.
  View::wait();
  return 0;
}
