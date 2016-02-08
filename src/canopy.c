/* ============================================================================
* Calculates all within canopy C & water fluxes.
*
*
* NOTES:
*
*
* AUTHOR:
*   Martin De Kauwe
*
* DATE:
*   17.02.2015
*
* =========================================================================== */
#include "canopy.h"
#include "water_balance.h"

void canopy(control *c, fluxes *f, met *m, params *p, state *s) {
    /*
        Two-leaf canopy module consists of two parts:
        (1) a radiation submodel to calculate PAR, NIR and thermal radiation
        (2) a coupled model of stomatal conductance, photosynthesis and
            partitioning between absorbed net radiation into sensible and
            latent heat.

        The coupled model has two leaves: sunlit & shaded under the assumption
        that the sunlit and shaded leaf is representative of all respective
        sunlit or shaded leaves within a canopy. Clearly for dense canopies this
        assumption will not hold due, but as fluxes from canopy elements at the
        base of the canopy are small, it is likely to be an acceptable error.

        References
        ----------
        * Wang & Leuning (1998) Agricultural & Forest Meterorology, 91, 89-111.
        * Dai et al. (2004) Journal of Climate, 17, 2281-2299.
        * De Pury & Farquhar (1997) PCE, 20, 537-557.

    */

    double Cs, dleaf, tleaf, new_tleaf, trans_hlf_hr, leafn, fc, ncontent[2],
           cos_zenith, elevation, anleaf[2], gsc[2], apar[2], leaf_trans[2],
           direct_apar, diffuse_apar, diffuse_frac, rnet=0.0, total_rnet,
           press, vpd, par, tair, wind, Ca, sunlit_lai, acanopy, trans_canopy,
           shaded_lai, gsc_canopy, total_apar;
    int    hod, iter = 0, itermax = 100, ileaf;



    zero_carbon_day_fluxes(f);
    zero_water_day_fluxes(f);

    for (hod = 0; hod < c->num_hlf_hrs; hod++) {
        calculate_zenith_angle(p, m->doy[c->hrly_idx], hod, &cos_zenith,
                               &elevation);

        /* calculates diffuse frac from half-hourly incident radiation */
        par = m->par[c->hrly_idx];
        diffuse_frac = get_diffuse_frac(m->doy[c->hrly_idx], cos_zenith, par);

        /* Is the sun up? */
        if (elevation > 0.0 && par > 50.0) {

            /* sunlit, shaded loop */
            acanopy = 0.0;
            total_apar = 0.0;
            trans_canopy = 0.0;
            gsc_canopy = 0.0;
            calculate_absorbed_radiation(p, s, par, diffuse_frac, elevation,
                                         cos_zenith, &(apar[0]), &sunlit_lai,
                                         &shaded_lai);

            /* THIS BIT IS WRONG< BUT IVE TURNED OFF this feedback */


            if (s->lai > 0.0) {
                /* average leaf nitrogen content (g N m-2 leaf) */
                leafn = (s->shootnc * p->cfracts / p->sla * KG_AS_G);

                /* total nitrogen content of the canopy */
                ncontent[SUNLIT] = leafn * sunlit_lai;
                ncontent[SHADED] = leafn * shaded_lai;

            } else {
                ncontent[SUNLIT] = 0.0;
                ncontent[SHADED] = 0.0;
            }


            for (ileaf = 0; ileaf <= 1; ileaf++) {

                /*
                    initialise values of leaf temp, leaf surface CO2 and VPD at
                    the leaf surface using air space values
                */
                tleaf = m->tair[c->hrly_idx];
                dleaf = m->vpd[c->hrly_idx] * KPA_2_PA;
                Cs = m->co2[c->hrly_idx];

                while (TRUE) { /* Stability loop */


                    if (c->ps_pathway == C3) {

                        photosynthesis_C3(c, p, s, ncontent[ileaf], tleaf,
                                          apar[ileaf], Cs, dleaf, &gsc[ileaf],
                                          &anleaf[ileaf]);
                    } else {
                        /* Nothing implemented */
                        fprintf(stderr, "C4 photosynthesis not implemented\n");
                        exit(EXIT_FAILURE);
                    }

                    /*printf("%lf %lf %lf %lf %d\n", hod/2., par, apar[ileaf], anleaf[ileaf], ileaf);*/

                    if (anleaf[ileaf] > 0.0) {
                        /* Calculate new Cs, dleaf, Tleaf */
                        solve_leaf_energy_balance(c, f, m, p, s, tleaf,
                                                  gsc[ileaf], anleaf[ileaf],
                                                  apar[ileaf], &Cs, &dleaf,
                                                  &new_tleaf,
                                                  &leaf_trans[ileaf]);
                    } else {
                        leaf_trans[ileaf] = 0.0;
                        break;
                    }


                    if (iter >= itermax) {
                        fprintf(stderr, "No convergence in canopy loop\n");
                        exit(EXIT_FAILURE);
                    } else if (fabs(tleaf - new_tleaf) < 0.02) {
                        break;  /* stopping criteria */
                    } else {    /* Update temperature & do another iteration */
                        tleaf = new_tleaf;
                        iter++;
                    }

                } /* end of leaf temperature stability loop */


                /* This is wrong as rnet isn't being changed, however fix
                   this later. I'm not sure the canopy rnet is what should
                   be passed to the soil evap calculation anyway */
                total_rnet += rnet;
                total_apar += apar[ileaf];

            } /* end of sunlit/shaded leaf loop */

            /* Scale leaf fluxes to the canopy */
            acanopy = sunlit_lai * anleaf[SUNLIT];
            acanopy += shaded_lai * anleaf[SHADED];
            gsc_canopy = sunlit_lai * gsc[SUNLIT];
            gsc_canopy += shaded_lai * gsc[SHADED];
            trans_canopy = sunlit_lai * leaf_trans[SUNLIT];
            trans_canopy += shaded_lai * leaf_trans[SHADED];



            update_daily_carbon_fluxes(f, p, acanopy, total_apar);
            calculate_sub_daily_water_balance(c, f, m, p, s, total_rnet,
                                              trans_canopy);

        } else {
            /* set time slot photosynthesis/respiration to be zero, but we
               still need to calc the full water balance, i.e. soil evap */
            acanopy = 0.0;
            gsc_canopy = 0.0;
            trans_canopy = 0.0;
            total_apar = apar[SUNLIT] + apar[SHADED];
            update_daily_carbon_fluxes(f, p, acanopy, total_apar);
            calculate_sub_daily_water_balance(c, f, m, p, s, total_rnet,
                                              trans_canopy);
        }

        /*printf("* %lf %lf: %lf %lf %lf  %lf\n", hod/2., elevation, par, acanopy, apar[SUNLIT], apar[SHADED]);*/
        c->hrly_idx++;
    }


    return;

}



