/* TREERAY: progressive dust+H2 attenuation during tree walk.
 *
 * Like treecol.cc, computes TREE_RAD column densities and G0_VARIABLE
 * radiation fluxes using the gravity tree. The key difference: stellar
 * fluxes are attenuated by the dust/H2 column accumulated from closer
 * gas, so stars behind dense clumps are properly shadowed.
 *
 * Algorithm:
 *   Pass 1: Walk the tree, collect all interactions into a buffer.
 *           Also accumulate column densities directly (same as treecol).
 *   Pass 2: Sort buffer by (pixel, distance). Sweep outward per pixel,
 *           accumulating optical depth. Attenuate luminous contributions.
 *
 * Compile flag: TREE_RAY (implies TREE_RAD + G0_VARIABLE via precompiler_logic.h)
 */

#include <mpi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../declarations/allvars.h"
#include "../core/proto.h"
#include "../mesh/kernel.h"
#include "healpix_utils.h"
#ifdef TREE_RAD_H2
#include "../cooling/chemcool/chemcool_consts.h"
#endif

#if defined(BOX_PERIODIC) && !defined(GRAVITY_NOT_PERIODIC)
#define GRAVITY_NEAREST_XYZ(x,y,z,sign) NEAREST_XYZ(x,y,z,sign)
#else
#define GRAVITY_NEAREST_XYZ(x,y,z,sign)
#endif

#ifdef TREE_RAY

#define TREERAY_OPENING_ANGLE2 (All.ErrTolTheta * All.ErrTolTheta)
#define TAG_TREERAY_A 610
#define TAG_TREERAY_B 611
#define TREE_RAY_MAX_INTERACTIONS 32768

/* Dust cross-sections [cm^2 per H atom] — same as calc_photo.F */
#define SIGMA_DUST_FUV 2.0e-21
#define SIGMA_DUST_NUV 1.3e-21
#define SIGMA_DUST_OPT 0.5e-21

#ifdef TREE_RAY_IR
#define SIGMA_DUST_IR_REF 8.27e-25  /* IR dust opacity [cm^2/H] at 20K: 4.68e-31*400/(4*5.67e-5) */
#endif

#ifdef TREE_RAY_PI
#define SIGMA_ION_HI  6.3e-18      /* HI photoionization cross-section at threshold [cm^2] */
#define E_PHOT_ION_EV 27.2         /* effective photon energy for single-bin ionizing [eV] */
#define ELECTRONVOLT_IN_ERGS_LOCAL 1.60218e-12
#endif

/* ============================================================
 *  Interaction buffer entry
 * ============================================================ */
struct tree_ray_interaction {
    float r2;               /* distance squared (code units) */
    float gasmass;          /* gas mass (code units; 0 for stars) */
    float h2mass;           /* H2 mass (code units) */
    float comass;           /* CO mass (code units) */
    float uv_lum;           /* UV luminosity [erg/s] */
    float lw_lum;           /* LW luminosity */
    float nuv_lum;          /* NUV luminosity */
    float opt_lum;          /* OPT luminosity */
#ifdef TREE_RAY_IR
    float ir_lum;           /* IR luminosity [erg/s] (from dust in gas cells) */
#endif
#ifdef TREE_RAY_PI
    float ion_lum;          /* ionizing luminosity [erg/s] (from star particles) */
    float neutral_h_mass;   /* neutral H mass (code units; for ionizing opacity) */
#endif
    int   iheal;            /* HEALPix pixel index (0..NPIX-1) */
};

static int compare_pixel_then_r2(const void *a, const void *b)
{
    const struct tree_ray_interaction *ia = (const struct tree_ray_interaction *)a;
    const struct tree_ray_interaction *ib = (const struct tree_ray_interaction *)b;
    if(ia->iheal != ib->iheal) return (ia->iheal < ib->iheal) ? -1 : 1;
    if(ia->r2 < ib->r2) return -1;
    if(ia->r2 > ib->r2) return 1;
    return 0;
}

/* ============================================================
 *  MPI data structures (same layout as treecol)
 * ============================================================ */
struct treeray_data_in {
    MyFloat Pos[3];
    int NodeList[NODELISTLENGTH];
    /* Per-pixel cumulative optical depths from the local domain, so the
     * remote domain can start its attenuation sweep with the correct τ.
     * Approximate: assumes local gas is closer than remote gas. */
    MyFloat tau_fuv[NPIX];
    MyFloat tau_nuv[NPIX];
    MyFloat tau_opt[NPIX];
#ifdef TREE_RAY_IR
    MyFloat tau_ir[NPIX];
#endif
#ifdef TREE_RAY_PI
    MyFloat tau_ion[NPIX];
#endif
};

struct treeray_data_out {
    MyDouble Projection[NPIX];
#ifdef TREE_RAD_H2
    MyDouble ProjectionH2[NPIX];
    MyDouble ProjectionCO[NPIX];
#endif
    MyDouble UV_flux[NPIX];
    MyDouble LW_flux[NPIX];
    MyDouble NUV_flux[NPIX];
    MyDouble OPT_flux[NPIX];
#ifdef TREE_RAY_IR
    MyDouble IR_flux[NPIX];
#endif
#ifdef TREE_RAY_PI
    MyDouble Ion_flux[NPIX];
#endif
};

static struct treeray_data_in *TreeRayDataIn, *TreeRayDataGet;
static struct treeray_data_out *TreeRayDataOut, *TreeRayDataResult;

/* Per-particle storage for local τ (indexed by particle ID, allocated in driver) */
static double (*local_tau_fuv)[NPIX] = NULL;
static double (*local_tau_nuv)[NPIX] = NULL;
static double (*local_tau_opt)[NPIX] = NULL;
#ifdef TREE_RAY_IR
static double (*local_tau_ir)[NPIX] = NULL;
#endif
#ifdef TREE_RAY_PI
static double (*local_tau_ion)[NPIX] = NULL;
#endif

/* ============================================================
 *  Core tree walk with buffered interactions
 * ============================================================ */
static int tree_ray_evaluate(int target, int mode, int *exportflag, int *exportnodecount, int *exportindex,
                             struct tree_ray_interaction *ibuf)
{
    struct NODE *nop = 0;
    int no, nodesinlist = 0, listindex = 0;
    int maxPart = All.MaxPart, maxNodes = MaxNodes;
    long bunchSize = All.BunchSize;
    integertime ti_Current = All.Ti_Current;
    double r2, dx, dy, dz, xtmp, pos_x, pos_y, pos_z;
    xtmp = 0;

    /* Column density arrays — accumulated directly (same as treecol) */
    double treecol_Projection[NPIX] = {0};
#ifdef TREE_RAD_H2
    double treecol_ProjectionH2[NPIX] = {0}, treecol_ProjectionCO[NPIX] = {0};
    double h2mass = 0, comass = 0;
#endif
    double gasmass = 0;
    double uv_lum = 0, lw_lum = 0, nuv_lum = 0, opt_lum = 0;
#ifdef TREE_RAY_IR
    double ir_lum = 0;
#endif
#ifdef TREE_RAY_PI
    double ion_lum = 0, neutral_h_mass_local = 0;
#endif

