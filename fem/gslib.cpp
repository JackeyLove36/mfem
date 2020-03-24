﻿// Copyright (c) 2010-2020, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "gslib.hpp"

#ifdef MFEM_USE_GSLIB

// Ignore warnings from the gslib header (GCC version)
#ifdef MFEM_HAVE_GCC_PRAGMA_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "gslib.h"

#ifdef MFEM_HAVE_GCC_PRAGMA_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif

namespace mfem
{

FindPointsGSLIB::FindPointsGSLIB()
   : mesh(NULL), ir_simplex(NULL), gsl_mesh(), fdata2D(NULL), fdata3D(NULL),
     dim(-1)
{
   gsl_comm = new comm;
#ifdef MFEM_USE_MPI
   MPI_Init(NULL, NULL);
   MPI_Comm comm = MPI_COMM_WORLD;;
   comm_init(gsl_comm, comm);
#else
   comm_init(gsl_comm, 0);
#endif
}

FindPointsGSLIB::~FindPointsGSLIB()
{
   delete gsl_comm;
   delete ir_simplex;
}

#ifdef MFEM_USE_MPI
FindPointsGSLIB::FindPointsGSLIB(MPI_Comm _comm)
   : mesh(NULL), ir_simplex(NULL), gsl_mesh(), fdata2D(NULL), fdata3D(NULL),
     dim(-1)
{
   gsl_comm = new comm;
   comm_init(gsl_comm, _comm);
}
#endif

void FindPointsGSLIB::Setup(Mesh &m, double bb_t, double newt_tol, int npt_max)
{
   MFEM_VERIFY(m.GetNodes() != NULL, "Mesh nodes are required.");
   MFEM_VERIFY(m.GetNumGeometries(m.Dimension()) == 1,
               "Mixed meshes are not currently supported in FindPointsGSLIB.");

   mesh = &m;
   dim  = mesh->Dimension();
   const FiniteElement *fe = mesh->GetNodalFESpace()->GetFE(0);
   unsigned dof1D = fe->GetOrder() + 1;
   const int gt   = fe->GetGeomType();

   if (gt == Geometry::TRIANGLE || gt == Geometry::TETRAHEDRON ||
       gt == Geometry::PRISM)
   {
      GetSimplexNodalCoordinates();
   }
   else if (gt == Geometry::SQUARE || gt == Geometry::CUBE)
   {
      GetQuadHexNodalCoordinates();
   }
   else
   {
      MFEM_ABORT("Element type not currently supported in FindPointsGSLIB.");
   }

   const int pts_cnt = gsl_mesh.Size()/dim,
             NEtot = pts_cnt/(int)pow(dof1D, dim);

   if (dim == 2)
   {
      unsigned nr[2] = { dof1D, dof1D };
      unsigned mr[2] = { 2*dof1D, 2*dof1D };
      double * const elx[2] = { &gsl_mesh(0), &gsl_mesh(pts_cnt) };
      fdata2D = findpts_setup_2(gsl_comm, elx, nr, NEtot, mr, bb_t,
                                pts_cnt, pts_cnt, npt_max, newt_tol);
   }
   else
   {
      unsigned nr[3] = { dof1D, dof1D, dof1D };
      unsigned mr[3] = { 2*dof1D, 2*dof1D, 2*dof1D };
      double * const elx[3] =
      { &gsl_mesh(0), &gsl_mesh(pts_cnt), &gsl_mesh(2*pts_cnt) };
      fdata3D = findpts_setup_3(gsl_comm, elx, nr, NEtot, mr, bb_t,
                                pts_cnt, pts_cnt, npt_max, newt_tol);
   }
}

void FindPointsGSLIB::FindPoints(const Vector &point_pos,
                                 Array<unsigned int> &codes,
                                 Array<unsigned int> &proc_ids,
                                 Array<unsigned int> &elem_ids,
                                 Vector &ref_pos, Vector &dist)
{
   const int points_cnt = point_pos.Size() / dim;
   if (dim == 2)
   {
      const double *xv_base[2];
      xv_base[0] = point_pos.GetData();
      xv_base[1] = point_pos.GetData() + points_cnt;
      unsigned xv_stride[2];
      xv_stride[0] = sizeof(double);
      xv_stride[1] = sizeof(double);
      findpts_2(codes.GetData(), sizeof(unsigned int),
                proc_ids.GetData(), sizeof(unsigned int),
                elem_ids.GetData(), sizeof(unsigned int),
                ref_pos.GetData(), sizeof(double) * dim,
                dist.GetData(), sizeof(double),
                xv_base, xv_stride, points_cnt, fdata2D);
   }
   else
   {
      const double *xv_base[3];
      xv_base[0] = point_pos.GetData();
      xv_base[1] = point_pos.GetData() + points_cnt;
      xv_base[2] = point_pos.GetData() + 2*points_cnt;
      unsigned xv_stride[3];
      xv_stride[0] = sizeof(double);
      xv_stride[1] = sizeof(double);
      xv_stride[2] = sizeof(double);
      findpts_3(codes.GetData(), sizeof(unsigned int),
                proc_ids.GetData(), sizeof(unsigned int),
                elem_ids.GetData(), sizeof(unsigned int),
                ref_pos.GetData(), sizeof(double) * dim,
                dist.GetData(), sizeof(double),
                xv_base, xv_stride, points_cnt, fdata3D);
   }
}

void FindPointsGSLIB::Interpolate(Array<unsigned int> &codes,
                                  Array<unsigned int> &proc_ids,
                                  Array<unsigned int> &elem_ids,
                                  Vector &ref_pos, const GridFunction &field_in,
                                  Vector &field_out)
{

   H1_FECollection ind_fec(mesh->GetNodalFESpace()->GetFE(0)->GetOrder(), dim);
   FiniteElementSpace ind_fes(mesh, &ind_fec);
   GridFunction field_in_scalar;
   field_in_scalar.SetSpace(&ind_fes);
   Vector node_vals;

   const int ncomp      = field_in.FESpace()->GetVDim(),
             points_cnt = field_in.Size() / ncomp;

   for (int i = 0; i < ncomp; i++)
   {
      int dataptr = i*points_cnt;
      field_in_scalar.SetData(field_in.GetData()+dataptr);
      GetNodeValues(field_in_scalar, node_vals);

      if (dim==2)
      {
         findpts_eval_2(field_out.GetData()+dataptr, sizeof(double),
                        codes.GetData(),       sizeof(unsigned int),
                        proc_ids.GetData(),    sizeof(unsigned int),
                        elem_ids.GetData(),    sizeof(unsigned int),
                        ref_pos.GetData(),     sizeof(double) * dim,
                        points_cnt, node_vals.GetData(), fdata2D);
      }
      else
      {
         findpts_eval_3(field_out.GetData()+dataptr, sizeof(double),
                        codes.GetData(),       sizeof(unsigned int),
                        proc_ids.GetData(),    sizeof(unsigned int),
                        elem_ids.GetData(),    sizeof(unsigned int),
                        ref_pos.GetData(),     sizeof(double) * dim,
                        points_cnt, node_vals.GetData(), fdata3D);
      }
   }
}

void FindPointsGSLIB::FreeData()
{
   if (dim == 2)
   {
      findpts_free_2(fdata2D);
   }
   else
   {
      findpts_free_3(fdata3D);
   }
   gsl_mesh.Destroy();
}

void FindPointsGSLIB::GetNodeValues(const GridFunction &gf_in,
                                    Vector &node_vals)
{
   MFEM_ASSERT(gf_in.FESpace()->GetVDim() == 1, "Scalar function expected.");

   const FiniteElement *fe = mesh->GetNodalFESpace()->GetFE(0);
   const Geometry::Type gt = fe->GetGeomType();
   const int            NE = mesh->GetNE();

   if (gt == Geometry::SQUARE || gt == Geometry::CUBE)
   {
      const GridFunction *nodes     = mesh->GetNodes();
      const FiniteElementSpace *fes = nodes->FESpace();
      const IntegrationRule &ir     = fes->GetFE(0)->GetNodes();
      const int dof_cnt = ir.GetNPoints();

      node_vals.SetSize(NE * dof_cnt);

      const TensorBasisElement *tbe =
         dynamic_cast<const TensorBasisElement *>(fes->GetFE(0));
      MFEM_VERIFY(tbe != NULL, "TensorBasis FiniteElement expected.");
      const Array<int> &dof_map = tbe->GetDofMap();

      int pt_id = 0;
      Vector vals_el;
      for (int i = 0; i < NE; i++)
      {
         gf_in.GetValues(i, ir, vals_el);
         for (int j = 0; j < dof_cnt; j++)
         {
            node_vals(pt_id++) = vals_el(dof_map[j]);
         }
      }
   }
   else if (gt == Geometry::TRIANGLE || gt == Geometry::TETRAHEDRON ||
            gt == Geometry::PRISM)
   {
      const int dof_cnt = ir_simplex->GetNPoints();
      node_vals.SetSize(NE * dof_cnt);

      int pt_id = 0;
      Vector vals_el;
      for (int j = 0; j < NE; j++)
      {
         gf_in.GetValues(j, *ir_simplex, vals_el);
         for (int i = 0; i < dof_cnt; i++)
         {
            node_vals(pt_id++) = vals_el(i);
         }
      }
   }
   else
   {
      MFEM_ABORT("Element type not currently supported.");
   }
}

void FindPointsGSLIB::GetQuadHexNodalCoordinates()
{
   const GridFunction *nodes     = mesh->GetNodes();
   const FiniteElementSpace *fes = nodes->FESpace();

   const int NE      = mesh->GetNE(),
             dof_cnt = fes->GetFE(0)->GetDof(),
             pts_cnt = NE * dof_cnt;
   gsl_mesh.SetSize(dim * pts_cnt);

   const TensorBasisElement *tbe =
      dynamic_cast<const TensorBasisElement *>(fes->GetFE(0));
   MFEM_VERIFY(tbe != NULL, "TensorBasis FiniteElement expected.");
   const Array<int> &dof_map = tbe->GetDofMap();

   DenseMatrix pos(dof_cnt, dim);
   Vector posV(pos.Data(), dof_cnt * dim);
   Array<int> xdofs(dof_cnt * dim);

   int pt_id = 0;
   for (int i = 0; i < NE; i++)
   {
      fes->GetElementVDofs(i, xdofs);
      nodes->GetSubVector(xdofs, posV);
      for (int j = 0; j < dof_cnt; j++)
      {
         for (int d = 0; d < dim; d++)
         {
            gsl_mesh(pts_cnt * d + pt_id) = pos(dof_map[j], d);
         }
         pt_id++;
      }
   }
}

void FindPointsGSLIB::GetSimplexNodalCoordinates()
{
   const FiniteElement *fe   = mesh->GetNodalFESpace()->GetFE(0);
   const Geometry::Type gt   = fe->GetGeomType();
   const GridFunction *nodes = mesh->GetNodes();
   Mesh *meshsplit           = NULL;
   const int NE              = mesh->GetNE();
   int NEsplit;

   // Split the reference element into a reference submesh of quads or hexes.
   if (gt == Geometry::TRIANGLE)
   {
      int Nvert = 7;
      NEsplit = 3;
      meshsplit = new Mesh(2, Nvert, NEsplit, 0, 2);

      const double quad_v[7][2] =
      {
         {0, 0}, {0.5, 0}, {1, 0}, {0, 0.5},
         {1./3., 1./3.}, {0.5, 0.5}, {0, 1}
      };
      const int quad_e[3][4] =
      {
         {3, 4, 1, 0}, {4, 5, 2, 1}, {6, 5, 4, 3}
      };

      for (int j = 0; j < Nvert; j++)
      {
         meshsplit->AddVertex(quad_v[j]);
      }
      for (int j = 0; j < NEsplit; j++)
      {
         int attribute = j + 1;
         meshsplit->AddQuad(quad_e[j], attribute);
      }
      meshsplit->FinalizeQuadMesh(1, 1, true);
   }
   else if (gt == Geometry::TETRAHEDRON)
   {
      int Nvert = 15;
      NEsplit = 4;
      meshsplit = new Mesh(3, Nvert, NEsplit, 0, 3);

      const double hex_v[15][3] =
      {
         {0, 0, 0.}, {1, 0., 0.}, {0., 1., 0.}, {0, 0., 1.},
         {0.5, 0., 0.}, {0.5, 0.5, 0.}, {0., 0.5, 0.},
         {0., 0., 0.5}, {0.5, 0., 0.5}, {0., 0.5, 0.5},
         {1./3., 0., 1./3.}, {1./3., 1./3., 1./3.}, {0, 1./3., 1./3.},
         {1./3., 1./3., 0}, {0.25, 0.25, 0.25}
      };
      const int hex_e[4][8] =
      {
         {0, 4, 10, 7, 6, 13, 14, 12},
         {4, 1, 8, 10, 13, 5, 11, 14},
         {13, 5, 11, 14, 6, 2, 9, 12},
         {10, 8, 3, 7, 14, 11, 9, 12}
      };

      for (int j = 0; j < Nvert; j++)
      {
         meshsplit->AddVertex(hex_v[j]);
      }
      for (int j = 0; j < NEsplit; j++)
      {
         int attribute = j + 1;
         meshsplit->AddHex(hex_e[j], attribute);
      }
      meshsplit->FinalizeHexMesh(1, 1, true);
   }
   else if (gt == Geometry::PRISM)
   {
      int Nvert = 14;
      NEsplit = 3;
      meshsplit = new Mesh(3, Nvert, NEsplit, 0, 3);

      const double hex_v[14][3] =
      {
         {0, 0, 0}, {0.5, 0, 0}, {1, 0, 0}, {0, 0.5, 0},
         {1./3., 1./3., 0}, {0.5, 0.5, 0}, {0, 1, 0},
         {0, 0, 1}, {0.5, 0, 1}, {1, 0, 1}, {0, 0.5, 1},
         {1./3., 1./3., 1}, {0.5, 0.5, 1}, {0, 1, 1}
      };
      const int hex_e[3][8] =
      {
         {3, 4, 1, 0, 10, 11, 8, 7},
         {4, 5, 2, 1, 11, 12, 9, 8},
         {6, 5, 4, 3, 13, 12, 11, 10}
      };

      for (int j = 0; j < Nvert; j++)
      {
         meshsplit->AddVertex(hex_v[j]);
      }
      for (int j = 0; j < NEsplit; j++)
      {
         int attribute = j + 1;
         meshsplit->AddHex(hex_e[j], attribute);
      }
      meshsplit->FinalizeHexMesh(1, 1, true);
   }

   // Curve the reference submesh.
   H1_FECollection fec(fe->GetOrder(), dim);
   FiniteElementSpace nodal_fes(meshsplit, &fec, dim);
   meshsplit->SetNodalFESpace(&nodal_fes);

   const int dof_cnt = nodal_fes.GetFE(0)->GetDof(),
             pts_cnt = NEsplit * dof_cnt;
   Vector irlist(dim * pts_cnt);

   const TensorBasisElement *tbe =
      dynamic_cast<const TensorBasisElement *>(nodal_fes.GetFE(0));
   MFEM_VERIFY(tbe != NULL, "TensorBasis FiniteElement expected.");
   const Array<int> &dof_map = tbe->GetDofMap();

   DenseMatrix pos(dof_cnt, dim);
   Vector posV(pos.Data(), dof_cnt * dim);
   Array<int> xdofs(dof_cnt * dim);

   // Create an IntegrationRule on the nodes of the reference submesh.
   ir_simplex = new IntegrationRule(pts_cnt);
   GridFunction *nodesplit = meshsplit->GetNodes();
   int pt_id = 0;
   for (int i = 0; i < NEsplit; i++)
   {
      nodal_fes.GetElementVDofs(i, xdofs);
      nodesplit->GetSubVector(xdofs, posV);
      for (int j = 0; j < dof_cnt; j++)
      {
         for (int d = 0; d < dim; d++)
         {
            irlist(pts_cnt * d + pt_id) = pos(dof_map[j], d);
         }
         ir_simplex->IntPoint(pt_id).x = irlist(pt_id);
         ir_simplex->IntPoint(pt_id).y = irlist(pts_cnt + pt_id);
         if (dim == 3)
         {
            ir_simplex->IntPoint(pt_id).z = irlist(2*pts_cnt + pt_id);
         }
         pt_id++;
      }
   }

   // Initialize gsl_mesh with the positions of the split physical elements.
   pt_id = 0;
   Vector locval(dim);
   const int tot_pts_cnt = pts_cnt*NE;
   gsl_mesh.SetSize(tot_pts_cnt*dim);
   for (int j = 0; j < NE; j++)
   {
      for (int i = 0; i < dof_cnt*NEsplit; i++)
      {
         const IntegrationPoint &ip = ir_simplex->IntPoint(i);
         nodes->GetVectorValue(j, ip, locval);
         for (int d = 0; d < dim; d++)
         {
            gsl_mesh(tot_pts_cnt*d + pt_id) = locval(d);
         }
         pt_id++;
      }
   }

   delete meshsplit;
}

} // namespace mfem

#endif // MFEM_USE_GSLIB