void solve_leaf_energy_balance(control *c, fluxes *f, met *m, params *p,
                               state *s, double tleaf, double gsc,
                               double anleaf, double apar, double *Cs,
                               double *dleaf, double *new_tleaf, double *et) {
    /*
        Coupled model wrapper to solve photosynthesis, stomtal conductance
        and radiation paritioning.

    */
    double LE; /* latent heat (W m-2) */
    double Rspecifc_dry_air = 287.058; /* J kg-1 K-1 */
    double lambda, arg1, arg2, slope, gradn, gbhu, gbhf, gbh, gh, gbv, gsv, gv;
    double gbc, gamma, epsilon, omega, Tdiff, sensible_heat, rnet, ea, ema, Tk;
    double emissivity_atm, sw_rad;

    /* unpack the met data and get the units right */
    double press = m->press[c->hrly_idx] * KPA_2_PA;
    double vpd = m->vpd[c->hrly_idx] * KPA_2_PA;
    double tair = m->tair[c->hrly_idx];
    double wind = m->wind[c->hrly_idx];
    double Ca = m->co2[c->hrly_idx];

    /*
        extinction coefficient for diffuse radiation and black leaves
        (m2 ground m2 leaf)
    */
    double kd = 0.8, net_lw_rad;

    Tk = m->tair[c->hrly_idx] + DEG_TO_KELVIN;

    /* Radiation conductance (mol m-2 s-1) */
    gradn = calc_radiation_conductance(tair);

    /* Boundary layer conductance for heat - single sided, forced
       convection (mol m-2 s-1) */
    gbhu = calc_bdn_layer_forced_conduct(tair, press, wind, p->leaf_width);

    /* Boundary layer conductance for heat - single sided, free convection */
    gbhf = calc_bdn_layer_free_conduct(tair, tleaf, press, p->leaf_width);

    /* Total boundary layer conductance for heat */
    gbh = gbhu + gbhf;

    /* Total conductance for heat - two-sided */
    gh = 2.0 * (gbh + gradn);

    /* Total conductance for water vapour */
    gbv = GBVGBH * gbh;
    gsv = GSVGSC * gsc;
    gv = (gbv * gsv) / (gbv + gsv);

    gbc = gbh / GBHGBC;

    /* Isothermal net radiation (Leuning et al. 1995, Appendix) */
    ea = calc_sat_water_vapour_press(tair) - vpd;

    /* apparent emissivity for a hemisphere radiating at air temp eqn D4 */
    emissivity_atm = 0.642 * pow((ea / Tk), (1.0 / 7.0));
    sw_rad = apar * PAR_2_SW; /* W m-2 */

    /* isothermal net LW radiaiton at top of canopy, assuming emissivity of
       the canopy is 1 */
    net_lw_rad = (1.0 - emissivity_atm) * SIGMA * pow(Tk, 4.0);
    rnet = p->leaf_abs * sw_rad - net_lw_rad * kd * exp(-kd * s->lai);

    /* Penman-Monteith equation */
    *et = penman_leaf(press, rnet, vpd, tair, gh, gv, gbv, gsv, &LE);

    /* sensible heat exchanged between leaf and surroundings */
    sensible_heat = (1.0 / (1.0 + gradn / gbh)) * (rnet - LE);

    /*
    ** calculate new dleaf, tleaf and Cs
    */
    /* Temperature difference between the leaf surface and the air */
    Tdiff = (rnet - LE) / (CP * MASS_AIR * gh);
    *new_tleaf = tair + Tdiff / 4.;
    *Cs = Ca - anleaf / gbc;                /* CO2 conc at the leaf surface */
    *dleaf = *et * press / gv;              /* VPD at the leaf surface */

    return;
}