    int n_interactions = 0;
    int buffer_overflow = 0;

    double shielding_length = All.ShieldingLength / All.cf_atime;
    double shielding_length2 = shielding_length * shielding_length;

    /* Unit conversion: gasmass/area [code] → N_H [cm^-2] */
    double fac_mass_to_NH = UNIT_DENSITY_IN_CGS * UNIT_LENGTH_IN_CGS * All.cf_a2inv
                            / ((1.0 + 4.0 * ABHE) * PROTONMASS_CGS);
    /* Dust-to-gas ratio for opacity */
    double DGR = All.DGRnormalized;  /* TODO: could use local Z per particle */

    /* Precompute effective cross-sections [code-unit column → optical depth] */
    double sigma_fuv = SIGMA_DUST_FUV * DGR * fac_mass_to_NH;
    double sigma_nuv = SIGMA_DUST_NUV * DGR * fac_mass_to_NH;
    double sigma_opt = SIGMA_DUST_OPT * DGR * fac_mass_to_NH;
#ifdef TREE_RAY_IR
    double sigma_ir  = SIGMA_DUST_IR_REF * DGR * fac_mass_to_NH;  /* IR dust opacity (much lower than UV) */
#endif
#ifdef TREE_RAY_PI
    /* Ionizing opacity: neutral_h_mass/area [code] → N_HI [cm⁻²], then × σ_HI.
     * neutral_h_mass already has HYDROGEN_MASSFRAC baked in, so divide by m_H only. */
    double fac_neutralh_to_NHI = UNIT_DENSITY_IN_CGS * UNIT_LENGTH_IN_CGS * All.cf_a2inv / PROTONMASS_CGS;
    double sigma_ion = SIGMA_ION_HI * fac_neutralh_to_NHI;  /* converts neutral_h_col/area to τ_ion */
#endif

    /* set target position */
    if(mode == 0) {
        pos_x = P[target].Pos[0]; pos_y = P[target].Pos[1]; pos_z = P[target].Pos[2];
    } else {
        pos_x = TreeRayDataGet[target].Pos[0]; pos_y = TreeRayDataGet[target].Pos[1]; pos_z = TreeRayDataGet[target].Pos[2];
    }

    /* start tree walk */
    if(mode == 0) {
        no = maxPart;
    } else {
        nodesinlist++;
        no = TreeRayDataGet[target].NodeList[0];
        no = Nodes[no].u.d.nextnode;
    }

