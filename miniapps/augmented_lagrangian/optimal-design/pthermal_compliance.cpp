//
// Compile with: make optimal_design
//
// Sample runs:
// mpirun -np 6 ./pthermal_compliance -gamma 0.001 -epsilon 0.0005 -alpha 0.005 -beta 5.0 -r 4 -o 2 -tl 0.000001 -bs 1
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
#include "../entropy/H1_box_projection.hpp"

using namespace std;
using namespace mfem;

// Let H^1_Γ_1 := {v ∈ H^1(Ω) | v|Γ_1 = 0}
/** The Lagrangian for this problem is
 *    
 *    L(u,K,p,λ) = <g,u> - (K ∇u, ∇p) + <g,v>
 *                + γϵ/2 (∇K, ∇K)
 *                + γ/(2ϵ) ∫_Ω K(1-K) dx
 *                - λ (∫_Ω K dx - V ⋅ vol(Ω))
 *                + β/2 (∫_Ω K dx - V ⋅ vol(Ω))^2    
 *      u, p \in H^1_Γ_1
 *      K \in H^1(Ω)
 * 
 *  Note that
 * 
 *    ∂_p L = 0        (1)
 *  
 *  delivers the state equation
 *    
 *    (K ∇u, ∇ v) = <g,v> for all v in 
 * 
 *  and
 *  
 *    ∂_u L = 0        (2)
 * 
 *  delivers the adjoint equation (same as the state eqn)
 * 
 *    (∇ p, ∇ v) = <g,v>  for all v H^1_Γ_1
 *    
 *  and at the solutions u=p of (1) and (2), respectively,
 * 
 *  D_K J = D_K L = ∂_u L ∂_K u + ∂_p L ∂_K p
 *                + ∂_K L
 *                = ∂_K L
 *                = (-|∇ u|^2 - λ + β(∫_Ω K dx - V ⋅ vol(Ω)), ⋅)
 *                + γϵ(∇ K,∇⋅) + γ/ϵ(1/2-K,⋅)
 * 
 * We update the control K_k with projected gradient descent via
 * 
 *  1. Initialize λ 
 *  2. update until convergence 
 *     K <- P (K - α( γ/ϵ(1/2+K) - λ + β(∫_Ω K dx - V ⋅ vol(Ω)) - R^{-1}( |∇ u|^2 + 2K ) )
 *  3. update λ 
 *     λ <- λ - β (∫_Ω K dx - V ⋅ vol(Ω))
 * 
 * P is the projection operator enforcing a <= K(x) <= b, and α  is a specified
 * step length.
 * 
 */

class RandomFunctionCoefficient : public Coefficient
{
private:
   double a = 0.2;
   double b = 0.8;
   double x,y;
   std::default_random_engine generator;
   std::uniform_real_distribution<double> * distribution;
   double (*Function)(const Vector &, double, double);
public:
   RandomFunctionCoefficient(double (*F)(const Vector &, double, double)) 
   : Function(F) 
   {
      distribution = new std::uniform_real_distribution<double> (a,b);
      x = (*distribution)(generator);
      y = (*distribution)(generator);
   }
   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip)
   {
      Vector transip(3);
      T.Transform(ip, transip);
      return ((*Function)(transip, x,y));
   }
   void resample()
   {
      x = (*distribution)(generator);
      y = (*distribution)(generator);
   }
};