double calc_radiation_conductance(double tair) {
    /*  Returns the 'radiation conductance' at given temperature.

        Units: mol m-2 s-1

        References:
        -----------
        * Formula from Ying-Ping's version of Maestro, cf. Wang and Leuning
          1998, Table 1,
        * See also Jones (1992) p. 108.
        * And documented in Medlyn 2007, equation A3, although I think there
          is a mistake. It should be Tk**3 not Tk**4, see W & L.
    */
    double grad;
    double Tk;

    Tk = tair + DEG_TO_KELVIN;
    grad = 4.0 * SIGMA * (Tk * Tk * Tk) * LEAF_EMISSIVITY / (CP * MASS_AIR);

    return (grad);
}

double calc_bdn_layer_forced_conduct(double tair, double press, double wind,
                                     double leaf_width) {
    /*
        Boundary layer conductance for heat - single sided, forced convection
        (mol m-2 s-1)
        See Leuning et al (1995) PC&E 18:1183-1200 Eqn E1
    */
    double cmolar, Tk, gbh;

    Tk = tair + DEG_TO_KELVIN;
    cmolar = press / (RGAS * Tk);
    gbh = 0.003 * sqrt(wind / leaf_width) * cmolar;

    return (gbh);
}

double calc_bdn_layer_free_conduct(double tair, double tleaf, double press,
                                   double leaf_width) {
    /*
        Boundary layer conductance for heat - single sided, free convection
        (mol m-2 s-1)
        See Leuning et al (1995) PC&E 18:1183-1200 Eqns E3 & E4
    */
    double cmolar, Tk, gbh, grashof, leaf_width_cubed;
    double tolerance = 1E-08;

    Tk = tair + DEG_TO_KELVIN;
    cmolar = press / (RGAS * Tk);
    leaf_width_cubed = leaf_width * leaf_width * leaf_width;

    if (float_eq((tleaf - tair), 0.0)) {
        gbh = 0.0;
    } else {
        grashof = 1.6E8 * fabs(tleaf - tair) * leaf_width_cubed;
        gbh = 0.5 * DHEAT * pow(grashof, 0.25) / leaf_width * cmolar;
    }

    return (gbh);
}


void zero_carbon_day_fluxes(fluxes *f) {

    f->gpp_gCm2 = 0.0;
    f->npp_gCm2 = 0.0;
    f->gpp = 0.0;
    f->npp = 0.0;
    f->auto_resp = 0.0;
    f->apar = 0.0;

    return;
}

void update_daily_carbon_fluxes(fluxes *f, params *p, double acanopy,
                                double total_apar) {

    /* umol m-2 s-1 -> gC m-2 30 min-1 */
    f->gpp_gCm2 += acanopy * UMOL_TO_MOL * MOL_C_TO_GRAMS_C * SEC_2_HLFHR;
    f->npp_gCm2 = f->gpp_gCm2 * p->cue;
    f->gpp = f->gpp_gCm2 * GRAM_C_2_TONNES_HA;
    f->npp = f->npp_gCm2 * GRAM_C_2_TONNES_HA;
    f->auto_resp = f->gpp - f->npp;
    f->apar += total_apar;

    return;
}

double calc_top_of_canopy_n(params *p, state *s, double ncontent)  {

    /*
    Calculate the canopy N at the top of the canopy (g N m-2), N0.
    See notes and Chen et al 93, Oecologia, 93,63-69.

    Returns:
    -------
    N0 : float (g N m-2)
        Top of the canopy N
    */
    double N0;

    if (s->lai > 0.0) {
        /* calculation for canopy N content at the top of the canopy */
        N0 = ncontent * p->kext / (1.0 - exp(-p->kext * s->lai));
    } else {
        N0 = 0.0;
    }

    return (N0);
}