    /* ---- Pass 1: walk tree, collect interactions into buffer ---- */
    while(no >= 0)
    {
        while(no >= 0)
        {
            if(no < maxPart) /* single particle */
            {
                if(P[no].Ti_current != ti_Current) {
#ifdef _OPENMP
#pragma omp critical(_particledrift_treeray_)
#endif
                    { drift_particle(no, ti_Current); }
                }

                dx = P[no].Pos[0] - pos_x; dy = P[no].Pos[1] - pos_y; dz = P[no].Pos[2] - pos_z;
                GRAVITY_NEAREST_XYZ(dx, dy, dz, -1);
                r2 = dx*dx + dy*dy + dz*dz;

                gasmass = 0;
                if(P[no].Type == 0) gasmass = P[no].Mass;
#ifdef TREE_RAD_H2
                h2mass = 0; comass = 0;
                if(P[no].Type == 0) {
                    h2mass = 2.0 * CellP[no].TracAbund[IH2] * HYDROGEN_MASSFRAC * P[no].Mass;
#if defined(TREE_RAD_CO) && CHEMISTRYNETWORK != 1 && CHEMISTRYNETWORK != 4
                    comass = 28.0 * CellP[no].TracAbund[ICO] * HYDROGEN_MASSFRAC * P[no].Mass;
#endif
                }
#endif
                uv_lum = 0; lw_lum = 0; nuv_lum = 0; opt_lum = 0;
                if(P[no].Type == 4 || P[no].Type == 5) {
                    uv_lum = P[no].UV_luminosity; lw_lum = P[no].LW_luminosity;
#ifdef GALSF_RESOLVEDISM_NUV_VARIABLE
                    nuv_lum = P[no].NUV_luminosity;
#endif
#ifdef GALSF_RESOLVEDISM_OPT_VARIABLE
                    opt_lum = P[no].OPT_luminosity;
#endif
                }
#ifdef TREE_RAY_IR
                ir_lum = 0;
                if(P[no].Type == 0) { ir_lum = P[no].IR_luminosity; }
#endif
#ifdef TREE_RAY_PI
                ion_lum = 0; neutral_h_mass_local = 0;
                if(P[no].Type == 4 || P[no].Type == 5) { ion_lum = P[no].Ion_luminosity; }
                if(P[no].Type == 0) { neutral_h_mass_local = (1.0 - CellP[no].TracAbund[IHP]) * HYDROGEN_MASSFRAC * P[no].Mass; }
#endif

                if(r2 > 0)
                {
                    int dominated_by_gas = (gasmass > 0);
                    int has_luminosity = (uv_lum > 0 || lw_lum > 0 || nuv_lum > 0 || opt_lum > 0);
#ifdef TREE_RAY_IR
                    has_luminosity = has_luminosity || (ir_lum > 0);
#endif
#ifdef TREE_RAY_PI
                    has_luminosity = has_luminosity || (ion_lum > 0);
                    /* For PI, neutral gas is an absorber even beyond ShieldingLength
                     * if there are ionizing sources. But we still limit columns to ShieldingLength. */
#endif
                    int within_shield = (r2 < shielding_length2);

                    /* Accumulate columns directly (same as treecol) */
                    if(dominated_by_gas && within_shield) {
                        long iheal;
                        double vec_hp[3] = {dx, dy, dz};
                        vec2pix_ring(NSIDE, vec_hp, &iheal);
                        double area = (4.0*M_PI / NPIX) * r2;
                        treecol_Projection[iheal] += gasmass / area;
#ifdef TREE_RAD_H2
                        treecol_ProjectionH2[iheal] += h2mass / area;
                        treecol_ProjectionCO[iheal] += comass / area;
#endif
                    }

                    /* Buffer this interaction if it contributes gas or flux */
                    if((dominated_by_gas && within_shield) || has_luminosity)
                    {
                        if(n_interactions < TREE_RAY_MAX_INTERACTIONS) {
                            long iheal;
                            double vec_hp[3] = {dx, dy, dz};
                            vec2pix_ring(NSIDE, vec_hp, &iheal);
                            ibuf[n_interactions].r2 = (float)r2;
                            ibuf[n_interactions].gasmass = dominated_by_gas ? (float)gasmass : 0.0f;
                            ibuf[n_interactions].h2mass = (float)h2mass;
                            ibuf[n_interactions].comass = (float)comass;
                            ibuf[n_interactions].uv_lum = (float)uv_lum;
                            ibuf[n_interactions].lw_lum = (float)lw_lum;
                            ibuf[n_interactions].nuv_lum = (float)nuv_lum;
                            ibuf[n_interactions].opt_lum = (float)opt_lum;
#ifdef TREE_RAY_IR
                            ibuf[n_interactions].ir_lum = (float)ir_lum;
#endif
#ifdef TREE_RAY_PI
                            ibuf[n_interactions].ion_lum = (float)ion_lum;
                            ibuf[n_interactions].neutral_h_mass = (dominated_by_gas && within_shield) ? (float)neutral_h_mass_local : 0.0f;
#endif
                            ibuf[n_interactions].iheal = (int)iheal;
                            n_interactions++;
                        } else {
                            buffer_overflow = 1;
                        }
                    }
                }

                no = Nextnode[no];
            }
            else /* internal node */
            {
                if(no >= maxPart + maxNodes) /* pseudo-particle: export */
                {
                    if(mode == 0)
                    {
                        int task, nexp;
                        if(exportflag[task = DomainTask[no - (maxPart + maxNodes)]] != target) {
                            exportflag[task] = target;
                            exportnodecount[task] = NODELISTLENGTH;
                        }
                        if(exportnodecount[task] == NODELISTLENGTH) {
                            int exitFlag = 0;
#ifdef _OPENMP
#pragma omp critical(_nexport_treeray_)
#endif
                            {
                                if(Nexport >= bunchSize) { BufferFullFlag = 1; exitFlag = 1; }
                                else { nexp = Nexport; Nexport++; }
                            }
                            if(exitFlag) return -1;
                            exportnodecount[task] = 0;
                            exportindex[task] = nexp;
                            DataIndexTable[nexp].Task = task;
                            DataIndexTable[nexp].Index = target;
                            DataIndexTable[nexp].IndexGet = nexp;
                        }
                        DataNodeList[exportindex[task]].NodeList[exportnodecount[task]++] =
                            DomainNodeIndex[no - (maxPart + maxNodes)];
                        if(exportnodecount[task] < NODELISTLENGTH)
                            DataNodeList[exportindex[task]].NodeList[exportnodecount[task]] = -1;
                    }
                    no = Nextnode[no - maxNodes];
                    continue;
                }

                /* local internal node */
                nop = &Nodes[no];
                if(mode == 1) {
                    if(nop->u.d.bitflags & (1 << BITFLAG_TOPLEVEL)) { no = -1; continue; }
                }

                double nodemass = nop->u.d.mass;
                if(nodemass <= 0) { no = nop->u.d.sibling; continue; }

                if(!(nop->u.d.bitflags & (1 << BITFLAG_MULTIPLEPARTICLES))) {
                    if(nodemass) { no = nop->u.d.nextnode; continue; }
                }

                if(nop->Ti_current != ti_Current) {
#ifdef _OPENMP
#pragma omp critical(_nodedrift_treeray_)
#endif
                    { force_drift_node(no, ti_Current); }
                }

                dx = nop->u.d.s[0] - pos_x; dy = nop->u.d.s[1] - pos_y; dz = nop->u.d.s[2] - pos_z;
                GRAVITY_NEAREST_XYZ(dx, dy, dz, -1);
                r2 = dx*dx + dy*dy + dz*dz;

                /* Branch pruning */
                int node_has_lum = (nop->uv_luminosity > 0 || nop->lw_luminosity > 0
#ifdef GALSF_RESOLVEDISM_NUV_VARIABLE
                                    || nop->nuv_luminosity > 0
#endif
#ifdef GALSF_RESOLVEDISM_OPT_VARIABLE
                                    || nop->opt_luminosity > 0
#endif
                                   );
#ifdef TREE_RAY_IR
                node_has_lum = node_has_lum || (nop->ir_luminosity > 0);
#endif
                if(!node_has_lum && r2 > shielding_length2) { no = nop->u.d.sibling; continue; }

                /* Opening criterion */
                if(nop->len * nop->len > r2 * TREERAY_OPENING_ANGLE2) { no = nop->u.d.nextnode; continue; }

                /* Use this node */
                gasmass = nop->gasmass;
#ifdef TREE_RAD_H2
                h2mass = nop->h2mass; comass = nop->comass;
#endif
                uv_lum = nop->uv_luminosity; lw_lum = nop->lw_luminosity;
#ifdef GALSF_RESOLVEDISM_NUV_VARIABLE
                nuv_lum = nop->nuv_luminosity;
#endif
#ifdef GALSF_RESOLVEDISM_OPT_VARIABLE
                opt_lum = nop->opt_luminosity;
#endif
#ifdef TREE_RAY_IR
                ir_lum = nop->ir_luminosity;
#endif
#ifdef TREE_RAY_PI
                ion_lum = nop->ion_luminosity;
                neutral_h_mass_local = nop->neutral_h_mass;
#endif

                if(r2 > 0)
                {
                    int within_shield = (r2 < shielding_length2);

                    /* Accumulate columns directly */
                    if(gasmass > 0 && within_shield) {
                        long iheal;
                        double vec_hp[3] = {dx, dy, dz};
                        vec2pix_ring(NSIDE, vec_hp, &iheal);
                        double area = (4.0*M_PI / NPIX) * r2;
                        treecol_Projection[iheal] += gasmass / area;
#ifdef TREE_RAD_H2
                        treecol_ProjectionH2[iheal] += h2mass / area;
                        treecol_ProjectionCO[iheal] += comass / area;
#endif
                    }

                    /* Buffer interaction */
                    int has_gas = (gasmass > 0 && within_shield);
                    int has_lum = (uv_lum > 0 || lw_lum > 0 || nuv_lum > 0 || opt_lum > 0);
#ifdef TREE_RAY_IR
                    has_lum = has_lum || (ir_lum > 0);
#endif
#ifdef TREE_RAY_PI
                    has_lum = has_lum || (ion_lum > 0);
#endif
                    if(has_gas || has_lum) {
                        if(n_interactions < TREE_RAY_MAX_INTERACTIONS) {
                            long iheal;
                            double vec_hp[3] = {dx, dy, dz};
                            vec2pix_ring(NSIDE, vec_hp, &iheal);
                            ibuf[n_interactions].r2 = (float)r2;
                            ibuf[n_interactions].gasmass = has_gas ? (float)gasmass : 0.0f;
                            ibuf[n_interactions].h2mass = has_gas ? (float)h2mass : 0.0f;
                            ibuf[n_interactions].comass = has_gas ? (float)comass : 0.0f;
                            ibuf[n_interactions].uv_lum = (float)uv_lum;
                            ibuf[n_interactions].lw_lum = (float)lw_lum;
                            ibuf[n_interactions].nuv_lum = (float)nuv_lum;
                            ibuf[n_interactions].opt_lum = (float)opt_lum;
#ifdef TREE_RAY_IR
                            ibuf[n_interactions].ir_lum = (float)ir_lum;
#endif
#ifdef TREE_RAY_PI
                            ibuf[n_interactions].ion_lum = (float)ion_lum;
                            ibuf[n_interactions].neutral_h_mass = has_gas ? (float)neutral_h_mass_local : 0.0f;
#endif
                            ibuf[n_interactions].iheal = (int)iheal;
                            n_interactions++;
                        } else {
                            buffer_overflow = 1;
                        }
                    }
                }

                no = nop->u.d.sibling;
            }
        } /* inner while */

        if(mode == 1) {
            listindex++;
            if(listindex < NODELISTLENGTH) {
                no = TreeRayDataGet[target].NodeList[listindex];
                if(no >= 0) { nodesinlist++; no = Nodes[no].u.d.nextnode; }
            }
        }
    } /* outer while */

