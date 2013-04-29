/*-
 * Copyright (c) 2012-2013 Ilya Kaliman
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>

#include "elec.h"
#include "private.h"

#define POL_SCF_TOL 1.0e-10
#define POL_SCF_MAX_ITER 80

static double
get_pol_damp_tt(double r)
{
	/* polarization damping parameter */
	static const double a = 0.6;

	double r2 = r * r;

	return 1.0 - exp(-a * r2) * (1.0 + a * r2);
}

static double
get_pol_damp_tt_grad(double r)
{
	/* polarization damping parameter */
	static const double a = 0.6;

	double r2 = r * r;

	return -2.0 * exp(-a * r2) * (a * a * r2);
}

static vec_t
get_multipole_field(const struct efp *efp, const struct polarizable_pt *pt,
		    const struct multipole_pt *mult_pt, const struct swf *swf)
{
	vec_t field = vec_zero;

	vec_t dr = {
		pt->x - mult_pt->x - swf->cell.x,
		pt->y - mult_pt->y - swf->cell.y,
		pt->z - mult_pt->z - swf->cell.z
	};

	double r = vec_len(&dr);
	double r3 = r * r * r;
	double r5 = r3 * r * r;
	double r7 = r5 * r * r;

	double p1 = 1.0;

	if (efp->opts.pol_damp == EFP_POL_DAMP_TT)
		p1 = get_pol_damp_tt(r);

	double t1, t2;

	/* charge */
	field.x += swf->swf * mult_pt->monopole * dr.x / r3 * p1;
	field.y += swf->swf * mult_pt->monopole * dr.y / r3 * p1;
	field.z += swf->swf * mult_pt->monopole * dr.z / r3 * p1;

	/* dipole */
	t1 = vec_dot(&mult_pt->dipole, &dr);

	field.x += swf->swf * (3.0 / r5 * t1 * dr.x - mult_pt->dipole.x / r3) * p1;
	field.y += swf->swf * (3.0 / r5 * t1 * dr.y - mult_pt->dipole.y / r3) * p1;
	field.z += swf->swf * (3.0 / r5 * t1 * dr.z - mult_pt->dipole.z / r3) * p1;

	/* quadrupole */
	t1 = quadrupole_sum(mult_pt->quadrupole, &dr);

	t2 = mult_pt->quadrupole[quad_idx(0, 0)] * dr.x +
	     mult_pt->quadrupole[quad_idx(1, 0)] * dr.y +
	     mult_pt->quadrupole[quad_idx(2, 0)] * dr.z;
	field.x += swf->swf * (-2.0 / r5 * t2 + 5.0 / r7 * t1 * dr.x) * p1;

	t2 = mult_pt->quadrupole[quad_idx(0, 1)] * dr.x +
	     mult_pt->quadrupole[quad_idx(1, 1)] * dr.y +
	     mult_pt->quadrupole[quad_idx(2, 1)] * dr.z;
	field.y += swf->swf * (-2.0 / r5 * t2 + 5.0 / r7 * t1 * dr.y) * p1;

	t2 = mult_pt->quadrupole[quad_idx(0, 2)] * dr.x +
	     mult_pt->quadrupole[quad_idx(1, 2)] * dr.y +
	     mult_pt->quadrupole[quad_idx(2, 2)] * dr.z;
	field.z += swf->swf * (-2.0 / r5 * t2 + 5.0 / r7 * t1 * dr.z) * p1;

	/* octupole-polarizability interactions are ignored */

	return (field);
}

