#include "Stiffness.h"


Stiffness::Stiffness()
{
}


Stiffness::Stiffness(WireFrame *ptr_frame, DualGraph *ptr_dualgraph)
{
	ptr_frame_ = ptr_frame;
	ptr_dualgraph_ = ptr_dualgraph;
	ptr_parm_ = new FiberPrintPARM();		// default parameter set
}


Stiffness::Stiffness(WireFrame *ptr_frame, DualGraph *ptr_dualgraph, FiberPrintPARM *ptr_parm)
{
	ptr_frame_ = ptr_frame;
	ptr_dualgraph_ = ptr_dualgraph;
	ptr_parm_ = ptr_parm;
}


Stiffness::~Stiffness()
{
}


void Stiffness::CreateM()
{
	vector<WF_vert*> verts = *(ptr_frame_->GetVertList());
	int N = ptr_frame_->SizeOfVertList();
	int M = ptr_frame_->SizeOfEdgeList();
	int Nd = M / 2;

	M_.resize(Nd);
	for (int i = 0; i < Nd; i++)
	{
		M_[i].setZero();
	}

	for (int i = 0; i < N; i++)
	{
		//if (!verts[i]->IsFixed())
		//{
			WF_edge *edge = verts[i]->pedge_;
			while (edge != NULL)
			{
				if (edge->ID() < edge->ppair_->ID())
				{
					WF_vert *neighbor = edge->pvert_;

					double dx = verts[i]->Position().x() - neighbor->Position().x();
					double dy = verts[i]->Position().y() - neighbor->Position().y();
					double dz = verts[i]->Position().z() - neighbor->Position().z();
					double len = sqrt(dx*dx + dy*dy + dz*dz);
					double volume = pow(len, 3);

					double G = ptr_parm_->ShearModulus();
					double E = ptr_parm_->YoungsModulus();
					double material = G - E;
					double radius = ptr_parm_->Radius();

					Matrix3d Muv;
					Muv.setZero();

					/*x axis*/
					Muv(0, 0) = -material * M_PI * radius * radius / volume * dx * dx
						+ G * M_PI * radius * radius / len;
					Muv(0, 1) = -material * M_PI * radius * radius / volume * dx * dy;
					Muv(0, 2) = -material * M_PI * radius * radius / volume * dx * dz;

					/*y axis*/
					Muv(1, 0) = -material * M_PI * radius * radius / volume * dx * dy;
					Muv(1, 1) = -material * M_PI * radius * radius / volume * dy * dy
						+ G * M_PI * radius * radius / len;
					Muv(1, 2) = -material * M_PI * radius * radius / volume * dz * dy;

					/*z axis*/
					Muv(2, 0) = -material * M_PI * radius * radius / volume * dx * dz;
					Muv(2, 1) = -material * M_PI * radius * radius / volume * dy * dz;
					Muv(2, 2) = -material * M_PI * radius * radius / volume * dz * dz
						+ G * M_PI * radius * radius / len;

					M_[ptr_dualgraph_->dual_id(edge->ID())] = Muv;
				}
				edge = edge->pnext_;
			}
		//}
	}
}


void Stiffness::CreateK(const VectorXd *x)
{
	vector<WF_edge*> edges = *(ptr_frame_->GetEdgeList());
	vector<WF_vert*> verts = *(ptr_frame_->GetVertList());

	int N = ptr_frame_->SizeOfVertList();
	int M = ptr_frame_->SizeOfEdgeList();
	int Nd = M / 2;

	vector<Matrix3d> Mn;
	Mn.resize(N);
	for (int i = 0; i < N; i++)
	{
		Mn[i].setZero();
	}
	
	vector<Triplet<double>> K_list;
	for (int i = 0; i < M; i++)
	//for (int i = 0; i < Nd; i++)
	{
		int e_id = ptr_dualgraph_->dual_id(i); 
		int u = edges[i]->ppair_->pvert_->ID();
		int v = edges[i]->pvert_->ID();

		if (!verts[u]->IsFixed())
		{
			Matrix3d M_tmp = M_[e_id] * (*x)[e_id];
			Mn[u] -= M_tmp;

			if (!verts[v]->IsFixed())
			{
				for (int j = 0; j < 3; j++)
				{
					for (int k = 0; k < 3; k++)
					{
						if (M_tmp(j, k) != 0)
						{
							K_list.push_back(Triplet<double>(u * 3 + j, v * 3 + k, M_tmp(j, k)));
						}
					}
				}
			}
		}
		else
		{
			Mn[u](0, 0) = Mn[u](1, 1) = Mn[u](2, 2) = 1;
		}
	}

	for (int i = 0; i < N; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			for (int k = 0; k < 3; k++)
			{
				if (Mn[i](j, k) != 0)
				{
					K_list.push_back(Triplet<double>(i * 3 + j, i * 3 + k, Mn[i](j, k)));
				}
			}
		}
	}

	K_.resize(3 * N, 3 * N);
	K_.setFromTriplets(K_list.begin(), K_list.end());
}