    /* ---- Pass 2: sort by (pixel, distance), sweep with attenuation ---- */
    double treeray_UV_flux[NPIX] = {0}, treeray_LW_flux[NPIX] = {0};
    double treeray_NUV_flux[NPIX] = {0}, treeray_OPT_flux[NPIX] = {0};
#ifdef TREE_RAY_IR
    double treeray_IR_flux[NPIX] = {0};
    double final_tau_ir[NPIX] = {0};
#endif
#ifdef TREE_RAY_PI
    double treeray_Ion_flux[NPIX] = {0};
    double final_tau_ion[NPIX] = {0};
#endif
    double final_tau_fuv[NPIX] = {0}, final_tau_nuv[NPIX] = {0}, final_tau_opt[NPIX] = {0};

    if(n_interactions > 0)
    {
        qsort(ibuf, n_interactions, sizeof(struct tree_ray_interaction), compare_pixel_then_r2);

        int idx = 0;
        for(int pix = 0; pix < NPIX; pix++)
        {
            /* Initialize τ: for remote evaluations, start with the local domain's τ */
            double tau_fuv = 0, tau_nuv = 0, tau_opt = 0;
#ifdef TREE_RAY_IR
            double tau_ir = 0;
#endif
#ifdef TREE_RAY_PI
            double tau_ion = 0;
#endif
            if(mode == 1) {
                tau_fuv = (double)TreeRayDataGet[target].tau_fuv[pix];
                tau_nuv = (double)TreeRayDataGet[target].tau_nuv[pix];
                tau_opt = (double)TreeRayDataGet[target].tau_opt[pix];
#ifdef TREE_RAY_IR
                tau_ir  = (double)TreeRayDataGet[target].tau_ir[pix];
#endif
#ifdef TREE_RAY_PI
                tau_ion = (double)TreeRayDataGet[target].tau_ion[pix];
#endif
            }

            while(idx < n_interactions && ibuf[idx].iheal == pix)
            {
                struct tree_ray_interaction *ia = &ibuf[idx];
                double r2_ia = (double)ia->r2;
                double area = (4.0*M_PI / NPIX) * r2_ia;

                /* Gas contribution: accumulate optical depth (all bands) */
                if(ia->gasmass > 0.0f && area > 0)
                {
                    double col_code = (double)ia->gasmass / area;
                    tau_fuv += sigma_fuv * col_code;
                    tau_nuv += sigma_nuv * col_code;
                    tau_opt += sigma_opt * col_code;
#ifdef TREE_RAY_IR
                    tau_ir  += sigma_ir  * col_code;
#endif
                }
#ifdef TREE_RAY_PI
                /* Neutral H contribution: accumulate ionizing optical depth */
                if(ia->neutral_h_mass > 0.0f && area > 0)
                {
                    double neutralh_col_code = (double)ia->neutral_h_mass / area;
                    tau_ion += sigma_ion * neutralh_col_code;
                }
#endif

                /* Stellar contribution: UV/LW/NUV/OPT attenuated by dust */
                if(ia->uv_lum > 0.0f || ia->lw_lum > 0.0f || ia->nuv_lum > 0.0f || ia->opt_lum > 0.0f)
                {
                    double inv_4pi_r2 = 1.0 / (4.0*M_PI * r2_ia);
                    double atten_fuv = exp(-tau_fuv);
                    double atten_nuv = exp(-tau_nuv);
                    double atten_opt = exp(-tau_opt);
                    double atten_lw = atten_fuv;
                    treeray_UV_flux[pix]  += (double)ia->uv_lum  * inv_4pi_r2 * atten_fuv;
                    treeray_LW_flux[pix]  += (double)ia->lw_lum  * inv_4pi_r2 * atten_lw;
                    treeray_NUV_flux[pix] += (double)ia->nuv_lum * inv_4pi_r2 * atten_nuv;
                    treeray_OPT_flux[pix] += (double)ia->opt_lum * inv_4pi_r2 * atten_opt;
                }

#ifdef TREE_RAY_IR
                /* Dust IR emission: gas cells emit IR, attenuated by IR dust opacity */
                if(ia->ir_lum > 0.0f)
                {
                    double inv_4pi_r2 = 1.0 / (4.0*M_PI * r2_ia);
                    double atten_ir = exp(-tau_ir);
                    treeray_IR_flux[pix] += (double)ia->ir_lum * inv_4pi_r2 * atten_ir;
                }
#endif
#ifdef TREE_RAY_PI
                /* Ionizing radiation: stars emit, attenuated by neutral H */
                if(ia->ion_lum > 0.0f)
                {
                    double inv_4pi_r2 = 1.0 / (4.0*M_PI * r2_ia);
                    double atten_ion = exp(-tau_ion);
                    treeray_Ion_flux[pix] += (double)ia->ion_lum * inv_4pi_r2 * atten_ion;
                }
#endif

                idx++;
            }

            /* Store final τ for this pixel */
            final_tau_fuv[pix] = tau_fuv;
            final_tau_nuv[pix] = tau_nuv;
            final_tau_opt[pix] = tau_opt;
#ifdef TREE_RAY_IR
            final_tau_ir[pix] = tau_ir;
#endif
#ifdef TREE_RAY_PI
            final_tau_ion[pix] = tau_ion;
#endif
        }
    }

    if(buffer_overflow) {
        printf("TREERAY WARNING [task %d]: buffer overflow for target %d (%d interactions, max %d)\n",
               ThisTask, target, n_interactions, TREE_RAY_MAX_INTERACTIONS);
    }