static vec_t
get_elec_field(const struct efp *efp, size_t frag_idx, size_t pt_idx)
{
	const struct frag *frag = efp->frags + frag_idx;
	const struct polarizable_pt *pt = frag->polarizable_pts + pt_idx;
	vec_t elec_field = vec_zero;

	for (size_t i = 0; i < efp->n_frag; i++) {
		if (i == frag_idx || efp_skip_frag_pair(efp, i, frag_idx))
			continue;

		const struct frag *fr_i = efp->frags + i;
		struct swf swf = efp_make_swf(efp, fr_i, frag);

		/* field due to nuclei */
		for (size_t j = 0; j < fr_i->n_atoms; j++) {
			const struct efp_atom *at = fr_i->atoms + j;

			vec_t dr = {
				pt->x - at->x - swf.cell.x,
				pt->y - at->y - swf.cell.y,
				pt->z - at->z - swf.cell.z
			};

			double r = vec_len(&dr);
			double r3 = r * r * r;

			double p1 = 1.0;

			if (efp->opts.pol_damp == EFP_POL_DAMP_TT)
				p1 = get_pol_damp_tt(r);

			elec_field.x += swf.swf * at->znuc * dr.x / r3 * p1;
			elec_field.y += swf.swf * at->znuc * dr.y / r3 * p1;
			elec_field.z += swf.swf * at->znuc * dr.z / r3 * p1;
		}

		/* field due to multipoles */
		for (size_t j = 0; j < fr_i->n_multipole_pts; j++) {
			const struct multipole_pt *mult_pt = fr_i->multipole_pts + j;
			vec_t mult_field = get_multipole_field(efp, pt, mult_pt, &swf);

			elec_field.x += mult_field.x;
			elec_field.y += mult_field.y;
			elec_field.z += mult_field.z;
		}
	}

	if (efp->opts.terms & EFP_TERM_AI_POL) {
		/* field due to nuclei from ab initio subsystem */
		for (size_t i = 0; i < efp->n_ptc; i++) {
			const struct point_charge *at_i = efp->point_charges + i;

			vec_t dr = vec_sub(CVEC(pt->x), CVEC(at_i->x));

			double r = vec_len(&dr);
			double r3 = r * r * r;

			elec_field.x += at_i->charge * dr.x / r3;
			elec_field.y += at_i->charge * dr.y / r3;
			elec_field.z += at_i->charge * dr.z / r3;
		}
	}

	return (elec_field);
}

static enum efp_result
add_electron_density_field(struct efp *efp)
{
	enum efp_result res;
	vec_t xyz[efp->n_polarizable_pts];
	vec_t field[efp->n_polarizable_pts];

	if (!efp->get_electron_density_field) {
		/* assume no electrons */
		for (size_t i = 0; i < efp->n_frag; i++)
			for (size_t j = 0; j < efp->frags[i].n_polarizable_pts; j++)
				efp->frags[i].polarizable_pts[j].elec_field_wf = vec_zero;

		return EFP_RESULT_SUCCESS;
	}

	for (size_t i = 0, idx = 0; i < efp->n_frag; i++) {
		struct frag *frag = efp->frags + i;

		for (size_t j = 0; j < frag->n_polarizable_pts; j++, idx++) {
			struct polarizable_pt *pt = frag->polarizable_pts + j;

			xyz[idx].x = pt->x;
			xyz[idx].y = pt->y;
			xyz[idx].z = pt->z;
		}
	}

	if ((res = efp->get_electron_density_field(efp->n_polarizable_pts,
			(const double *)xyz, (double *)field,
				efp->get_electron_density_field_user_data)))
		return res;

	for (size_t i = 0, idx = 0; i < efp->n_frag; i++) {
		struct frag *frag = efp->frags + i;

		for (size_t j = 0; j < frag->n_polarizable_pts; j++, idx++) {
			struct polarizable_pt *pt = frag->polarizable_pts + j;
			pt->elec_field_wf = field[idx];
		}
	}

	return EFP_RESULT_SUCCESS;
}

#ifdef WITH_MPI
static size_t
polarizable_offset(struct efp *efp, size_t idx)
{
	return idx < efp->n_frag ? efp->frags[idx].polarizable_offset : efp->n_polarizable_pts;
}

static void
broadcast(struct efp *efp, vec_t *all)
{
	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	for (int i = 0; i < size; i++) {
		size_t off1 = polarizable_offset(efp, efp->mpi_offset[i]);
		size_t off2 = polarizable_offset(efp, efp->mpi_offset[i + 1]);

		MPI_Bcast(all + off1, (int)(off2 - off1) * 3, MPI_DOUBLE, i, MPI_COMM_WORLD);
	}
}
#endif

static enum efp_result
compute_elec_field(struct efp *efp)
{
	int rank = 0;
	enum efp_result res;
	vec_t elec_field[efp->n_polarizable_pts];

#ifdef WITH_MPI
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4)
#endif
	for (size_t i = efp->mpi_offset[rank]; i < efp->mpi_offset[rank + 1]; i++) {
		const struct frag *frag = efp->frags + i;

		for (size_t j = 0; j < frag->n_polarizable_pts; j++)
			elec_field[frag->polarizable_offset + j] = get_elec_field(efp, i, j);
	}

