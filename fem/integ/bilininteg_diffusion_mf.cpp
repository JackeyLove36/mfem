// Copyright (c) 2010-2023, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "../bilininteg.hpp"
#include "../gridfunc.hpp"
#include "../ceed/integrators/diffusion/diffusion.hpp"

using namespace std;

namespace mfem
{

void DiffusionIntegrator::AssembleMF(const FiniteElementSpace &fes)
{
   Mesh *mesh = fes.GetMesh();
   if (mesh->GetNE() == 0) { return; }
   if (DeviceCanUseCeed())
   {
      delete ceedOp;
      if (MQ) { ceedOp = new ceed::MFDiffusionIntegrator(*this, fes, MQ); }
      else if (VQ) { ceedOp = new ceed::MFDiffusionIntegrator(*this, fes, VQ); }
      else { ceedOp = new ceed::MFDiffusionIntegrator(*this, fes, Q); }
      return;
   }

   // Assuming the same element type
   // const FiniteElement &el = *fes.GetFE(0);
   // const IntegrationRule *ir = IntRule ? IntRule : &GetRule(el, el);
   MFEM_ABORT("Error: DiffusionIntegrator::AssembleMF only implemented with"
              " libCEED");
}

void DiffusionIntegrator::AssembleMFBoundary(const FiniteElementSpace &fes)
{
   Mesh *mesh = fes.GetMesh();
   if (mesh->GetNBE() == 0) { return; }
   if (DeviceCanUseCeed())
   {
      delete ceedOp;
      if (MQ) { ceedOp = new ceed::MFDiffusionIntegrator(*this, fes, MQ, true); }
      else if (VQ) { ceedOp = new ceed::MFDiffusionIntegrator(*this, fes, VQ, true); }
      else { ceedOp = new ceed::MFDiffusionIntegrator(*this, fes, Q, true); }
      return;
   }

   // Assuming the same element type
   // const FiniteElement &el = *fes.GetBE(0);
   // const IntegrationRule *ir = IntRule ? IntRule : &GetRule(el, el);
   MFEM_ABORT("Error: DiffusionIntegrator::AssembleMFBoundary only implemented with"
              " libCEED");
}

void DiffusionIntegrator::AssembleDiagonalMF(Vector &diag)
{
   if (DeviceCanUseCeed())
   {
      ceedOp->GetDiagonal(diag);
   }
   else
   {
      MFEM_ABORT("Error: DiffusionIntegrator::AssembleDiagonalMF only"
                 " implemented with libCEED");
   }
}

void DiffusionIntegrator::AddMultMF(const Vector &x, Vector &y) const
{
   if (DeviceCanUseCeed())
   {
      ceedOp->AddMult(x, y);
   }
   else
   {
      MFEM_ABORT("Error: DiffusionIntegrator::AddMultMF only implemented with"
                 " libCEED");
   }
}

}