    /* ---- Store results ---- */
    if(mode == 0)
    {
        if(P[target].Type == 0) {
            int kp;
            for(kp = 0; kp < NPIX; kp++) CellP[target].Projection[kp] = treecol_Projection[kp];
#ifdef TREE_RAD_H2
            for(kp = 0; kp < NPIX; kp++) { CellP[target].ProjectionH2[kp] = treecol_ProjectionH2[kp]; CellP[target].ProjectionCO[kp] = treecol_ProjectionCO[kp]; }
#endif
            for(kp = 0; kp < NPIX; kp++) {
                CellP[target].UV_flux[kp] = treeray_UV_flux[kp];
                CellP[target].LW_flux[kp] = treeray_LW_flux[kp];
#ifdef GALSF_RESOLVEDISM_NUV_VARIABLE
                CellP[target].NUV_flux[kp] = treeray_NUV_flux[kp];
#endif
#ifdef GALSF_RESOLVEDISM_OPT_VARIABLE
                CellP[target].OPT_flux[kp] = treeray_OPT_flux[kp];
#endif
            }
#ifdef TREE_RAY_IR
            for(kp = 0; kp < NPIX; kp++) CellP[target].IR_flux[kp] = treeray_IR_flux[kp];
#endif
#ifdef TREE_RAY_PI
            for(kp = 0; kp < NPIX; kp++) CellP[target].Ion_flux[kp] = treeray_Ion_flux[kp];
#endif
            /* Store local τ for MPI export (remote domains start their sweep here) */
            for(kp = 0; kp < NPIX; kp++) { local_tau_fuv[target][kp] = final_tau_fuv[kp]; local_tau_nuv[target][kp] = final_tau_nuv[kp]; local_tau_opt[target][kp] = final_tau_opt[kp]; }
#ifdef TREE_RAY_IR
            for(kp = 0; kp < NPIX; kp++) local_tau_ir[target][kp] = final_tau_ir[kp];
#endif
#ifdef TREE_RAY_PI
            for(kp = 0; kp < NPIX; kp++) local_tau_ion[target][kp] = final_tau_ion[kp];
#endif
        }
    }
    else
    {
        int kp;
        for(kp = 0; kp < NPIX; kp++) TreeRayDataResult[target].Projection[kp] = treecol_Projection[kp];
#ifdef TREE_RAD_H2
        for(kp = 0; kp < NPIX; kp++) { TreeRayDataResult[target].ProjectionH2[kp] = treecol_ProjectionH2[kp]; TreeRayDataResult[target].ProjectionCO[kp] = treecol_ProjectionCO[kp]; }
#endif
        for(kp = 0; kp < NPIX; kp++) { TreeRayDataResult[target].UV_flux[kp] = treeray_UV_flux[kp]; TreeRayDataResult[target].LW_flux[kp] = treeray_LW_flux[kp]; TreeRayDataResult[target].NUV_flux[kp] = treeray_NUV_flux[kp]; TreeRayDataResult[target].OPT_flux[kp] = treeray_OPT_flux[kp]; }
#ifdef TREE_RAY_IR
        for(kp = 0; kp < NPIX; kp++) TreeRayDataResult[target].IR_flux[kp] = treeray_IR_flux[kp];
#endif
#ifdef TREE_RAY_PI
        for(kp = 0; kp < NPIX; kp++) TreeRayDataResult[target].Ion_flux[kp] = treeray_Ion_flux[kp];
#endif
    }

    return 0;
}

/* ============================================================
 *  Primary/secondary loop functions (threaded)
 *  Each thread gets its own interaction buffer.
 * ============================================================ */
static struct tree_ray_interaction **thread_ibuf = NULL;

static void *treeray_primary_loop(void *p)
{
    int i, j, dummy, *exportflag, *exportnodecount, *exportindex, threadid = *(int *)p;
    exportflag = Exportflag + threadid * NTask;
    exportnodecount = Exportnodecount + threadid * NTask;
    exportindex = Exportindex + threadid * NTask;
    for(j = 0; j < NTask; j++) exportflag[j] = -1;

    struct tree_ray_interaction *ibuf = thread_ibuf[threadid];

    while(1)
    {
#ifdef _OPENMP
#pragma omp critical(_nexport_treeray_primary_)
#endif
        { i = NextParticle; NextParticle++; }
        if(i >= NumPart) break;

        if(P[i].Type != 0) { ProcessedFlag[i] = 1; continue; }
        if(!TimeBinActive[P[i].TimeBin]) { ProcessedFlag[i] = 1; continue; }

        if(tree_ray_evaluate(i, 0, exportflag, exportnodecount, exportindex, ibuf) < 0) break;
        ProcessedFlag[i] = 1;
    }
    return NULL;
}

static void *treeray_secondary_loop(void *p)
{
    int j, dummy, threadid = *(int *)p;
    struct tree_ray_interaction *ibuf = thread_ibuf[threadid];

    while(1)
    {
#ifdef _OPENMP
#pragma omp critical(_nimport_treeray_secondary_)
#endif
        { j = NextJ; NextJ++; }
        if(j >= Nimport) break;
        tree_ray_evaluate(j, 1, &dummy, &dummy, &dummy, ibuf);
    }
    return NULL;
}

/* ============================================================
 *  Driver: MPI export/import loop
 * ============================================================ */