#ifdef WITH_MPI
	broadcast(efp, elec_field);
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4)
#endif
	for (size_t i = 0; i < efp->n_frag; i++) {
		struct frag *frag = efp->frags + i;

		for (size_t j = 0; j < frag->n_polarizable_pts; j++) {
			struct polarizable_pt *pt = frag->polarizable_pts + j;

			pt->elec_field = elec_field[frag->polarizable_offset + j];
			pt->elec_field_wf = vec_zero;
		}
	}

	if (efp->opts.terms & EFP_TERM_AI_POL)
		if ((res = add_electron_density_field(efp)))
			return res;

	return EFP_RESULT_SUCCESS;
}

static void
get_induced_dipole_field(struct efp *efp, size_t frag_idx,
			 struct polarizable_pt *pt,
			 vec_t *field, vec_t *field_conj)
{
	struct frag *fr_i = efp->frags + frag_idx;

	*field = vec_zero;
	*field_conj = vec_zero;

	for (size_t j = 0; j < efp->n_frag; j++) {
		if (j == frag_idx || efp_skip_frag_pair(efp, frag_idx, j))
			continue;

		struct frag *fr_j = efp->frags + j;
		struct swf swf = efp_make_swf(efp, fr_i, fr_j);

		for (size_t jj = 0; jj < fr_j->n_polarizable_pts; jj++) {
			struct polarizable_pt *pt_j =
				fr_j->polarizable_pts + jj;

			vec_t dr = {
				pt->x - pt_j->x + swf.cell.x,
				pt->y - pt_j->y + swf.cell.y,
				pt->z - pt_j->z + swf.cell.z
			};

			double r = vec_len(&dr);
			double r3 = r * r * r;
			double r5 = r3 * r * r;

			double t1 = vec_dot(&pt_j->induced_dipole, &dr);
			double t2 = vec_dot(&pt_j->induced_dipole_conj, &dr);

			double p1 = 1.0;

			if (efp->opts.pol_damp == EFP_POL_DAMP_TT)
				p1 = get_pol_damp_tt(r);

			field->x -= swf.swf * p1 * (pt_j->induced_dipole.x / r3 -
							3.0 * t1 * dr.x / r5);
			field->y -= swf.swf * p1 * (pt_j->induced_dipole.y / r3 -
							3.0 * t1 * dr.y / r5);
			field->z -= swf.swf * p1 * (pt_j->induced_dipole.z / r3 -
							3.0 * t1 * dr.z / r5);

			field_conj->x -= swf.swf * p1 * (pt_j->induced_dipole_conj.x / r3 -
							3.0 * t2 * dr.x / r5);
			field_conj->y -= swf.swf * p1 * (pt_j->induced_dipole_conj.y / r3 -
							3.0 * t2 * dr.y / r5);
			field_conj->z -= swf.swf * p1 * (pt_j->induced_dipole_conj.z / r3 -
							3.0 * t2 * dr.z / r5);
		}
	}
}

static double
pol_scf_iter(struct efp *efp)
{
	int rank = 0;
	double conv = 0.0;
	vec_t *id_new = malloc(efp->n_polarizable_pts * sizeof(vec_t));
	vec_t *id_conj_new = malloc(efp->n_polarizable_pts * sizeof(vec_t));

#ifdef WITH_MPI
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif
	/* compute new induced dipoles on polarizable points */
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4)
#endif
	for (size_t i = efp->mpi_offset[rank]; i < efp->mpi_offset[rank + 1]; i++) {
		struct frag *frag = efp->frags + i;

		for (size_t j = 0; j < frag->n_polarizable_pts; j++) {
			struct polarizable_pt *pt = frag->polarizable_pts + j;

			/* electric field from other induced dipoles */
			vec_t field, field_conj;
			get_induced_dipole_field(efp, i, pt, &field, &field_conj);

			/* add field that doesn't change during scf */
			field.x += pt->elec_field.x + pt->elec_field_wf.x;
			field.y += pt->elec_field.y + pt->elec_field_wf.y;
			field.z += pt->elec_field.z + pt->elec_field_wf.z;

			field_conj.x += pt->elec_field.x + pt->elec_field_wf.x;
			field_conj.y += pt->elec_field.y + pt->elec_field_wf.y;
			field_conj.z += pt->elec_field.z + pt->elec_field_wf.z;

			id_new[frag->polarizable_offset + j] =
						mat_vec(&pt->tensor, &field);
			id_conj_new[frag->polarizable_offset + j] =
						mat_trans_vec(&pt->tensor, &field_conj);
		}
	}

