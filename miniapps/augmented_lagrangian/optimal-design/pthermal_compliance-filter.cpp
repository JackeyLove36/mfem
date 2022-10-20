//
// Compile with: make optimal_design
//
// Sample runs:
// mpirun -np 6 ./pthermal_compliance-filter -epsilon 0.01 -alpha 0.1 -beta 5.0 -r 4 -o 2
//
//         min J(K) = <g,u>
//                            
//                        Γ_1           Γ_2            Γ_1
//               _ _ _ _ _ _ _ _ _ _ _________ _ _ _ _ _ _ _ _ _ _   
//              |         |         |         |         |         |  
//              |         |         |         |         |         |  
//              |---------|---------|---------|---------|---------|  
//              |         |         |         |         |         |  
//              |         |         |         |         |         |  
//      Γ_1-->  |---------|---------|---------|---------|---------|  <-- Γ_1
//              |         |         |         |         |         |  
//              |         |         |         |         |         |  
//              |---------|---------|---------|---------|---------|  
//              |         |         |         |         |         |  
//              |         |         |         |         |         |  
//               -------------------------------------------------|
//                       |̂                              |̂  
//                      Γ_1                            Γ_1                    
//
//
//         subject to   - div( K\nabla u ) = f    in \Omega
//                                       u = 0    on Γ_2
//                               (K ∇ u)⋅n = 0    on Γ_1
//         and                   ∫_Ω K dx <= V ⋅ vol(\Omega)
//         and                  a <= K(x) <= b

#include "mfem.hpp"
#include <memory>
#include <iostream>
#include <fstream>
#include <random>
#include "common/fpde.hpp"

class DiffusionCoefficient : public Coefficient
{
protected:
   GridFunction *rho_filter; // grid function
   double min_val;
   double max_val;
   double exponent;

public:
   DiffusionCoefficient(GridFunction &rho_filter_, double min_val_= 1e-3, double max_val_=1.0, 
      double exponent_ = 3)
      : rho_filter(&rho_filter_), min_val(min_val_), max_val(max_val_),exponent(exponent_) { }

   virtual double Eval(ElementTransformation &T, const IntegrationPoint &ip)
   {
      double val = rho_filter->GetValue(T, ip);
      double coeff = min_val + pow(val,exponent)*(max_val-min_val);
      return coeff;
   }
};

class GradientRHSCoefficient : public Coefficient
{
protected:
   GridFunction *u; // grid function
   GridFunction *rho_filter; // grid function
   double min_val;
   double max_val;
   double exponent;

public:
   GradientRHSCoefficient(GridFunction &u_, GridFunction & rho_filter_, 
      double min_val_= 1e-3, double max_val_=1.0, double exponent_ = 3.0)
      : u(&u_), rho_filter(&rho_filter_), min_val(min_val_), max_val(max_val_), 
         exponent(exponent_) { }

   virtual double Eval(ElementTransformation &T, const IntegrationPoint &ip)
   {
      T.SetIntPoint(&ip);
      double val = rho_filter->GetValue(T,ip);
      Vector gradu;
      u->GetGradient(T,gradu);
      return -exponent * pow(val, exponent-1.0) * (max_val-min_val) * (gradu * gradu); 
   }
};


using namespace std;
using namespace mfem;

// Let H^1_Γ_1 := {v ∈ H^1(Ω) | v|Γ_1 = 0}
/** The Lagrangian for this problem is
 *    TODO
 * -------------------------------------------------------- 
 * 
 * We update ρ with projected gradient descent via
 * 
 *  1. Initialize λ, ρ 
 *  while not converged
 *     2. Solve (ϵ^2 ∇ ρ̃, ∇ v ) + (ρ̃,v) = (ρ,v)  
 *     3. Solve (k(ρ̃) ∇ u , ∇ v) = (f,v) , k(ρ̃):= K_min + ρ̃^3 (K_max- Kmin)  
 *     4. Solve (ϵ^2 ∇ w̃ , ∇ v ) + (w̃ ,v) = (-k'(ρ̃) |∇ u|^2 ,v)
 *     5. Compute gradient in L2 w:= M^-1 w̃ 
 *     6. update until convergence 
 *       ρ <--- P(ρ - α (w - λ + β (∫_Ω ρ - V ⋅ vol(Ω)) ) )     
 *              P is the projection operator enforcing 0 <= ρ <= 1
 * 
 *  7. update λ 
 *     λ <- λ - β (∫_Ω K dx - V ⋅ vol(Ω))
 * 
 *  ρ ∈ L^2 (order p - 1)
 *  ρ̃ ∈ H^1 (order p - 1)
 *  u ∈ H^1 (order p)
 *  w̃ ∈ H^1 (order p - 1)
 *  w ∈ L^2 (order p - 1)
 */