void tree_ray_tree(void)
{
    int i, j, k, ndone, ndone_flag, ngrp, recvTask, place, nexp;
    double tstart, tend;

    if(ThisTask == 0) printf("TREERAY: Starting attenuated flux + column density walk...\n");
    tstart = my_second();

#ifdef TREE_RAY_IR
    /* Compute IR luminosity for each gas cell from dust temperature (operator-split:
     * uses T_dust from the previous cooling step). L_IR = 4.68e-31 * Tdust^6 * nH * DGR * Volume.
     * The 4.68e-31 * Tdust^6 is the optically thin dust emissivity [erg/s/cm^3/nH/DGR]
     * from cool_util.F (Ossenkopf & Henning 1994 opacities). */
    {
        double unit_vol = UNIT_LENGTH_IN_CGS * UNIT_LENGTH_IN_CGS * UNIT_LENGTH_IN_CGS
                        * All.cf_atime * All.cf_atime * All.cf_atime;  /* code volume → cm^3 */
        for(i = 0; i < NumPart; i++) {
            if(P[i].Type == 0) {
                double nH_cgs = HYDROGEN_MASSFRAC * CellP[i].Density * All.cf_a3inv
                              * UNIT_DENSITY_IN_CGS / PROTONMASS_CGS;
                double vol_cgs = P[i].Mass / CellP[i].Density * unit_vol;
                double Td = CellP[i].DustTemp;
                if(Td < 2.73) Td = 2.73;  /* CMB floor */
                double DGR = All.DGRnormalized;
                P[i].IR_luminosity = 4.68e-31 * Td*Td*Td*Td*Td*Td * nH_cgs * DGR * vol_cgs;
                if(P[i].IR_luminosity < 0) P[i].IR_luminosity = 0;
            }
        }
    }
#endif

#ifdef TREE_RAY_PI
    /* Compute ionizing luminosity for each star particle from stellar tables.
     * Same logic as rt_get_lum_band_resolvedism() in rt_utilities.cc:305-346. */
    {
        for(i = 0; i < NumPart; i++) {
            if(P[i].Type == 4) {
                P[i].Ion_luminosity = 0;
                double Mstar = 0;
#ifdef GALSF_RESOLVEDISM_SAMPLE_IMF
                if(P[i].sampled) Mstar = P[i].MstarSampleIMF[0];
#endif
                if(Mstar <= 0) continue;
                double logM = log10(Mstar);
                double logZ = log10(DMAX(P[i].BirthMetallicity, 1e-10));
                double star_age_yr = evaluate_stellar_age_Gyr(i) * 1.0e9;
                if(star_age_yr <= 0) continue;
                double lifetime_yr = stellar_lifetime(logM, logZ);
                if(star_age_yr > lifetime_yr) continue; /* dead star */
                double table_age = star_age_yr - stellar_t_PMS(logM, logZ);
                if(table_age <= 0) continue; /* pre-main-sequence */
                double log_age = log10(DMAX(table_age, 100.0));
                P[i].Ion_luminosity = pow(10.0, stellar_log_L_ion_tot(logM, logZ, log_age)); /* [erg/s] */
            }
        }
    }
#endif

    /* Allocate per-thread interaction buffers */
    int nthreads_alloc = 1;
#ifdef _OPENMP
    nthreads_alloc = omp_get_max_threads();
#endif
    thread_ibuf = (struct tree_ray_interaction **)mymalloc("TrIbufPtrs", nthreads_alloc * sizeof(struct tree_ray_interaction *));
    for(i = 0; i < nthreads_alloc; i++)
        thread_ibuf[i] = (struct tree_ray_interaction *)mymalloc("TrIbuf", TREE_RAY_MAX_INTERACTIONS * sizeof(struct tree_ray_interaction));

    /* Allocate per-particle τ storage for MPI export */
    local_tau_fuv = (double (*)[NPIX])mymalloc("TrTauFUV", All.MaxPart * NPIX * sizeof(double));
    local_tau_nuv = (double (*)[NPIX])mymalloc("TrTauNUV", All.MaxPart * NPIX * sizeof(double));
    local_tau_opt = (double (*)[NPIX])mymalloc("TrTauOPT", All.MaxPart * NPIX * sizeof(double));
#ifdef TREE_RAY_IR
    local_tau_ir  = (double (*)[NPIX])mymalloc("TrTauIR",  All.MaxPart * NPIX * sizeof(double));
#endif
#ifdef TREE_RAY_PI
    local_tau_ion = (double (*)[NPIX])mymalloc("TrTauION", All.MaxPart * NPIX * sizeof(double));
#endif
    memset(local_tau_fuv, 0, All.MaxPart * NPIX * sizeof(double));
    memset(local_tau_nuv, 0, All.MaxPart * NPIX * sizeof(double));
    memset(local_tau_opt, 0, All.MaxPart * NPIX * sizeof(double));
#ifdef TREE_RAY_IR
    memset(local_tau_ir,  0, All.MaxPart * NPIX * sizeof(double));
#endif
#ifdef TREE_RAY_PI
    memset(local_tau_ion, 0, All.MaxPart * NPIX * sizeof(double));
#endif

    memset(ProcessedFlag, 0, All.MaxPart * sizeof(unsigned char));

    size_t MyBufferSize = All.BufferSize;
    All.BunchSize = (long)((MyBufferSize * 1024 * 1024) /
        (sizeof(struct data_index) + sizeof(struct data_nodelist) +
         sizeof(struct treeray_data_in) + sizeof(struct treeray_data_out) +
         sizemax(sizeof(struct treeray_data_in), sizeof(struct treeray_data_out))));
    DataIndexTable = (struct data_index *)mymalloc("TrDataIdx", All.BunchSize * sizeof(struct data_index));
    DataNodeList = (struct data_nodelist *)mymalloc("TrDataNL", All.BunchSize * sizeof(struct data_nodelist));

    NextParticle = 0;
    int iter = 0;

    do {
        BufferFullFlag = 0;
        Nexport = 0;
        for(j = 0; j < NTask; j++) { Send_count[j] = 0; Exportflag[j] = -1; }

        int currentParticle = NextParticle;
#ifdef _OPENMP
        int nthreads = omp_get_max_threads();
        int threadid[nthreads];
        for(i = 0; i < nthreads; i++) threadid[i] = i;
#pragma omp parallel for schedule(dynamic)
        for(i = 0; i < nthreads; i++)
            treeray_primary_loop(&threadid[i]);
#else
        int threadid_single = 0;
        treeray_primary_loop(&threadid_single);
#endif

        if(BufferFullFlag) {
            int last_index = NextParticle;
            NextParticle = currentParticle;
            while(NextParticle < last_index) {
                if(ProcessedFlag[NextParticle] != 1) break;
                NextParticle++;
            }
        }

        qsort(DataIndexTable, Nexport, sizeof(struct data_index), data_index_compare);
        for(j = 0; j < NTask; j++) Send_count[j] = 0;
        for(j = 0; j < Nexport; j++) Send_count[DataIndexTable[j].Task]++;
        MPI_Alltoall(Send_count, 1, MPI_INT, Recv_count, 1, MPI_INT, MPI_COMM_WORLD);

        Nimport = 0;
        for(j = 0; j < NTask; j++) {
            Send_offset[j] = (j == 0) ? 0 : Send_offset[j-1] + Send_count[j-1];
            Recv_offset[j] = (j == 0) ? 0 : Recv_offset[j-1] + Recv_count[j-1];
            Nimport += Recv_count[j];
        }

        TreeRayDataIn = (struct treeray_data_in *)mymalloc("TrDataIn", Nexport * sizeof(struct treeray_data_in));
        for(j = 0; j < Nexport; j++) {
            place = DataIndexTable[j].Index;
            for(k = 0; k < 3; k++) TreeRayDataIn[j].Pos[k] = P[place].Pos[k];
            memcpy(TreeRayDataIn[j].NodeList, DataNodeList[DataIndexTable[j].IndexGet].NodeList, NODELISTLENGTH * sizeof(int));
            /* Pack local τ so remote domain can start its sweep with correct initial optical depth */
            for(k = 0; k < NPIX; k++) {
                TreeRayDataIn[j].tau_fuv[k] = (MyFloat)local_tau_fuv[place][k];
                TreeRayDataIn[j].tau_nuv[k] = (MyFloat)local_tau_nuv[place][k];
                TreeRayDataIn[j].tau_opt[k] = (MyFloat)local_tau_opt[place][k];
#ifdef TREE_RAY_IR
                TreeRayDataIn[j].tau_ir[k]  = (MyFloat)local_tau_ir[place][k];
#endif
#ifdef TREE_RAY_PI
                TreeRayDataIn[j].tau_ion[k] = (MyFloat)local_tau_ion[place][k];
#endif
            }
        }

        TreeRayDataGet = (struct treeray_data_in *)mymalloc("TrDataGet", Nimport * sizeof(struct treeray_data_in));
        for(ngrp = 1; ngrp < (1 << PTask); ngrp++) {
            recvTask = ThisTask ^ ngrp;
            if(recvTask < NTask) {
                if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0) {
                    MPI_Sendrecv(&TreeRayDataIn[Send_offset[recvTask]], Send_count[recvTask] * sizeof(struct treeray_data_in), MPI_BYTE,
                                 recvTask, TAG_TREERAY_A,
                                 &TreeRayDataGet[Recv_offset[recvTask]], Recv_count[recvTask] * sizeof(struct treeray_data_in), MPI_BYTE,
                                 recvTask, TAG_TREERAY_A, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
            }
        }

        TreeRayDataResult = (struct treeray_data_out *)mymalloc("TrDataRes", Nimport * sizeof(struct treeray_data_out));
        memset(TreeRayDataResult, 0, Nimport * sizeof(struct treeray_data_out));

        NextJ = 0;
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
        for(i = 0; i < nthreads; i++)
            treeray_secondary_loop(&threadid[i]);
#else
        treeray_secondary_loop(&threadid_single);
#endif

        TreeRayDataOut = (struct treeray_data_out *)mymalloc("TrDataOut", Nexport * sizeof(struct treeray_data_out));
        for(ngrp = 1; ngrp < (1 << PTask); ngrp++) {
            recvTask = ThisTask ^ ngrp;
            if(recvTask < NTask) {
                if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0) {
                    MPI_Sendrecv(&TreeRayDataResult[Recv_offset[recvTask]], Recv_count[recvTask] * sizeof(struct treeray_data_out), MPI_BYTE,
                                 recvTask, TAG_TREERAY_B,
                                 &TreeRayDataOut[Send_offset[recvTask]], Send_count[recvTask] * sizeof(struct treeray_data_out), MPI_BYTE,
                                 recvTask, TAG_TREERAY_B, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
            }
        }

        /* Add imported results */
        for(j = 0; j < Nexport; j++) {
            place = DataIndexTable[j].Index;
            if(P[place].Type == 0) {
                int kp;
                for(kp = 0; kp < NPIX; kp++) CellP[place].Projection[kp] += TreeRayDataOut[j].Projection[kp];
#ifdef TREE_RAD_H2
                for(kp = 0; kp < NPIX; kp++) { CellP[place].ProjectionH2[kp] += TreeRayDataOut[j].ProjectionH2[kp]; CellP[place].ProjectionCO[kp] += TreeRayDataOut[j].ProjectionCO[kp]; }
#endif
                for(kp = 0; kp < NPIX; kp++) {
                    CellP[place].UV_flux[kp] += TreeRayDataOut[j].UV_flux[kp];
                    CellP[place].LW_flux[kp] += TreeRayDataOut[j].LW_flux[kp];
#ifdef GALSF_RESOLVEDISM_NUV_VARIABLE
                    CellP[place].NUV_flux[kp] += TreeRayDataOut[j].NUV_flux[kp];
#endif
#ifdef GALSF_RESOLVEDISM_OPT_VARIABLE
                    CellP[place].OPT_flux[kp] += TreeRayDataOut[j].OPT_flux[kp];
#endif
                }
#ifdef TREE_RAY_IR
                for(kp = 0; kp < NPIX; kp++) CellP[place].IR_flux[kp] += TreeRayDataOut[j].IR_flux[kp];
#endif
#ifdef TREE_RAY_PI
                for(kp = 0; kp < NPIX; kp++) CellP[place].Ion_flux[kp] += TreeRayDataOut[j].Ion_flux[kp];
#endif
            }
        }

        myfree(TreeRayDataOut);
        myfree(TreeRayDataResult);
        myfree(TreeRayDataGet);
        myfree(TreeRayDataIn);

        if(NextParticle >= NumPart) ndone_flag = 1; else ndone_flag = 0;
        MPI_Allreduce(&ndone_flag, &ndone, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        iter++;
    }
    while(ndone < NTask);

    myfree(DataNodeList);
    myfree(DataIndexTable);

    /* Free per-particle τ storage (LIFO: allocated before DataIndexTable) */
#ifdef TREE_RAY_PI
    myfree(local_tau_ion); local_tau_ion = NULL;
#endif
#ifdef TREE_RAY_IR
    myfree(local_tau_ir);  local_tau_ir = NULL;
#endif
    myfree(local_tau_opt); local_tau_opt = NULL;
    myfree(local_tau_nuv); local_tau_nuv = NULL;
    myfree(local_tau_fuv); local_tau_fuv = NULL;

    /* Debug output */
    {
        long n_nonzero_col = 0, n_nonzero_uv = 0, n_gas_local = 0;
        double col_max_local = 0, uv_max_local = 0, lw_max_local = 0;
        for(i = 0; i < NumPart; i++) {
            if(P[i].Type == 0) {
                n_gas_local++;
                int kp; double col_sum = 0, uv_sum = 0, lw_sum = 0;
                for(kp = 0; kp < NPIX; kp++) {
                    col_sum += CellP[i].Projection[kp];
                    uv_sum += CellP[i].UV_flux[kp];
                    lw_sum += CellP[i].LW_flux[kp];
                }
                if(col_sum > 0) n_nonzero_col++;
                if(uv_sum > 0) n_nonzero_uv++;
                if(col_sum > col_max_local) col_max_local = col_sum;
                if(uv_sum > uv_max_local) uv_max_local = uv_sum;
                if(lw_sum > lw_max_local) lw_max_local = lw_sum;
            }
        }
        long n_col_tot = 0, n_uv_tot = 0, n_gas_tot = 0;
        double col_max_tot = 0, uv_max_tot = 0, lw_max_tot = 0;
        MPI_Reduce(&n_nonzero_col, &n_col_tot, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&n_nonzero_uv, &n_uv_tot, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&n_gas_local, &n_gas_tot, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&col_max_local, &col_max_tot, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&uv_max_local, &uv_max_tot, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&lw_max_local, &lw_max_tot, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        if(ThisTask == 0) {
            printf("TREERAY: col nonzero=%ld/%ld (%.1f%%), max=%.4e\n",
                   n_col_tot, n_gas_tot, 100.0*n_col_tot/DMAX(n_gas_tot,1), col_max_tot);
            printf("TREERAY: UV(atten) nonzero=%ld, max=%.4e | LW(atten) max=%.4e\n",
                   n_uv_tot, uv_max_tot, lw_max_tot);
        }
    }

    /* ---- Post-hoc photon conservation (LEBRON-style) ----
     * For each band: if total absorbed power > total emitted power,
     * rescale all fluxes down to enforce conservation. Prevents photon
     * creation from tree monopole / pixel discretization errors. */
    {
        double L_emit_uv_local = 0, L_emit_lw_local = 0, L_emit_nuv_local = 0, L_emit_opt_local = 0;
        double L_abs_uv_local = 0, L_abs_lw_local = 0, L_abs_nuv_local = 0, L_abs_opt_local = 0;

        /* Sum emitted luminosity from local stars */
        for(i = 0; i < NumPart; i++) {
            if(P[i].Type == 4 || P[i].Type == 5) {
                L_emit_uv_local  += P[i].UV_luminosity;
                L_emit_lw_local  += P[i].LW_luminosity;
#ifdef GALSF_RESOLVEDISM_NUV_VARIABLE
                L_emit_nuv_local += P[i].NUV_luminosity;
#endif
#ifdef GALSF_RESOLVEDISM_OPT_VARIABLE
                L_emit_opt_local += P[i].OPT_luminosity;
#endif
            }
        }

        /* Sum absorbed power at each gas cell: P_abs = κ × (4πJ) × V
         * where 4πJ = Σ_pix F_pix, κ = σ_dust × n_H × DGR, V = M/ρ
         * Combined: P_abs = σ_dust_cgs × DGR × (Σ_pix F_pix) × M × HYDROGEN_MASSFRAC / m_H
         *           (using n_H × V = M × X_H / m_H, with appropriate unit conversions) */
        double DGR = All.DGRnormalized;
        for(i = 0; i < NumPart; i++) {
            if(P[i].Type == 0) {
                double nH_V = HYDROGEN_MASSFRAC * P[i].Mass * UNIT_MASS_IN_CGS / PROTONMASS_CGS; /* n_H × V in CGS */
                int kp; double fuv = 0, flw = 0, fnuv = 0, fopt = 0;
                for(kp = 0; kp < NPIX; kp++) {
                    fuv  += CellP[i].UV_flux[kp];
                    flw  += CellP[i].LW_flux[kp];
#ifdef GALSF_RESOLVEDISM_NUV_VARIABLE
                    fnuv += CellP[i].NUV_flux[kp];
#endif
#ifdef GALSF_RESOLVEDISM_OPT_VARIABLE
                    fopt += CellP[i].OPT_flux[kp];
#endif
                }
                /* Flux is in code units [erg/s / code_length²]; convert to CGS */
                double fac_flux = All.cf_a2inv / (UNIT_LENGTH_IN_CGS * UNIT_LENGTH_IN_CGS);
                L_abs_uv_local  += SIGMA_DUST_FUV * DGR * nH_V * fuv  * fac_flux;
                L_abs_lw_local  += SIGMA_DUST_FUV * DGR * nH_V * flw  * fac_flux;  /* LW uses same dust σ as FUV */
                L_abs_nuv_local += SIGMA_DUST_NUV * DGR * nH_V * fnuv * fac_flux;
                L_abs_opt_local += SIGMA_DUST_OPT * DGR * nH_V * fopt * fac_flux;
            }
        }

        /* MPI-reduce totals */
        double L_emit_uv, L_emit_lw, L_emit_nuv, L_emit_opt;
        double L_abs_uv, L_abs_lw, L_abs_nuv, L_abs_opt;
        MPI_Allreduce(&L_emit_uv_local, &L_emit_uv, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&L_emit_lw_local, &L_emit_lw, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&L_emit_nuv_local, &L_emit_nuv, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&L_emit_opt_local, &L_emit_opt, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&L_abs_uv_local, &L_abs_uv, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&L_abs_lw_local, &L_abs_lw, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&L_abs_nuv_local, &L_abs_nuv, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&L_abs_opt_local, &L_abs_opt, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

        /* Compute correction factors (only renormalize DOWN, never up) */
        double f_uv  = (L_abs_uv  > L_emit_uv  && L_abs_uv  > 0) ? L_emit_uv  / L_abs_uv  : 1.0;
        double f_lw  = (L_abs_lw  > L_emit_lw  && L_abs_lw  > 0) ? L_emit_lw  / L_abs_lw  : 1.0;
        double f_nuv = (L_abs_nuv > L_emit_nuv && L_abs_nuv > 0) ? L_emit_nuv / L_abs_nuv : 1.0;
        double f_opt = (L_abs_opt > L_emit_opt && L_abs_opt > 0) ? L_emit_opt / L_abs_opt : 1.0;

        /* Apply correction to all gas cells */
        if(f_uv < 1.0 || f_lw < 1.0 || f_nuv < 1.0 || f_opt < 1.0) {
            for(i = 0; i < NumPart; i++) {
                if(P[i].Type == 0) {
                    int kp;
                    for(kp = 0; kp < NPIX; kp++) {
                        CellP[i].UV_flux[kp]  *= f_uv;
                        CellP[i].LW_flux[kp]  *= f_lw;
#ifdef GALSF_RESOLVEDISM_NUV_VARIABLE
                        CellP[i].NUV_flux[kp] *= f_nuv;
#endif
#ifdef GALSF_RESOLVEDISM_OPT_VARIABLE
                        CellP[i].OPT_flux[kp] *= f_opt;
#endif
                    }
                }
            }
            if(ThisTask == 0) {
                printf("TREERAY CONSERV: f_uv=%.4f f_lw=%.4f f_nuv=%.4f f_opt=%.4f (L_abs/L_emit: %.3e/%.3e %.3e/%.3e %.3e/%.3e %.3e/%.3e)\n",
                       f_uv, f_lw, f_nuv, f_opt, L_abs_uv, L_emit_uv, L_abs_lw, L_emit_lw, L_abs_nuv, L_emit_nuv, L_abs_opt, L_emit_opt);
            }
        }

#ifdef TREE_RAY_PI
        /* Same for ionizing band */
        double L_emit_ion_local = 0, L_abs_ion_local = 0;
        for(i = 0; i < NumPart; i++) {
            if(P[i].Type == 4 || P[i].Type == 5) L_emit_ion_local += P[i].Ion_luminosity;
        }
        for(i = 0; i < NumPart; i++) {
            if(P[i].Type == 0) {
                double nHI_V = (1.0 - CellP[i].TracAbund[IHP]) * HYDROGEN_MASSFRAC * P[i].Mass * UNIT_MASS_IN_CGS / PROTONMASS_CGS;
                int kp; double fion = 0;
                for(kp = 0; kp < NPIX; kp++) fion += CellP[i].Ion_flux[kp];
                double fac_flux = All.cf_a2inv / (UNIT_LENGTH_IN_CGS * UNIT_LENGTH_IN_CGS);
                L_abs_ion_local += SIGMA_ION_HI * nHI_V * fion * fac_flux;
            }
        }
        double L_emit_ion, L_abs_ion;
        MPI_Allreduce(&L_emit_ion_local, &L_emit_ion, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&L_abs_ion_local, &L_abs_ion, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        double f_ion = (L_abs_ion > L_emit_ion && L_abs_ion > 0) ? L_emit_ion / L_abs_ion : 1.0;
        if(f_ion < 1.0) {
            for(i = 0; i < NumPart; i++) {
                if(P[i].Type == 0) { int kp; for(kp = 0; kp < NPIX; kp++) CellP[i].Ion_flux[kp] *= f_ion; }
            }
            if(ThisTask == 0) printf("TREERAY CONSERV: f_ion=%.4f (L_abs/L_emit: %.3e/%.3e)\n", f_ion, L_abs_ion, L_emit_ion);
        }
#endif
    }

    /* Free per-thread buffers (LIFO order) */
    for(i = nthreads_alloc - 1; i >= 0; i--)
        myfree(thread_ibuf[i]);
    myfree(thread_ibuf);
    thread_ibuf = NULL;

    tend = my_second();
    if(ThisTask == 0) printf("TREERAY: Done (%d iterations, %.3g sec)\n", iter, timediff(tstart, tend));
}

#endif /* TREE_RAY */