#ifdef WITH_MPI
	broadcast(efp, id_new);
	broadcast(efp, id_conj_new);
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4) reduction(+:conv)
#endif
	for (size_t i = efp->mpi_offset[rank]; i < efp->mpi_offset[rank + 1]; i++) {
		struct frag *frag = efp->frags + i;

		for (size_t j = 0; j < frag->n_polarizable_pts; j++) {
			struct polarizable_pt *pt = frag->polarizable_pts + j;

			conv += vec_dist(&id_new[frag->polarizable_offset + j],
					 &pt->induced_dipole);
			conv += vec_dist(&id_conj_new[frag->polarizable_offset + j],
					 &pt->induced_dipole_conj);
		}
	}

#ifdef WITH_MPI
	MPI_Allreduce(MPI_IN_PLACE, &conv, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4)
#endif
	for (size_t i = 0; i < efp->n_frag; i++) {
		struct frag *frag = efp->frags + i;

		for (size_t j = 0; j < frag->n_polarizable_pts; j++) {
			struct polarizable_pt *pt = frag->polarizable_pts + j;

			pt->induced_dipole = id_new[frag->polarizable_offset + j];
			pt->induced_dipole_conj = id_conj_new[frag->polarizable_offset + j];
		}
	}

	free(id_new);
	free(id_conj_new);

	return (conv / efp->n_polarizable_pts / 2);
}

enum efp_result
efp_compute_pol_energy(struct efp *efp, double *energy)
{
	enum efp_result res;

	if ((res = compute_elec_field(efp)))
		return res;

	/* set initial approximation - all induced dipoles are zero */
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4)
#endif
	for (size_t i = 0; i < efp->n_frag; i++) {
		struct frag *frag = efp->frags + i;

		for (size_t j = 0; j < frag->n_polarizable_pts; j++) {
			struct polarizable_pt *pt = frag->polarizable_pts + j;

			pt->induced_dipole = vec_zero;
			pt->induced_dipole_conj = vec_zero;
		}
	}

	/* compute induced dipoles self consistently */
	for (size_t iter = 1; iter <= POL_SCF_MAX_ITER; iter++) {
		if (pol_scf_iter(efp) < POL_SCF_TOL)
			break;

		if (iter == POL_SCF_MAX_ITER)
			return EFP_RESULT_POL_NOT_CONVERGED;
	}

	int rank = 0;
	double ener = 0.0;
#ifdef WITH_MPI
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4) reduction(+:ener)
#endif
	for (size_t i = efp->mpi_offset[rank]; i < efp->mpi_offset[rank + 1]; i++) {
		struct frag *frag = efp->frags + i;

		for (size_t j = 0; j < frag->n_polarizable_pts; j++) {
			struct polarizable_pt *pt = frag->polarizable_pts + j;

			ener += 0.5 * vec_dot(&pt->induced_dipole_conj,
					      &pt->elec_field_wf) -
				0.5 * vec_dot(&pt->induced_dipole,
					      &pt->elec_field);
		}
	}

#ifdef WITH_MPI
	MPI_Allreduce(MPI_IN_PLACE, &ener, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
#endif
	*energy = ener;
	return EFP_RESULT_SUCCESS;
}