double randomload(const Vector & X, double x0, double y0)
{
   // double x = X(0);
   // double y = X(1);
   // double sigma = 0.1;
   // double sigma2 = sigma*sigma;
   // double alpha = 1.0/(2.0*M_PI*sigma2);
   // double r2 = (x-x0)*(x-x0) + (y-y0)*(y-y0);
   // double beta = -0.5/sigma2 * r2;
   // return alpha * exp(beta);
   return 1.0;
}

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
   double gamma = 1.0;
   double epsilon = 1.0;
   double theta = 0.5;
   double mass_fraction = 0.3;
   int max_it = 1e2;
   double tol_K = 1e-2;
   double tol_lambda = 1e-2;
   double K_max = 1.0;
   double K_min = 1e-3;
   int prob = 0;
   int batch_size_min = 2;
   bool BoxH1proj = false;

   OptionsParser args(argc, argv);
   args.AddOption(&ref_levels, "-r", "--refine",
                  "Number of times to refine the mesh uniformly.");
   args.AddOption(&order, "-o", "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&alpha, "-alpha", "--alpha-step-length",
                  "Step length for gradient descent.");
   args.AddOption(&beta, "-beta", "--beta-step-length",
                  "Step length for λ"); 
   args.AddOption(&gamma, "-gamma", "--gamma-penalty",
                  "gamma penalty weight");
   args.AddOption(&epsilon, "-epsilon", "--epsilon-thickness",
                  "epsilon phase field thickness");
   args.AddOption(&theta, "-theta", "--theta-sampling-ratio",
                  "Sampling ratio theta");                  
   args.AddOption(&max_it, "-mi", "--max-it",
                  "Maximum number of gradient descent iterations.");
   args.AddOption(&tol_K, "-tk", "--tol_K",
                  "Exit tolerance for K");     
   args.AddOption(&batch_size_min, "-bs", "--batch-size",
                  "batch size for stochastic gradient descent.");                             
   args.AddOption(&tol_lambda, "-tl", "--tol_lambda",
                  "Exit tolerance for λ");                                 
   args.AddOption(&mass_fraction, "-mf", "--mass-fraction",
                  "Mass fraction for diffusion coefficient.");
   args.AddOption(&K_max, "-Kmax", "--K-max",
                  "Maximum of diffusion diffusion coefficient.");
   args.AddOption(&K_min, "-Kmin", "--K-min",
                  "Minimum of diffusion diffusion coefficient.");
   args.AddOption(&BoxH1proj, "-boxH1projection", "--boxH1projection", "-no-boxH1projection",
                  "--no-boxH1projection",
                  "Enable or disable Box H1 Projection.");                  
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&prob, "-p", "--problem",
                  "Optimization problem: 0 - Compliance Minimization, 1 - Mass Minimization.");

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
   int batch_size = batch_size_min;

   ostringstream file_name;
   file_name << "conv_order" << order << "_GD" << ".csv";
   ofstream conv(file_name.str().c_str());
   if (myid == 0)
   {
      conv << "Step,    Sample Size,    Compliance,    Mass Fraction" << endl;
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
   H1_FECollection state_fec(order, dim);
   H1_FECollection control_fec(order-1, dim);
   // H1_FECollection control_fec(order-1, dim, BasisType::Positive);
   ParFiniteElementSpace state_fes(&pmesh, &state_fec);
   ParFiniteElementSpace control_fes(&pmesh, &control_fec);
   
   HYPRE_BigInt state_size = state_fes.GlobalTrueVSize();
   HYPRE_BigInt control_size = control_fes.GlobalTrueVSize();
   if (myid==0)
   {
      cout << "Number of state unknowns: " << state_size << endl;
      cout << "Number of control unknowns: " << control_size << endl;
   }

   // 7. Set the initial guess for f and the boundary conditions for u.
   ParGridFunction u(&state_fes);
   ParGridFunction K(&control_fes);
   ParGridFunction K_old(&control_fes);
   u = 0.0;
   K = (K_min + K_max)*0.5;
   K_old = 0.0;

   // 8. Set up the linear form b(.) for the state and adjoint equations.
   int maxat = pmesh.bdr_attributes.Max();
   Array<int> ess_bdr(maxat);
   ess_bdr = 0;
   if (maxat > 0)
   {
      ess_bdr[maxat-1] = 1;
   }
   FPDESolver * PoissonSolver = new FPDESolver();
   PoissonSolver->SetMesh(&pmesh);
   PoissonSolver->SetOrder(order);
   PoissonSolver->SetAlpha(1.0);
   PoissonSolver->SetBeta(0.0);
   PoissonSolver->SetupFEM();
   RandomFunctionCoefficient load_coeff(randomload);
   PoissonSolver->SetRHSCoefficient(&load_coeff);
   PoissonSolver->SetEssentialBoundary(ess_bdr);
   PoissonSolver->Init();

   ConstantCoefficient eps2_cf(epsilon*epsilon);
   FPDESolver * H1Projection = new FPDESolver();
   H1Projection->SetMesh(&pmesh);
   H1Projection->SetOrder(order-1);
   H1Projection->SetAlpha(1.0);
   H1Projection->SetBeta(1.0);
   H1Projection->SetDiffusionCoefficient(&eps2_cf);
   Array<int> ess_bdr_K(pmesh.bdr_attributes.Max()); ess_bdr_K = 0;
   H1Projection->SetEssentialBoundary(ess_bdr_K);
   H1Projection->Init();
   H1Projection->SetupFEM();

   // b.AddDomainIntegrator(new DomainLFIntegrator(f));
   OperatorPtr A;
   Vector B, C, X;

   // 9. Define the gradient function
   ParGridFunction grad(&control_fes);
   ParGridFunction avg_grad(&control_fes);

   // 10. Define some tools for later
   ConstantCoefficient zero(0.0);
   ConstantCoefficient one(1.0);
   ParGridFunction onegf(&control_fes);
   onegf = 1.0;
   ParLinearForm vol_form(&control_fes);
   vol_form.AddDomainIntegrator(new DomainLFIntegrator(one));
   vol_form.Assemble(false);
   double domain_volume = vol_form(onegf);

   // 11. Connect to GLVis. Prepare for VisIt output.
   char vishost[] = "localhost";
   int  visport   = 19916;
   socketstream sout_u,sout_p,sout_K;
   if (visualization)
   {
      sout_u.open(vishost, visport);
      sout_K.open(vishost, visport);
      sout_u.precision(8);
      sout_K.precision(8);
   }

   mfem::ParaViewDataCollection paraview_dc("Thermal_compliance", &pmesh);
   paraview_dc.SetPrefixPath("ParaView");
   paraview_dc.SetLevelsOfDetail(order);
   paraview_dc.SetCycle(0);
   paraview_dc.SetDataFormat(VTKFormat::BINARY);
   paraview_dc.SetHighOrderOutput(true);
   paraview_dc.SetTime(0.0); // set the time
   paraview_dc.RegisterField("soln",&u);
   paraview_dc.RegisterField("dens",&K);
   paraview_dc.Save();

   // Project initial K onto constraint set.
   // GridFunctionCoefficient kgf(&K);
   // GradientGridFunctionCoefficient grad_kgf(&K);
   BoxProjection * proj = nullptr;
   if (BoxH1proj)
   {
      proj = new BoxProjection(&K,true);
      // proj = new BoxProjection(&pmesh,order,&kgf, &grad_kgf,true);
      proj->SetBoxBounds(K_min, K_max);
      proj->Solve();
      proj->SetPrintLevel(-1);
      ExpitGridFunctionCoefficient expit_p(proj->Getp());
      expit_p.SetBounds(K_min,K_max);
      K.ProjectCoefficient(expit_p);
      
      delete proj;
   }
   else
   {
      for (int i = 0; i < K.Size(); i++)
      {
         if (K[i] > K_max) 
         {
             K[i] = K_max;
         }
         else if (K[i] < K_min)
         {
             K[i] = K_min;
         }
         else
         { // do nothing
         }
      }
   }


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
            cout << "Step = " << l << endl;
            cout << "batch_size = " << batch_size << endl;
         }
         avg_grad = 0.0;
         double avg_grad_norm = 0.;
         double avg_compliance = 0.;

         GridFunctionCoefficient diffusion_coeff(&K);
         double mf = vol_form(K)/domain_volume;
         for (int ib = 0; ib<batch_size; ib++)
         {
            PoissonSolver->SetDiffusionCoefficient(&diffusion_coeff);
            load_coeff.resample();
            PoissonSolver->Solve();
            u = *PoissonSolver->GetFEMSolution();
            if (myid == 0)
            {
               cout << "norm of u = " << u.Norml2() << endl;
            }

            // H. Constuct gradient function
            // i.e., ∇ J = γ/ϵ (1/2 + K) - λ + β(∫_Ω K dx - V ⋅ vol(Ω)) - R^{-1}(|∇u|^2 + 2γ/ϵ K)
            GradientGridFunctionCoefficient grad_u(&u);
            InnerProductCoefficient norm2_grad_u(grad_u,grad_u);
            SumCoefficient grad_cf(norm2_grad_u,diffusion_coeff,-1.0,-2.0*gamma/epsilon);
            H1Projection->SetRHSCoefficient(&grad_cf);
            H1Projection->Solve();
            
            grad = K;
            grad += (K_max-K_min)/2.0;
            grad *= gamma/epsilon;
            grad += *H1Projection->GetFEMSolution();

            // - λ + β(∫_Ω K dx - V ⋅ vol(\Omega)))
            grad -= lambda;
            grad += beta * (mf - mass_fraction)/domain_volume;

            avg_grad += grad;
            double grad_norm = grad.ComputeL2Error(zero);
            avg_grad_norm += grad_norm*grad_norm;
            avg_compliance += (*(PoissonSolver->GetLinearForm()))(u);

         } // enf of loop through batch samples
         avg_grad_norm /= (double)batch_size;  
         avg_grad /= (double)batch_size;
         avg_compliance /= (double)batch_size;  

         double norm_avg_grad = pow(avg_grad.ComputeL2Error(zero),2);
         double denom = batch_size == 1 ? batch_size : batch_size-1;
         double variance = (avg_grad_norm - norm_avg_grad)/denom;

         avg_grad *= alpha;
         K -= avg_grad;

         // K. Project onto constraint set.
         // GridFunctionCoefficient kgf1(&K);
         // GradientGridFunctionCoefficient grad_kgf1(&K);
         BoxProjection * proj1 = nullptr;
         if (BoxH1proj)
         {
            proj1 = new BoxProjection(&K,true);
            // proj1 = new BoxProjection(&pmesh,order,&kgf, &grad_kgf,true);
            proj1->SetNewtonStepSize(0.1);
            proj1->SetBregmanStepSize(0.1/epsilon);
            // proj1->SetBregmanStepSize(0.001/epsilon);
            proj1->SetMaxInnerIterations(4);
            proj1->SetMaxOuterIterations(10);
            proj1->SetInnerIterationTol(1e-6);
            proj1->SetOuterIterationTol(1e-4);
            proj1->SetNormWeight(0.0);
            proj1->SetDiffusionConstant(epsilon*epsilon);
            proj1->SetPrintLevel(-1);
            proj1->SetBoxBounds(K_min, K_max);
            proj1->Solve();
            ExpitGridFunctionCoefficient expit_p(proj1->Getp());
            expit_p.SetBounds(K_min,K_max);
            K.ProjectCoefficient(expit_p);
            delete proj1;
         }
         else
         {
            for (int i = 0; i < K.Size(); i++)
            {
               if (K[i] > K_max) 
               {
                  K[i] = K_max;
               }
               else if (K[i] < K_min)
               {
                  K[i] = K_min;
               }
               else
               { // do nothing
               }
            }
         }

         GridFunctionCoefficient tmp(&K_old);
         double norm_K = K.ComputeL2Error(tmp)/alpha;
         K_old = K;
         if (myid == 0)
         {
            mfem::out << "norm of reduced gradient = " << norm_K << endl;
            mfem::out << "avg_compliance = " << avg_compliance << endl;
            mfem::out << "variance = " << variance << std::endl;
         }
         if (norm_K < tol_K)
         {
            break;
         }


         double ratio = sqrt(abs(variance)) / norm_K ;
         if (myid == 0)
         {
            mfem::out << "ratio = " << ratio << std::endl;
            conv << step << ",   " << batch_size << ",   " << avg_compliance << ",   " << mf << endl;
         }
         MFEM_VERIFY(IsFinite(ratio), "ratio not finite");
         if (ratio > theta)
         {
            batch_size = (int)(pow(ratio / theta,2.) * batch_size); 
         }
        //  else if (ratio < 0.1*theta)
        //  {
        //     batch_size = max(batch_size/2,batch_size_min);
        //  }

         if (visualization)
         {

            sout_u << "parallel " << num_procs << " " << myid << "\n";
            sout_u << "solution\n" << pmesh << u
                  << "window_title 'State u'" << flush;

            sout_K << "parallel " << num_procs << " " << myid << "\n";
            sout_K << "solution\n" << pmesh << K
                  << "window_title 'Control K'" << flush;

            paraview_dc.SetCycle(step);
            paraview_dc.SetTime((double)k);
            paraview_dc.Save();
         }

      }
      // λ <- λ - β (∫_Ω K dx - V⋅ vol(\Omega))
      double mass = vol_form(K);
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

         sout_K << "parallel " << num_procs << " " << myid << "\n";
         sout_K << "solution\n" << pmesh << K
                << "window_title 'Control K'" << flush;

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