void Stiffness::CreateFv(const VectorXd *x)
{
	vector<WF_vert*> verts = *(ptr_frame_->GetVertList());
	int N = ptr_frame_->SizeOfVertList();

	Fv_.resize(3 * N);
	Fv_.setZero();

	for (int i = 0; i < N; i++)
	{
		double gravity = 0;

		// if not boundary
		if (!verts[i]->IsFixed())
		{
			WF_edge *edge = verts[i]->pedge_;
			while (edge != NULL)
			{
				int e_id = ptr_dualgraph_->dual_id(edge->ID());

				gravity += (*x)[e_id] * Fe_[3 * e_id + 2];

				edge = edge->pnext_;
			} 
		}
		else
		{
			// do nothing
			//printf("Bdy; node %i: gravity %.8lf \n", u, gravity);
		}
		Fv_[3 * i + 2] = gravity / 2;
		//printf("Fv_[%i] = %.12lf \n", 3 * u + 2, gravity / 2);
	}
}


void Stiffness::CreateFe()
{
	double r = ptr_parm_->Radius();
	double density = ptr_parm_->Density();
	double g = ptr_parm_->G();

	vector<WF_edge*> edges = *(ptr_frame_->GetEdgeList());
	int M = ptr_frame_->SizeOfEdgeList();
	int Nd = M / 2;

	Fe_.resize(Nd * 3);
	Fe_.setZero();

	for (int i = 0; i < M; i++)
	{
		if (edges[i]->ID() < edges[i]->ppair_->ID())
		{
			int e_id = ptr_dualgraph_->dual_id(i);
			WF_vert *u = edges[i]->ppair_->pvert_;
			WF_vert *v = edges[i]->pvert_;

			double dx = u->Position().x() - v->Position().x();
			double dy = u->Position().y() - v->Position().y();
			double dz = u->Position().z() - v->Position().z();
			double len = sqrt(dx*dx + dy*dy + dz*dz);
			double gravity = M_PI * r * r * len * density * g;

			Fe_[3 * e_id + 2] = gravity;
		}
	}
}


void Stiffness::CalculateD(VectorXd *ptr_D, const VectorXd *ptr_x)
{
	CreateM();
	CreateK(ptr_x);
	CreateFv(ptr_x);

	cout << "KD = F solving ..." << endl;
	//SparseQR<SparseMatrix<double>, COLAMDOrdering<int>> solver;
	BiCGSTAB<SparseMatrix<double>> solver;
	//SparseLU<SparseMatrix<double>, COLAMDOrdering<int>> solver;
	//ConjugateGradient<SparseMatrix<double>> solver;

	//K_.makeCompressed();
	solver.preconditioner();
	solver.setTolerance(1e-10);
	
	solver.compute(K_);
	assert(solver.info() == Success);
	(*ptr_D) = solver.solve(Fv_);
	assert(solver.info() == Success);

	cout << "Matrix solved successfully!" << endl;
}


Matrix3d Stiffness::Me(int ei)
{
	int e_id = ptr_dualgraph_->dual_id(ei);
	return M_[e_id];
}


Vector3d Stiffness::Fe(int ei)
{
	int e_id = ptr_dualgraph_->dual_id(ei);
	Vector3d Fei;
	Fei[0] = Fe_[3 * e_id];
	Fei[1] = Fe_[3 * e_id + 1];
	Fei[2] = Fe_[3 * e_id + 2];
	return Fei;
}


void Stiffness::Debug()
{
	FILE *fp = fopen("E:\\test.txt", "wb+"); 
	vector<WF_edge*> &edges = *(ptr_frame_->GetEdgeList());
	int N = ptr_frame_->SizeOfVertList();
	int M = ptr_frame_->SizeOfEdgeList();
	int Nd = M / 2;

	CreateM();
	for (int i = 0; i < Nd; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			for (int k = 0; k < 3; k++)
			{
				if (M_[i](j, k) != 0)
				{
					fprintf(fp, "%.8f ", M_[i](j, k));
				}
			}
		}
		fprintf(fp, "\r\n");
	}

	VectorXd x;
	x.resize(Nd);
	x.setOnes();
	CreateK(&x);

	for (int i = 0; i < 3 * N; i++)
	{
		for (int j = 0; j < 3 * N; j++)
		{
			if (K_.coeff(i, j) != 0)
			{
				fprintf(fp, "(%d, %d)   %.4f\r\n", i + 1, j + 1, K_.coeff(i, j));
			}
		}
	}
	fclose(fp);

	VectorXd D;
	D.resize(3 * N);
	D.setZero();
	CalculateD(&D, &x);
	for (int i = 0; i < N; i++)
	{
		cout << D[i * 3] << " " << D[i * 3 + 1] << " " << D[i * 3 + 2] << endl;
	}
	getchar();
}