static void
compute_grad_point(struct efp *efp, size_t frag_idx, size_t pt_idx)
{
	struct frag *fr_i = efp->frags + frag_idx;
	struct polarizable_pt *pt_i = fr_i->polarizable_pts + pt_idx;

	vec_t dipole_i = {
		0.5 * (pt_i->induced_dipole.x + pt_i->induced_dipole_conj.x),
		0.5 * (pt_i->induced_dipole.y + pt_i->induced_dipole_conj.y),
		0.5 * (pt_i->induced_dipole.z + pt_i->induced_dipole_conj.z)
	};

	for (size_t j = 0; j < efp->n_frag; j++) {
		if (j == frag_idx || efp_skip_frag_pair(efp, frag_idx, j))
			continue;

		struct frag *fr_j = efp->frags + j;
		struct swf swf = efp_make_swf(efp, fr_i, fr_j);

		/* energy without switching applied */
		double energy = 0.0;

		/* induced dipole - nuclei */
		for (size_t k = 0; k < fr_j->n_atoms; k++) {
			struct efp_atom *at_j = fr_j->atoms + k;

			vec_t dr = {
				at_j->x - pt_i->x - swf.cell.x,
				at_j->y - pt_i->y - swf.cell.y,
				at_j->z - pt_i->z - swf.cell.z
			};

			double p1 = 1.0, p2 = 0.0;

			if (efp->opts.pol_damp == EFP_POL_DAMP_TT) {
				double r = vec_len(&dr);

				p1 = get_pol_damp_tt(r);
				p2 = get_pol_damp_tt_grad(r);
			}

			vec_t force, add_i, add_j;

			double e = -efp_charge_dipole_energy(at_j->znuc, &dipole_i, &dr);

			efp_charge_dipole_grad(at_j->znuc, &dipole_i, &dr,
					       &force, &add_j, &add_i);
			vec_negate(&force);

			vec_scale(&force, p1);
			vec_scale(&add_i, p1);
			vec_scale(&add_j, p1);

			force.x += p2 * e * dr.x;
			force.y += p2 * e * dr.y;
			force.z += p2 * e * dr.z;

			vec_scale(&force, swf.swf);
			vec_scale(&add_i, swf.swf);
			vec_scale(&add_j, swf.swf);

			efp_add_force(fr_i, CVEC(pt_i->x), &force, &add_i);
			efp_sub_force(fr_j, CVEC(at_j->x), &force, &add_j);
			efp_add_stress(&swf.dr, &force, &efp->stress);

			energy += p1 * e;
		}

		/* induced dipole - multipoles */
		for (size_t k = 0; k < fr_j->n_multipole_pts; k++) {
			struct multipole_pt *pt_j = fr_j->multipole_pts + k;

			vec_t dr = {
				pt_j->x - pt_i->x - swf.cell.x,
				pt_j->y - pt_i->y - swf.cell.y,
				pt_j->z - pt_i->z - swf.cell.z
			};

			double p1 = 1.0, p2 = 0.0;

			if (efp->opts.pol_damp == EFP_POL_DAMP_TT) {
				double r = vec_len(&dr);

				p1 = get_pol_damp_tt(r);
				p2 = get_pol_damp_tt_grad(r);
			}

			double e = 0.0;

			vec_t force_, add_i_, add_j_;
			vec_t force = vec_zero, add_i = vec_zero, add_j = vec_zero;

			/* induced dipole - charge */
			e -= efp_charge_dipole_energy(pt_j->monopole, &dipole_i, &dr);

			efp_charge_dipole_grad(pt_j->monopole, &dipole_i, &dr,
					       &force_, &add_j_, &add_i_);
			vec_negate(&force_);
			add_3(&force, &force_, &add_i, &add_i_, &add_j, &add_j_);

			/* induced dipole - dipole */
			e += efp_dipole_dipole_energy(&dipole_i, &pt_j->dipole, &dr);

			efp_dipole_dipole_grad(&dipole_i, &pt_j->dipole, &dr,
					       &force_, &add_i_, &add_j_);
			vec_negate(&add_j_);
			add_3(&force, &force_, &add_i, &add_i_, &add_j, &add_j_);

			/* induced dipole - quadrupole */
			e += efp_dipole_quadrupole_energy(&dipole_i, pt_j->quadrupole, &dr);

			efp_dipole_quadrupole_grad(&dipole_i, pt_j->quadrupole, &dr,
						   &force_, &add_i_, &add_j_);
			add_3(&force, &force_, &add_i, &add_i_, &add_j, &add_j_);

			/* induced dipole - octupole interactions are ignored */

			vec_scale(&force, p1);
			vec_scale(&add_i, p1);
			vec_scale(&add_j, p1);

			force.x += p2 * e * dr.x;
			force.y += p2 * e * dr.y;
			force.z += p2 * e * dr.z;

			vec_scale(&force, swf.swf);
			vec_scale(&add_i, swf.swf);
			vec_scale(&add_j, swf.swf);

			efp_add_force(fr_i, CVEC(pt_i->x), &force, &add_i);
			efp_sub_force(fr_j, CVEC(pt_j->x), &force, &add_j);
			efp_add_stress(&swf.dr, &force, &efp->stress);

			energy += p1 * e;
		}

		/* induced dipole - induced dipoles */
		for (size_t jj = 0; jj < fr_j->n_polarizable_pts; jj++) {
			struct polarizable_pt *pt_j = fr_j->polarizable_pts + jj;

			vec_t dr = {
				pt_j->x - pt_i->x - swf.cell.x,
				pt_j->y - pt_i->y - swf.cell.y,
				pt_j->z - pt_i->z - swf.cell.z
			};

			vec_t half_dipole_i = {
				0.5 * pt_i->induced_dipole.x,
				0.5 * pt_i->induced_dipole.y,
				0.5 * pt_i->induced_dipole.z
			};

			double p1 = 1.0, p2 = 0.0;

			if (efp->opts.pol_damp == EFP_POL_DAMP_TT) {
				double r = vec_len(&dr);

				p1 = get_pol_damp_tt(r);
				p2 = get_pol_damp_tt_grad(r);
			}

			vec_t force, add_i, add_j;

			double e = efp_dipole_dipole_energy(&half_dipole_i,
						&pt_j->induced_dipole_conj, &dr);

			efp_dipole_dipole_grad(&half_dipole_i, &pt_j->induced_dipole_conj,
						&dr, &force, &add_i, &add_j);
			vec_negate(&add_j);

			vec_scale(&force, p1);
			vec_scale(&add_i, p1);
			vec_scale(&add_j, p1);

			force.x += p2 * e * dr.x;
			force.y += p2 * e * dr.y;
			force.z += p2 * e * dr.z;

			vec_scale(&force, swf.swf);
			vec_scale(&add_i, swf.swf);
			vec_scale(&add_j, swf.swf);

			efp_add_force(fr_i, CVEC(pt_i->x), &force, &add_i);
			efp_sub_force(fr_j, CVEC(pt_j->x), &force, &add_j);
			efp_add_stress(&swf.dr, &force, &efp->stress);

			energy += p1 * e;
		}

		vec_t force = {
			swf.dswf.x * energy,
			swf.dswf.y * energy,
			swf.dswf.z * energy
		};

		vec_atomic_add(&fr_i->force, &force);
		vec_atomic_sub(&fr_j->force, &force);
		efp_add_stress(&swf.dr, &force, &efp->stress);
	}

	/* induced dipole - ab initio nuclei */
	if (efp->opts.terms & EFP_TERM_AI_POL) {
		for (size_t j = 0; j < efp->n_ptc; j++) {
			struct point_charge *at_j = efp->point_charges + j;

			vec_t dr = vec_sub(CVEC(at_j->x), CVEC(pt_i->x));
			vec_t force, add_i, add_j;

			efp_charge_dipole_grad(at_j->charge, &dipole_i, &dr,
					       &force, &add_j, &add_i);
			vec_negate(&add_i);

			vec_atomic_add(&at_j->grad, &force);
			efp_sub_force(fr_i, CVEC(pt_i->x), &force, &add_i);
		}
	}
}