int main(int argc, char *argv[])
{
   Mpi::Init();
   int num_procs = Mpi::WorldSize();
   int myid = Mpi::WorldRank();
   Hypre::Init();   
   // 1. Parse command-line options.
   int ref_levels = 2;
   int order = 2;
   bool visualization = true;
   double alpha = 1.0;
   double beta = 1.0;
   double epsilon = 1.0;
   double mass_fraction = 0.4;
   int max_it = 1e2;
   double tol_rho = 5e-2;
   double tol_lambda = 1e-3;
   double K_max = 1.0;
   double K_min = 1e-3;

   OptionsParser args(argc, argv);
   args.AddOption(&ref_levels, "-r", "--refine",
                  "Number of times to refine the mesh uniformly.");
   args.AddOption(&order, "-o", "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&alpha, "-alpha", "--alpha-step-length",
                  "Step length for gradient descent.");
   args.AddOption(&beta, "-beta", "--beta-step-length",
                  "Step length for λ"); 
   args.AddOption(&epsilon, "-epsilon", "--epsilon-thickness",
                  "epsilon phase field thickness");
   args.AddOption(&max_it, "-mi", "--max-it",
                  "Maximum number of gradient descent iterations.");
   args.AddOption(&tol_rho, "-tr", "--tol_rho",
                  "Exit tolerance for ρ ");     
   args.AddOption(&tol_lambda, "-tl", "--tol_lambda",
                  "Exit tolerance for λ");                                 
   args.AddOption(&mass_fraction, "-mf", "--mass-fraction",
                  "Mass fraction for diffusion coefficient.");
   args.AddOption(&K_max, "-Kmax", "--K-max",
                  "Maximum of diffusion diffusion coefficient.");
   args.AddOption(&K_min, "-Kmin", "--K-min",
                  "Minimum of diffusion diffusion coefficient.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");

   args.Parse();
   if (!args.Good())
   {
      if (myid == 0)
      {
         args.PrintUsage(cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (myid == 0)
   {
      args.PrintOptions(cout);
   }

   Mesh mesh = Mesh::MakeCartesian2D(7,7,mfem::Element::Type::QUADRILATERAL,true,1.0,1.0);

   int dim = mesh.Dimension();

   for (int i = 0; i<mesh.GetNBE(); i++)
   {
      Element * be = mesh.GetBdrElement(i);
      Array<int> vertices;
      be->GetVertices(vertices);

      double * coords1 = mesh.GetVertex(vertices[0]);
      double * coords2 = mesh.GetVertex(vertices[1]);

      Vector center(2);
      center(0) = 0.5*(coords1[0] + coords2[0]);
      center(1) = 0.5*(coords1[1] + coords2[1]);


      if (abs(center(1) - 1.0) < 1e-10 && abs(center(0)-0.5) < 1e-10)
      {
         // the top edge
         be->SetAttribute(2);
      }
      else
      {
         // all other boundaries
         be->SetAttribute(1);
      }
   }
   mesh.SetAttributes();

   for (int lev = 0; lev < ref_levels; lev++)
   {
      mesh.UniformRefinement();
   }

   ParMesh pmesh(MPI_COMM_WORLD, mesh);
   mesh.Clear();

   // 5. Define the vector finite element spaces representing the state variable u,
   //    adjoint variable p, and the control variable f.
   H1_FECollection state_fec(order, dim,BasisType::Positive); // space for u
   H1_FECollection filter_fec(order-1, dim,BasisType::Positive); // space for ρ̃  
   L2_FECollection control_fec(order-1, dim,BasisType::Positive); // space for ρ   
   ParFiniteElementSpace state_fes(&pmesh, &state_fec);
   ParFiniteElementSpace filter_fes(&pmesh, &filter_fec);
   ParFiniteElementSpace control_fes(&pmesh, &control_fec);
   
   HYPRE_BigInt state_size = state_fes.GlobalTrueVSize();
   HYPRE_BigInt control_size = control_fes.GlobalTrueVSize();
   HYPRE_BigInt filter_size = filter_fes.GlobalTrueVSize();
   if (myid==0)
   {
      cout << "Number of state unknowns: " << state_size << endl;
      cout << "Number of filter unknowns: " << filter_size << endl;
      cout << "Number of control unknowns: " << control_size << endl;
   }

   // 7. Set the initial guess for f and the boundary conditions for u.
   ParGridFunction u(&state_fes);
   ParGridFunction rho(&control_fes);
   ParGridFunction rho_old(&control_fes);
   ParGridFunction rho_filter(&filter_fes);
   u = 0.0;
   rho_filter = 0.0;
   rho = 0.5;
   rho_old = 0.5;
   // 8. Set up the linear form b(.) for the state and adjoint equations.
   int maxat = pmesh.bdr_attributes.Max();
   Array<int> ess_bdr(maxat);
   ess_bdr = 0;
   if (maxat > 0)
   {
      ess_bdr[maxat-1] = 1;
   }
   ConstantCoefficient one(1.0);
   FPDESolver * PoissonSolver = new FPDESolver();
   PoissonSolver->SetMesh(&pmesh);
   PoissonSolver->SetOrder(state_fec.GetOrder());
   PoissonSolver->SetAlpha(1.0);
   PoissonSolver->SetBeta(0.0);
   PoissonSolver->SetupFEM();
   // RandomFunctionCoefficient load_coeff(randomload);
   PoissonSolver->SetRHSCoefficient(&one);
   PoissonSolver->SetEssentialBoundary(ess_bdr);
   PoissonSolver->Init();

   ConstantCoefficient eps2_cf(epsilon*epsilon);
   FPDESolver * FilterSolver = new FPDESolver();
   FilterSolver->SetMesh(&pmesh);
   FilterSolver->SetOrder(filter_fec.GetOrder());
   FilterSolver->SetAlpha(1.0);
   FilterSolver->SetBeta(1.0);
   FilterSolver->SetDiffusionCoefficient(&eps2_cf);
   Array<int> ess_bdr_filter;
   if (pmesh.bdr_attributes.Size())
   {
      ess_bdr_filter.SetSize(pmesh.bdr_attributes.Max());
      ess_bdr_filter = 0;
   }
   FilterSolver->SetEssentialBoundary(ess_bdr_filter);
   FilterSolver->Init();
   FilterSolver->SetupFEM();

   ParBilinearForm mass(&control_fes);
   mass.AddDomainIntegrator(new InverseIntegrator(new MassIntegrator(one)));
   mass.Assemble();

   HypreParMatrix M;
   Array<int> empty;
   mass.FormSystemMatrix(empty,M);


   // 9. Define the gradient function
   ParGridFunction w(&control_fes);
   ParGridFunction w_filter(&filter_fes);

   // 10. Define some tools for later
   ConstantCoefficient zero(0.0);
   ParGridFunction onegf(&control_fes);
   onegf = 1.0;
   ParLinearForm vol_form(&control_fes);
   vol_form.AddDomainIntegrator(new DomainLFIntegrator(one));
   vol_form.Assemble(false);
   double domain_volume = vol_form(onegf);

   // 11. Connect to GLVis. Prepare for VisIt output.
   char vishost[] = "localhost";
   int  visport   = 19916;
   socketstream sout_u,sout_K,sout_rho, sout_rho_filter;
   if (visualization)
   {
      sout_u.open(vishost, visport);
      sout_rho.open(vishost, visport);
      sout_K.open(vishost, visport);
      sout_rho_filter.open(vishost, visport);
      sout_u.precision(8);
      sout_rho.precision(8);
      sout_K.precision(8);
      sout_rho_filter.precision(8);
   }

   mfem::ParaViewDataCollection paraview_dc("Thermal_compliance", &pmesh);
   paraview_dc.SetPrefixPath("ParaView");
   paraview_dc.SetLevelsOfDetail(order);
   paraview_dc.SetCycle(0);
   paraview_dc.SetDataFormat(VTKFormat::BINARY);
   paraview_dc.SetHighOrderOutput(true);
   paraview_dc.SetTime(0.0); // set the time
   paraview_dc.RegisterField("soln",&u);
   paraview_dc.RegisterField("dens",&rho);

   // 12. AL iterations
   int step = 0;
   double lambda = 0.0;
   for (int k = 1; k < max_it; k++)
   {
      // A. Form state equation
      for (int l = 1; l < max_it; l++)
      {
         step++;
         if (myid == 0)
         {
            cout << "\nStep = " << l << endl;
         }
         // Step 2 -  Filter Solve
         // Solve (ϵ^2 ∇ ρ̃, ∇ v ) + (ρ̃,v) = (ρ,v)  
         GridFunctionCoefficient rho_cf(&rho);
         FilterSolver->SetRHSCoefficient(&rho_cf);
         FilterSolver->Solve();
         rho_filter = *FilterSolver->GetFEMSolution();
         // ------------------------------------------------------------------
         // Step 3 - State Solve
         DiffusionCoefficient K(rho_filter,K_min, K_max);
         ParGridFunction k_cf(&control_fes);
         k_cf.ProjectCoefficient(K);
         sout_K << "parallel " << num_procs << " " << myid << "\n";
         sout_K << "solution\n" << pmesh << k_cf
                << "window_title 'Control K'" << flush;         

         PoissonSolver->SetDiffusionCoefficient(&K);
         PoissonSolver->Solve();
         u = *PoissonSolver->GetFEMSolution();
         // ------------------------------------------------------------------
         // Step 4 - Adjoint Solve
         GradientRHSCoefficient rhs_cf(u,rho_filter,K_min, K_max);
         FilterSolver->SetRHSCoefficient(&rhs_cf);
         FilterSolver->Solve();
         w_filter = *FilterSolver->GetFEMSolution();
         // Step 5 - get grad of w
         GridFunctionCoefficient w_cf(&w_filter);
         ParLinearForm w_rhs(&control_fes);
         w_rhs.AddDomainIntegrator(new DomainLFIntegrator(w_cf));
         w_rhs.Assemble();
         M.Mult(w_rhs,w);
         // w.ProjectCoefficient(w_cf); // This might need to change to L2-projection
         // ------------------------------------------------------------------

         if (myid == 0)
         {
            cout << "norm of u = " << u.Norml2() << endl;
         }

         // step 6-update  ρ 
         w -= lambda;
         double mf = vol_form(rho)/domain_volume;
         w += beta * (mf - mass_fraction)/domain_volume;
         rho.Add(-alpha, w);
         // project
         for (int i = 0; i < rho.Size(); i++)
         {
            if (rho[i] > 1.0) 
            {
               rho[i] = 1.0;
            }
            else if (rho[i] < 0.0)
            {
               rho[i] = 0.0;
            }
            else
            { // do nothing
            }
         }

         GridFunctionCoefficient tmp(&rho_old);
         double norm_rho = rho.ComputeL2Error(tmp)/alpha;
         rho_old = rho;
         double compliance = (*(PoissonSolver->GetLinearForm()))(u);
         if (myid == 0)
         {
            mfem::out << "norm of reduced gradient = " << norm_rho << endl;
            mfem::out << "compliance = " << compliance << endl;
         }
         if (norm_rho < tol_rho)
         {
            break;
         }

         if (visualization)
         {

            sout_u << "parallel " << num_procs << " " << myid << "\n";
            sout_u << "solution\n" << pmesh << u
                  << "window_title 'State u'" << flush;

            sout_rho << "parallel " << num_procs << " " << myid << "\n";
            sout_rho << "solution\n" << pmesh << rho
                  << "window_title 'Control ρ '" << flush;

            sout_rho_filter << "parallel " << num_procs << " " << myid << "\n";
            sout_rho_filter << "solution\n" << pmesh << rho_filter
                  << "window_title 'Control ρ filter '" << flush;                  

            paraview_dc.SetCycle(step);
            paraview_dc.SetTime((double)k);
            paraview_dc.Save();
         }

      }
      // λ <- λ - β (∫_Ω K dx - V⋅ vol(\Omega))
      double mass = vol_form(rho);
      if (myid == 0)
      {
         mfem::out << "mass_fraction = " << mass / domain_volume << endl;
      }

      double lambda_inc = mass/domain_volume - mass_fraction;

      lambda -= beta*lambda_inc;
      if (myid == 0)
      {
         mfem::out << "lambda_inc = " << lambda_inc << endl;
         mfem::out << "lambda = " << lambda << endl;
      }


      if (visualization)
      {

         sout_u << "parallel " << num_procs << " " << myid << "\n";
         sout_u << "solution\n" << pmesh << u
               << "window_title 'State u'" << flush;

         sout_rho << "parallel " << num_procs << " " << myid << "\n";
         sout_rho << "solution\n" << pmesh << rho
                << "window_title 'Control ρ '" << flush;
     

         paraview_dc.SetCycle(step);
         paraview_dc.SetTime((double)k);
         paraview_dc.Save();
      }

      if (abs(lambda_inc) < tol_lambda)
      {
         break;
      }
   }

   return 0;
}