static void
compute_grad(struct efp *efp)
{
	int rank = 0;
#ifdef WITH_MPI
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4)
#endif
	for (size_t i = efp->mpi_offset[rank]; i < efp->mpi_offset[rank + 1]; i++)
		for (size_t j = 0; j < efp->frags[i].n_polarizable_pts; j++)
			compute_grad_point(efp, i, j);
}

enum efp_result
efp_compute_pol(struct efp *efp)
{
	if (!(efp->opts.terms & EFP_TERM_POL))
		return EFP_RESULT_SUCCESS;

	enum efp_result res;

	if ((res = efp_compute_pol_energy(efp, &efp->energy.polarization)))
		return res;

	if (efp->do_gradient)
		compute_grad(efp);

	return EFP_RESULT_SUCCESS;
}

void
efp_update_pol(struct frag *frag)
{
	for (size_t i = 0; i < frag->n_polarizable_pts; i++) {
		efp_move_pt(CVEC(frag->x), &frag->rotmat,
			CVEC(frag->lib->polarizable_pts[i].x),
			VEC(frag->polarizable_pts[i].x));

		const mat_t *in = &frag->lib->polarizable_pts[i].tensor;
		mat_t *out = &frag->polarizable_pts[i].tensor;

		efp_rotate_t2(&frag->rotmat, (const double *)in, (double *)out);
	}
}
