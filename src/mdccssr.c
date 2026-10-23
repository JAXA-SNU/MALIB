/*------------------------------------------------------------------------------
* mdccssr.c : QZSS L6E signal MADOCA-PPP Compact SSR message decode functions
*
* Copyright (C) 2024-2026 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
*
* references :
*     [1]  CAO IS-QZSS-MDC-004, May, 2025
*
* history : 2024/02/01 1.0  new, for MALIB from TETC original tools.
*           2024/12/20 1.1  support of BeiDou in mcssr_sel_biascode()
*                           add vendor type setting
*                           support validity period for subtype 1.
*           2025/02/06 1.2  change the sign of the phase bias correction to 
*                            correspond to IS-QZSS-MDC (ref.[1]).
*           2025/12/26 1.3  modify Compact SSR decoding function regarding
*                            undefined GNSS ID handling of Compact SSR
*                           fix bug on vendor type 
*                           support input from multiple data streams for L6E
*                           refactor and move input_qzssl6e(),input_qzssl6ef()
*                            to qzssl6.c
*                           save code/phase bias flags for discontinuity
*           2026/01/23 1.4  fix G1P/G2P handling when CodeBias is unavailable
*           2026/02/27 1.5  support BeiDou.
*                           separate GPS L5 Q and L5 I+Q for ssr bias applied signals.
*                           fix bug on signal mask of QZSS L6D, L6P, L6E and L1C/B
*-----------------------------------------------------------------------------*/
#include "rtklib.h"

/* constants -----------------------------------------------------------------*/
#define RTCM_MN_CSSR             4073   /* Compact SSR message number */
                                    
#define MCSSR_BYTELEN            1060   /* MADOCA-PPP message 5 frames byte length */

#define MCSSR_ST_MASK               1   /* Compact SSR Mask */
#define MCSSR_ST_OC                 2   /* Compact SSR GNSS Orbit Correction */
#define MCSSR_ST_CC                 3   /* Compact SSR GNSS Clock Correction */
#define MCSSR_ST_CB                 4   /* Compact SSR GNSS Satellite Code Bias */
#define MCSSR_ST_PB                 5   /* Compact SSR GNSS Satellite Phase Bias */
#define MCSSR_ST_URA                7   /* Compact SSR GNSS URA */

#define MCSSR_MAX_SYS              16   /* GNSS ID (Table 4.2.2-7) */
#define MCSSR_SYS_GPS               0
#define MCSSR_SYS_GLO               1
#define MCSSR_SYS_GAL               2
#define MCSSR_SYS_BDS               3
#define MCSSR_SYS_QZS               4
#define MCSSR_SYS_BD3               7

#define MCSSR_MAX_SATMASK          40   /* Satellite Mask (Table 4.2.2-8) */
#define MCSSR_SATMASK_GPS_U  0xFFFFFFFFu /* Note, U and L for -Wlong-long Avoidance */
#define MCSSR_SATMASK_GPS_L  0xFF
#define MCSSR_SATMASK_GLO_U  0xFFFFFF00u
#define MCSSR_SATMASK_GLO_L  0x00
#define MCSSR_SATMASK_GAL_U  0xFFFFFFFFu
#define MCSSR_SATMASK_GAL_L  0xF8
#define MCSSR_SATMASK_BDS_U  0xFFFFFFFFu
#define MCSSR_SATMASK_BDS_L  0xFF
#define MCSSR_SATMASK_QZS_U  0xFFC00000u
#define MCSSR_SATMASK_QZS_L  0x00
#define MCSSR_SATMASK_BD3_U  0xFFFFFFFFu
#define MCSSR_SATMASK_BD3_L  0xFF
#define MCSSR_SATMASK_RSV_U  0xFFFFFFFFu
#define MCSSR_SATMASK_RSV_L  0xFF

#define MCSSR_MAX_SIGMASK          16   /* Signal Mask (Table 4.2.2-9) */

#define MCSSR_INVALID_15BIT    -16384   /* Invalid Value (Table. 4.2.2-11, etc.) */
#define MCSSR_INVALID_13BIT     -4096
#define MCSSR_INVALID_11BIT     -1024

#define MCSSR_MAX_PRN               7   /* Max defined MADOCA-PPP L6E signal (Table 3-1) */

/* SSR update intervals (Table 4.2.2-6) --------------------------------------*/
static const double ssrudint[16]={
    1,2,5,10,15,30,60,120,240,300,600,900,1800,3600,7200,10800
};

/* Compact SSR Signal mask (Table 4.2.2-9) -----------------------------------*/
static const uint8_t cssr_sig_gps[16]={
    CODE_L1C,CODE_L1P,CODE_L1W,CODE_L1S,CODE_L1L,CODE_L1X,CODE_L2S,CODE_L2L,
    CODE_L2X,CODE_L2P,CODE_L2W,CODE_L5I,CODE_L5Q,CODE_L5X,       0,       0
};
static const uint8_t cssr_sig_glo[16]={
    CODE_L1C,CODE_L1P,CODE_L2C,CODE_L2P,CODE_L4A,CODE_L4B,CODE_L4X,CODE_L6A,
    CODE_L6B,CODE_L6X,CODE_L3I,CODE_L3Q,CODE_L3X,       0,       0,       0
};
static const uint8_t cssr_sig_gal[16]={
    CODE_L1B,CODE_L1C,CODE_L1X,CODE_L5I,CODE_L5Q,CODE_L5X,CODE_L7I,CODE_L7Q,
    CODE_L7X,CODE_L8I,CODE_L8Q,CODE_L8X,CODE_L6B,CODE_L6C,       0,       0
};
static const uint8_t cssr_sig_bds[16]={
    CODE_L2I,CODE_L2Q,CODE_L2X,CODE_L6I,CODE_L6Q,CODE_L6X,CODE_L7I,CODE_L7Q,
    CODE_L7X,       0,       0,       0,       0,       0,       0,       0
};
static const uint8_t cssr_sig_bd3[16]={
    CODE_L2I,CODE_L2Q,       0,CODE_L6I,CODE_L6Q,       0,CODE_L7D,CODE_L7P,
    CODE_L7Z,CODE_L1D,CODE_L1P,CODE_L1X,CODE_L5D,CODE_L5P,CODE_L5X,       0
};
static const uint8_t cssr_sig_qzs[16]={
    CODE_L1C,CODE_L1S,CODE_L1L,CODE_L1X,CODE_L2S,CODE_L2L,CODE_L2X,CODE_L5I,
    CODE_L5Q,CODE_L5X,CODE_L6S,CODE_L6L,CODE_L6E,CODE_L1E,       0,       0
};

const uint8_t cssr_sig_rsv[16]={0};

/* MADOCA-PPP Sub Type Transmission Pattern (Figure 4.2.2-1) -----------------*/
/* note : Change MCSSR_BYTELEN when defining 6 or more. */
static const int framelen[]={       /* frame length(-1 : invalid) */
    -1, 5,-1, 3, 5, 5,-1, 3,        /* ST 0, 1, 2, 3, 4, 5, 6, 7 */
    -1,-1,-1,-1,-1,-1,-1,-1         /* ST 8, 9,10,11,12,13,14,15 */
};

typedef struct {
    gtime_t gt;                     /* current Epoch Time 1s */
    int mgfid;                      /* current Msg. Gen. Facility ID */
    int prn;                        /* current Msg. PRN */
    int prn_chk_cnt;                /* check counter for PRN auto selection */
    uint8_t buff[MCSSR_BYTELEN];    /* Compact-SSR Message */
    int ibuff;                      /* indicate the end of buff */
    int satlist[MAXSAT];            /* 0 : invalid, 1... : satno(G01...) */
    int gidlist[MAXSAT];            /* GNSS ID */
    int cellmask[MAXSAT];           /* Cellmask */
    int ncell[MAXSAT];              /* No. of cellmask flags */
    int siglist[MCSSR_MAX_SYS][MCSSR_MAX_SIGMASK];/* 0:Terminate,1...:CODE_xxx */
    int nsat[MCSSR_MAX_SYS];        /* No. of sat */
    int nsig[MCSSR_MAX_SYS];        /* No. of signal */
    int ns;                         /* total No. of */
    int maxframe;                   /* set framelen[first subtype] */
    int frame;                      /* frame counter */
    int tow0;                       /* for Hourly Epoch Time 1s */
    int ep0;                        /* for Hourly Epoch Time 1s regresses check */
    int valep0;                     /* subtype 1 validity time */
    int iod;                        /* IOD SSR */
} mcssr_t;

static mcssr_t _mcssr[NSTRIN];

/* count 16 bit flags --------------------------------------------------------*/
static int flgcnt16(const uint16_t flg)
{
    int i,cnt=0;

    for (i=0;i<16;i++) if ((flg>>i) & 0x1) cnt++;
    return cnt;
}
/* ---------------------------------------------------------------------------*/
static int gnssid2sys(const int gnssid)
{
    switch(gnssid) {
        case MCSSR_SYS_GPS : return SYS_GPS;
        case MCSSR_SYS_GLO : return SYS_GLO;
        case MCSSR_SYS_GAL : return SYS_GAL;
        case MCSSR_SYS_BDS : return SYS_CMP;
        case MCSSR_SYS_QZS : return SYS_QZS;
        case MCSSR_SYS_BD3 : return SYS_CMP;
    }
    return SYS_NONE;
}
/* ---------------------------------------------------------------------------*/
static int svmask2list(const uint64_t mask, const int gnssid, int *satlist, int *gidlist)
{
    int i,offp,ns=0;
    uint64_t svbit = (uint64_t)1 << (MCSSR_MAX_SATMASK - 1);

    for (i = 0; i < MCSSR_MAX_SATMASK; i++) {
        if (mask & svbit) {
            switch(gnssid) {
                case MCSSR_SYS_GPS : offp =   1; break;
                case MCSSR_SYS_GLO : offp =   1; break;
                case MCSSR_SYS_GAL : offp =   1; break;
                case MCSSR_SYS_BDS : offp =   1; break;
                case MCSSR_SYS_QZS : offp = 193; break;
                case MCSSR_SYS_BD3 : offp =  19; break;
                default            : offp =   1;
            }
            satlist[ns] = satno(gnssid2sys(gnssid),offp+i);
            gidlist[ns] = gnssid;
            ns++;
        }
        svbit >>= 1;
    }
    return ns;
}

/* ---------------------------------------------------------------------------*/
static int sigmask2list(const uint16_t mask, const int gnssid, int *siglist)
{
    int i,nsig=0;
    const uint8_t *sigs;
    uint16_t sigbit = (uint16_t)1 << (MCSSR_MAX_SIGMASK - 1);

    switch(gnssid) {
        case MCSSR_SYS_GPS : sigs = cssr_sig_gps; break;
        case MCSSR_SYS_GLO : sigs = cssr_sig_glo; break;
        case MCSSR_SYS_GAL : sigs = cssr_sig_gal; break;
        case MCSSR_SYS_BDS : sigs = cssr_sig_bds; break;
        case MCSSR_SYS_QZS : sigs = cssr_sig_qzs; break;
        case MCSSR_SYS_BD3 : sigs = cssr_sig_bd3; break;
        default            : sigs = cssr_sig_rsv;
    }

    for (i = 0; i < MCSSR_MAX_SIGMASK; i++) {
        if (mask & sigbit) {
            siglist[nsig] = sigs[i];
            nsig++;
        }
        sigbit >>= 1;
    }
    return nsig;
}

/* adjust weekly rollover of gps time ----------------------------------------*/
static void adjweek(gtime_t *gt, double tow)
{
    double tow_p;
    int week;
    
    /* if no time, get cpu time */
    if (gt->time == 0) *gt = utc2gpst(timeget());
    tow_p = time2gpst(*gt, &week);
    if      (tow < tow_p - 302400.0) tow+=604800.0;
    else if (tow > tow_p + 302400.0) tow-=604800.0;
    *gt = gpst2time(week, tow);
}

/* decode MADOCA-PPP L6E CSSR header -----------------------------------------*/
static int decode_mcssr_head(const mcssr_t *mc, int *sync, int *iod, int *ep,
                             double *udint, int i, const int st)
{
    int n, udi;

    n = (st == MCSSR_ST_MASK)? 20: 12;
    *ep    = getbitu(mc->buff, i, n); i+=n; /* GPS or GNSS Hourly Epoch Time 1s */
    udi    = getbitu(mc->buff, i, 4); i+=4; /* SSR Update Interval */
    *sync  = getbitu(mc->buff, i, 1); i+=1; /* Multiple Message Indicator */
    *iod   = getbitu(mc->buff, i, 4); i+=4; /* IOD SSR */
    *udint = ssrudint[udi];

    trace(4,"decode_mcssr_head: st=%d, ep=%d, udi=%d, sync=%d, iod=%d\n",
        st, *ep, udi, *sync, *iod);
    return i;
}

/* decode MADOCA-PPP L6E CSSR ST1 mask message -------------------------------*/
static int decode_mcssr_mask(mcssr_t *mc, int i)
{
    int j, k, sync, iod, ep, ngnss, gnssid, cmaflg, nsat, nsig;
    uint64_t svmask,gnssmask;
    uint16_t sigmask,cellmask;
    double udint;

    i = decode_mcssr_head(mc, &sync, &iod, &ep, &udint, i, MCSSR_ST_MASK);

    mc->tow0   = ep / 3600 * 3600;
    mc->ep0    = ep % 3600;
    mc->valep0 = mc->ep0 + (int)udint;
    adjweek(&mc->gt, ep);
    mc->iod  = iod;
    mc->ns = 0;
    for (j = 0; j < MCSSR_MAX_SYS; j++) {
        mc->nsig[j] = 0;
        mc->nsat[j] = 0;
    }
    for (j = 0; j < MAXSAT; j++) {
        mc->cellmask[j] = 0;
    }

    ngnss = getbitu(mc->buff, i, 4); i+=4;              /* No. of GNSS */
    for (j = 0; j < ngnss; j++) {
        gnssid = getbitu(mc->buff, i, 4); i+= 4;        /* GNSS ID */
        switch(gnssid) {
            case MCSSR_SYS_GPS : gnssmask = ((uint64_t)MCSSR_SATMASK_GPS_U) << 8 | MCSSR_SATMASK_GPS_L; break;
            case MCSSR_SYS_GLO : gnssmask = ((uint64_t)MCSSR_SATMASK_GLO_U) << 8 | MCSSR_SATMASK_GLO_L; break;
            case MCSSR_SYS_GAL : gnssmask = ((uint64_t)MCSSR_SATMASK_GAL_U) << 8 | MCSSR_SATMASK_GAL_L; break;
            case MCSSR_SYS_BDS : gnssmask = ((uint64_t)MCSSR_SATMASK_BDS_U) << 8 | MCSSR_SATMASK_BDS_L; break;
            case MCSSR_SYS_QZS : gnssmask = ((uint64_t)MCSSR_SATMASK_QZS_U) << 8 | MCSSR_SATMASK_QZS_L; break;
            case MCSSR_SYS_BD3 : gnssmask = ((uint64_t)MCSSR_SATMASK_BD3_U) << 8 | MCSSR_SATMASK_BD3_L; break;
            default            : gnssmask = ((uint64_t)MCSSR_SATMASK_RSV_U) << 8 | MCSSR_SATMASK_RSV_L;
                                 trace(2,"decode_mcssr_mask: reserved gnssid=%d\n",gnssid);
        }
        svmask   = (uint64_t)getbitu(mc->buff, i, 8)<<32; i+=8;
        svmask  |= getbitu(mc->buff, i, 32); i+=32;     /* Sat mask */
        svmask  &= gnssmask;
        sigmask  = getbitu(mc->buff, i, 16); i+=16;     /* Signal mask */
        cmaflg   = getbitu(mc->buff, i,  1); i+= 1;     /* Cell-mask Avail.Flag */
        nsat = svmask2list (svmask,  gnssid,&mc->satlist[mc->ns], &mc->gidlist[mc->ns]);
        nsig = sigmask2list(sigmask, gnssid, mc->siglist[gnssid]);

        trace(4,"decode_mcssr_mask: gnssid=%d, svmask=0x%02X%08X, sigmask=0x%X, cmaflg=%d, nsat=%d, nsig=%d\n",
            gnssid, (uint8_t)(svmask>>32), (uint32_t)(svmask), sigmask, cmaflg, nsat, nsig);

        if (cmaflg) {
            for (k = 0; k < nsat; k++) {
                cellmask = getbitu(mc->buff, i, nsig); i+=nsig; /* Cell-mask */
                mc->cellmask[mc->ns + k] = cellmask;
                mc->ncell[mc->ns + k] = flgcnt16(cellmask);
                trace(4,"decode_mcssr_mask: k=%d, cellmask=0x%X, ncell=%2d\n",
                    k,cellmask,mc->ncell[mc->ns + k]);
            }
        }
        else {
            for (k = 0; k < nsat; k++) {
                mc->cellmask[mc->ns + k] = (1 << nsig) - 1; /* ex) 4=0xF,5=0x1F */
                mc->ncell[mc->ns + k] = nsig;
            }
        }
        mc->nsat[gnssid] = nsat;
        mc->nsig[gnssid] = nsig;
        mc->ns += nsat;
    }
    return i;
}

/* decode MADOCA-PPP L6E CSSR ST2 orbit correction ---------------------------*/
static int decode_mcssr_oc(mcssr_t *mc, ssr_t *ssr, int i, int apply)
{
    int j, k, n, sync, iod, ep, sat, iode, deph[3];
    double udint;
    char satid[8];

    i = decode_mcssr_head(mc, &sync, &iod, &ep, &udint, i, MCSSR_ST_OC);
    if (mc->tow0 < 0) {
        trace(2,"decode_mcssr_oc: unprocessed mask\n");
        return -1;
    }
    if (mc->ep0 > ep) ep += 3600;
    if (ep >= mc->valep0) {
        trace(2,"decode_mcssr_oc: ep %d,expired ep %d by cssr ST1 mask\n",
            ep, mc->valep0);
        return -1;
    }
    if (mc->iod != iod) {
        trace(2,"decode_mcssr_oc: ep %d,iod mismatch %d != %d\n",
            ep,mc->iod,iod);
        return -1;
    }
    adjweek(&mc->gt, mc->tow0 + ep);
    for (j = 0; j < mc->ns; j++) {
        n = (mc->gidlist[j]==MCSSR_SYS_GAL)? 10: 8;
        iode    = getbitu(mc->buff, i,  n); i+= n; /* IODE */
        deph[0] = getbits(mc->buff, i, 15); i+=15; /* Delta Radial */
        deph[1] = getbits(mc->buff, i, 13); i+=13; /* Delta Along-Track */
        deph[2] = getbits(mc->buff, i, 13); i+=13; /* Delta Cross-Track */

        sat=mc->satlist[j];
        satno2id(sat, satid);
        trace(4,"decode_mcssr_oc :%s,i=%4d,j=%3d,sat=%s,iode=%4d,deph=%6d,%6d,%6d,%8.4f,%8.4f,%8.4f\n",
            time_str(mc->gt,3),i,j,satid,iode,deph[0],deph[1],deph[2],
            deph[0]*0.0016,deph[1]*0.0064,deph[2]*0.0064);

        if (sat == 0) continue; /* unsupprted sat */
        if ((deph[0] == MCSSR_INVALID_15BIT) ||
           (deph[1] == MCSSR_INVALID_13BIT) ||
           (deph[2] == MCSSR_INVALID_13BIT)) continue; /* invalid value */

        if (apply) {
            ssr[sat-1].vendor  = SSR_VENDOR_L6;
            ssr[sat-1].t0[0]   = mc->gt;
            ssr[sat-1].udi[0]  = udint;
            ssr[sat-1].iod[0]  = iod;
            ssr[sat-1].iode    = iode;
            ssr[sat-1].deph[0] = deph[0] * 0.0016;
            ssr[sat-1].deph[1] = deph[1] * 0.0064;
            ssr[sat-1].deph[2] = deph[2] * 0.0064;
            for (k = 0; k < 3; k++) ssr[sat-1].ddeph[k] = 0.0;
            ssr[sat-1].update=1;
        }
    }
    return i;
}

/* decode MADOCA-PPP L6E CSSR ST3 clock correction ---------------------------*/
static int decode_mcssr_cc(mcssr_t *mc, ssr_t *ssr, int i, int apply)
{
    int j, sync, iod, ep, sat, dclk;
    double udint;
    char satid[8];

    i = decode_mcssr_head(mc, &sync, &iod, &ep, &udint, i, MCSSR_ST_CC);
    if (mc->tow0 < 0) {
        trace(2,"decode_mcssr_cc: unprocessed mask\n");
        return -1;
    }
    if (mc->ep0 > ep) ep += 3600;
    if (ep >= mc->valep0) {
        trace(2,"decode_mcssr_cc: ep %d,expired ep %d by cssr ST1 mask\n",
            ep, mc->valep0);
        return -1;
    }
    if (mc->iod != iod) {
        trace(2,"decode_mcssr_cc: ep %d,iod mismatch %d != %d\n",
            ep,mc->iod,iod);
        return -1;
    }
    adjweek(&mc->gt, mc->tow0 + ep);
    for (j = 0; j < mc->ns; j++) {
        dclk = getbits(mc->buff, i, 15); i+=15; /* Delta Clock C0 */

        sat=mc->satlist[j];
        satno2id(sat, satid);

        trace(4,"decode_mcssr_cc :%s,i=%4d,j=%3d,sat=%s,dclk=%6d,%8.4f\n",
            time_str(mc->gt,3),i,j,satid,dclk,dclk*0.0016);

        if (sat == 0) continue; /* unsupprted sat */
        if (dclk == MCSSR_INVALID_15BIT) continue; /* invalid value */

        if (apply) {
            ssr[sat-1].vendor  = SSR_VENDOR_L6;
            ssr[sat-1].t0[1]   = mc->gt;
            ssr[sat-1].udi[1]  = udint;
            ssr[sat-1].iod[1]  = iod;
            ssr[sat-1].dclk[0] = dclk * 0.0016;
            ssr[sat-1].dclk[1] = 0.0;
            ssr[sat-1].dclk[2] = 0.0;
            ssr[sat-1].update=1;
        }
    }
    return i;
}

/* decode MADOCA-PPP L6E CSSR ST4 code bias ----------------------------------*/
static int decode_mcssr_cb(mcssr_t *mc, ssr_t *ssr, int i, int apply)
{
    int j, k, sync, iod, ep, sat, nsig, mask, cb, flg;
    int vcbias[MAXCODE]={0};
    double udint,cbias[MAXCODE];
    char satid[8];

    i = decode_mcssr_head(mc, &sync, &iod, &ep, &udint, i, MCSSR_ST_CB);
    if (mc->tow0 < 0) {
        trace(2,"decode_mcssr_cb: unprocessed mask\n");
        return -1;
    }
    if (mc->ep0 > ep) ep += 3600;
    if (ep >= mc->valep0) {
        trace(2,"decode_mcssr_cb: ep %d,expired ep %d by cssr ST1 mask\n",
            ep, mc->valep0);
        return -1;
    }
    if (mc->iod != iod) {
        trace(2,"decode_mcssr_cb: ep %d,iod mismatch %d != %d\n",
            ep,mc->iod,iod);
        return -1;
    }
    adjweek(&mc->gt, mc->tow0 + ep);
    for (j = 0; j < mc->ns; j++) {
        for (k=0;k<MAXCODE;k++) {
            cbias[k]=0.0;
            vcbias[k]=0;
        }
        sat = mc->satlist[j];
        satno2id(sat, satid);
        nsig = mc->nsig[mc->gidlist[j]];
        mask = 1 << (nsig - 1);
        for (k = 0; k < nsig; k++) {
            if ((flg = (mc->cellmask[j] & mask))) {
                cb = getbits(mc->buff, i, 11); i+=11; /* Code Bias */
                trace(4,"decode_mcssr_cb :%s,i=%4d,j=%3d,k=%2d,sat=%s,flg=0x%2X,sig=%2d,cb=%5d,%6.2f\n",
                    time_str(mc->gt,3),i,j,k,satid,flg,mc->siglist[mc->gidlist[j]][k],cb,cb*0.02);

                if (sat == 0) { mask >>= 1; continue; } /* unsupported sat */
                if (cb == MCSSR_INVALID_11BIT) { mask >>= 1; continue; } /* invalid value */

                cbias[mc->siglist[mc->gidlist[j]][k]-1]  = cb * 0.02;
                vcbias[mc->siglist[mc->gidlist[j]][k]-1] = 1;
            }
            mask >>= 1;
        }
        if (apply) {
            ssr[sat-1].vendor  = SSR_VENDOR_L6;
            ssr[sat-1].t0[4]   = mc->gt;
            ssr[sat-1].udi[4]  = udint;
            ssr[sat-1].iod[4]  = iod;
            for (k = 0; k < MAXCODE; k++) {
                ssr[sat-1].cbias [k] = (float)cbias[k];
                ssr[sat-1].vcbias[k] = vcbias[k];
            }
            ssr[sat-1].update=1;
        }
    }
    return i;
}

/* decode MADOCA-PPP L6E CSSR ST5 phase bias ---------------------------------*/
static int decode_mcssr_pb(mcssr_t *mc, ssr_t *ssr, int i, int apply)
{
    int j, k, sync, iod, ep, sat, nsig, mask, pb, di, flg;
    int disc[MAXCODE],vpbias[MAXCODE]={0};
    double udint,pbias[MAXCODE];
    char satid[8];

    i = decode_mcssr_head(mc, &sync, &iod, &ep, &udint, i, MCSSR_ST_PB);
    if (mc->tow0 < 0) {
        trace(2,"decode_mcssr_pb: unprocessed mask\n");
        return -1;
    }
    if (mc->ep0 > ep) ep += 3600;
    if (ep >= mc->valep0) {
        trace(2,"decode_mcssr_pb: ep %d,expired ep %d by cssr ST1 mask\n",
            ep, mc->valep0);
        return -1;
    }
    if (mc->iod != iod) {
        trace(2,"decode_mcssr_pb: ep %d,iod mismatch %d != %d\n",
            ep,mc->iod,iod);
        return -1;
    }
    adjweek(&mc->gt, mc->tow0 + ep);
    for (j = 0; j < mc->ns; j++) {
        for (k=0;k<MAXCODE;k++) {
            pbias[k]=0.0;
            vpbias[k]=0;
            disc[k]=0;
        }
        sat = mc->satlist[j];
        satno2id(sat, satid);
        nsig = mc->nsig[mc->gidlist[j]];
        mask = 1 << (nsig - 1);
        for (k = 0; k < nsig; k++) {
            if ((flg = (mc->cellmask[j] & mask))) {
                pb = getbits(mc->buff, i, 15); i+=15; /* Phase Bias */
                di = getbitu(mc->buff, i,  2); i+= 2; /* Discontinuity Indicator */

                trace(4,"decode_mcssr_pb :%s,i=%4d,j=%3d,k=%2d,sat=%s,flg=0x%2X,sig=%2d,di=%d,pb=%5d,%7.3f\n",
                    time_str(mc->gt,3),i,j,k,satid,flg,mc->siglist[mc->gidlist[j]][k],di,pb,pb*0.001);

                if (sat == 0) { mask >>= 1; continue; } /* unsupported sat */
                if (pb == MCSSR_INVALID_15BIT) { mask >>= 1; continue; } /* invalid value */

                pbias[mc->siglist[mc->gidlist[j]][k]-1]  = pb * 0.001;
                vpbias[mc->siglist[mc->gidlist[j]][k]-1] = 1;
                disc[mc->siglist[mc->gidlist[j]][k]-1]   = di;
            }
            mask >>= 1;
        }
        if (apply) {
            ssr[sat-1].vendor   = SSR_VENDOR_L6;
            ssr[sat-1].t0[5]    = mc->gt;
            ssr[sat-1].udi[5]   = udint;
            ssr[sat-1].iod[5]   = iod;
            ssr[sat-1].yaw_ang  = 0.0;
            ssr[sat-1].yaw_rate = 0.0;
            for (k=0;k<MAXCODE;k++) {
                ssr[sat-1].pbias[k]  = (float)pbias[k];
                ssr[sat-1].vpbias[k] = vpbias[k];
                ssr[sat-1].disc[k]   = disc[k];
            }
            ssr[sat-1].update=1;
        }
    }
    return i;
}

/* decode MADOCA-PPP L6E CSSR ST7 URA ----------------------------------------*/
static int decode_mcssr_ura(mcssr_t *mc, ssr_t *ssr, int i, int apply)
{
    int j, sync, iod, ep, sat, ura;
    double udint;
    char satid[8];

    i = decode_mcssr_head(mc, &sync, &iod, &ep, &udint, i, MCSSR_ST_URA);
    if (mc->tow0 < 0) {
        trace(2,"decode_mcssr_ura: unprocessed mask\n");
        return -1;
    }
    if (mc->ep0 > ep) ep += 3600;
    if (ep >= mc->valep0) {
        trace(2,"decode_mcssr_ura: ep %d,expired ep %d by cssr ST1 mask\n",
            ep, mc->valep0);
        return -1;
    }
    if (mc->iod != iod) {
        trace(2,"decode_mcssr_ura: ep %d,iod mismatch %d != %d\n",
            ep,mc->iod,iod);
        return -1;
    }
    adjweek(&mc->gt, mc->tow0 + ep);
    for (j = 0; j < mc->ns; j++) {
        ura = getbitu(mc->buff, i, 6); i+=6;

        sat=mc->satlist[j];
        satno2id(sat, satid);

        trace(4,"decode_mcssr_ura :%s,i=%4d,j=%3d,sat=%s,ura=0x%02X\n",
            time_str(mc->gt,3),i,j,satid,ura);

        if (sat == 0) continue; /* unsupprted sat */
        if (apply) {
            ssr[sat-1].vendor  = SSR_VENDOR_L6;
            ssr[sat-1].t0[3]   = mc->gt;
            ssr[sat-1].udi[3]  = udint;
            ssr[sat-1].iod[3]  = iod;
            ssr[sat-1].ura     = ura;
            ssr[sat-1].update=1;
        }
    }
    return i;
}

/* decode QZSS MADOCA-PPP CSSR message -----------------------------------------
* decode QZSS MADOCA-PPP CSSR message and convert to RTCM SSR
* args   : rtcm_t *rtcm IO   rtcm control struct
*          int    index I    input stream index
* return : status (-1: error message, 0: no message, 10: input ssr messages)
* note   : before calling the function, init_mcssr()
*-----------------------------------------------------------------------------*/
extern int decode_qzss_mdccssr(rtcm_t *rtcm, int index)
{
    int i=L6HEAD_BITLEN,n,ibuff,mn,st,apply,ret,optprn;
    char *str;
    uint8_t mask;
    qzssl6_t *l6=rtcm->l6info;

    l6->type = QZSS_L6TYPE_MSSR;
    
    if (l6->alert) {
        if (strstr(rtcm->opt,"-IGNORE_MCSSR_ALERT")) {
            trace(3,"decode_qzss_mdccssr : ignore alert\n",time_str(_mcssr[index].gt,3));
        }
        else {
            trace(2,"decode_qzss_mdccssr : alert\n",time_str(_mcssr[index].gt,3));
            return -1;
        }
    }
  
    if ((str = strstr(rtcm->opt,"-MCSSR_PRN"))) {
        optprn = atoi(strstr(str,"=")+1);
        if (optprn != l6->prn) {
            trace(3,"decode_qzss_mdccssr : -MCSSR_PRN=%d\n",optprn);
            return -1;
        }
    }
    else {
        if (_mcssr[index].prn == -1) {
            _mcssr[index].prn         = l6->prn;
        }
        else {
            if (_mcssr[index].prn != l6->prn) {
                _mcssr[index].prn_chk_cnt++;
                trace(3,"decode_qzss_mdccssr : prn mismatch _mcssr.prn=%d,prn=%d,cnt=%d\n",
                    _mcssr[index].prn,l6->prn,_mcssr[index].prn_chk_cnt);
                if (_mcssr[index].prn_chk_cnt < MCSSR_MAX_PRN) {
                    return -1;
                }
                else {
                    trace(2,"decode_qzss_mdccssr :%s,prn switched %d -> %d\n",
                        time_str(_mcssr[index].gt,3),_mcssr[index].prn,l6->prn);
                    _mcssr[index].prn = l6->prn;
                }
            }
        }
        _mcssr[index].prn_chk_cnt = 0;
    }

    if (l6->si==1) _mcssr[index].maxframe = 0; /* Subframe Indicator : First Data */

    /* frame recognition */
    if (_mcssr[index].maxframe <= 0) {
        mn = getbitu(rtcm->buff,   i, 12); /* Message Number(note:not i++) */
        st = getbitu(rtcm->buff,i+12,  4); /* Message Sub Type ID */
        if (mn != RTCM_MN_CSSR) {
            trace(2,"decode_qzss_mdccssr :%s,invalid mn=%d\n",
                time_str(_mcssr[index].gt,3),mn);
            return -1;
        }
        if ((_mcssr[index].maxframe = framelen[st]) < 1) {
            trace(2,"decode_qzss_mdccssr :%s,invalid frame start st=%d\n",
                time_str(_mcssr[index].gt,3),st);
            return -1;
        }
        trace(3,"decode_qzss_mdccssr : %s,mn=%d,st=%d,maxframe=%d\n",
            time_str(_mcssr[index].gt,3),mn,st,_mcssr[index].maxframe);
        _mcssr[index].frame=0;
        _mcssr[index].ibuff=0;
    }

    /* save data part (1695 bits) */
    _mcssr[index].frame++;
    trace(3,"decode_qzss_mdccssr : %s,maxframe=%d,frame=%d\n",
        time_str(_mcssr[index].gt,3),_mcssr[index].maxframe,_mcssr[index].frame);
    switch(_mcssr[index].frame) {
        case 1: ibuff=  0; n = 8; mask = 0xFE; break;
        case 2: ibuff=211; n = 1; mask = 0xFC; break; 
        case 3: ibuff=423; n = 2; mask = 0xF8; break;
        case 4: ibuff=635; n = 3; mask = 0xF0; break;
        case 5: ibuff=847; n = 4; mask = 0xE0; break;
        default:trace(3,"decode_qzss_mdccssr : invalid frame\n"); return -1;
    }
    _mcssr[index].buff[ibuff++] |= (unsigned char)getbitu(rtcm->buff, i, n); i += n;
    while (i < L6HEAD_BITLEN + L6DATA_BITLEN) {
        _mcssr[index].buff[ibuff++] = (unsigned char)getbitu(rtcm->buff, i, 8); i += 8;
    }
    _mcssr[index].buff[ibuff - 1] &= mask;

    if (_mcssr[index].frame != _mcssr[index].maxframe) return 0; /* no message */

    ret=10; /* input ssr messages */
    i=0;
    while (i < L6DATA_BITLEN * _mcssr[index].maxframe) {
        mn = getbitu(_mcssr[index].buff,i,12); i+=12; /* Message Number */
        if (mn != RTCM_MN_CSSR) continue;
        st = getbitu(_mcssr[index].buff,i, 4); i+= 4; /* Message Sub Type ID */
        if ((st != MCSSR_ST_MASK) && (st != MCSSR_ST_OC) && 
           (st != MCSSR_ST_CC)   && (st != MCSSR_ST_CB) &&
           (st != MCSSR_ST_PB)   && (st != MCSSR_ST_URA)) continue;
        if (st == MCSSR_ST_MASK) {
            if (_mcssr[index].mgfid != l6->mgfid) {
                trace(2,"decode_qzss_mdccssr :%s,mgfid switched %d -> %d\n",
                    time_str(_mcssr[index].gt,3),_mcssr[index].mgfid,l6->mgfid);
                _mcssr[index].mgfid = l6->mgfid;
            }
            i = decode_mcssr_mask(&_mcssr[index], i);
        }
        else {
            apply = 1;
            /* Note, ref[1] 4.2.1(3) ID 0 and 2 are the same, 1 and 3 are the same. */
            if ((_mcssr[index].mgfid & 0x1) != (l6->mgfid & 0x1)) {
                trace(2,"decode_qzss_mdccssr :%s,st=%d,mgfid mismatch %d != %d\n",
                    time_str(_mcssr[index].gt,3),st,_mcssr[index].mgfid,l6->mgfid);
                apply = 0;
            }
            switch(st) {
                case MCSSR_ST_OC  :
                    i = decode_mcssr_oc (&_mcssr[index], rtcm->ssr, i, apply);
                    break;
                case MCSSR_ST_CC  :
                    i = decode_mcssr_cc (&_mcssr[index], rtcm->ssr, i, apply);
                    break;
                case MCSSR_ST_CB  :
                    if (strstr(rtcm->opt,"-DIS_MCSSR_CB")) apply = 0;
                    i = decode_mcssr_cb (&_mcssr[index], rtcm->ssr, i, apply);
                    break;
                case MCSSR_ST_PB  :
                    i = decode_mcssr_pb (&_mcssr[index], rtcm->ssr, i, apply);
                    break;
                case MCSSR_ST_URA :
                    i = decode_mcssr_ura(&_mcssr[index], rtcm->ssr, i, apply);
                    break;
            default :
                trace(2,"decode_qzss_mdccssr : invalid st=%d\n",st);
                ret = -1;
                break;
            }
        }
        if (i<0) {
            trace(2,"decode_qzss_mdccssr : decode error st=%d\n",st);
            ret = -1;
            break;
        }
        rtcm->time = _mcssr[index].gt;
    }
    _mcssr[index].maxframe = 0;
    trace(4,"decode_qzss_mdccssr : %s,frames=%d\n",
        time_str(_mcssr[index].gt,3),_mcssr[index].frame);
    return ret; 
}

/* initialize MADOCA-PPP CSSR control ------------------------------------------
* initialize MADOCA-PPP CSSR control struct
* args   : gtime_t *gt   I    GPST for week number determination
*          int     index I    input stream index
* return : none
*-----------------------------------------------------------------------------*/
extern void init_mcssr(const gtime_t gt, int index)
{
    _mcssr[index].gt    = gt; /* for week number determination */
    _mcssr[index].mgfid = -1; /* invalid value */
    _mcssr[index].prn   = -1; /* invalid value */
    _mcssr[index].tow0  = -1; /* invalid value */
    _mcssr[index].frame =  0; /* initial value */
}

/* MADOCA-PPP bias code selection  ---------------------------------------------
* select MADOCA-PPP code bias, phase bias code from observation code
* args   : int sys  I    navigation system
*          int code I    observation code
* return : MADOCA-PPP CB,PB code
* note   : ref [1] section 5.5.3.1 Applicable Signals of Code/Phase Bias
*-----------------------------------------------------------------------------*/
extern int mcssr_sel_biascode(const int sys, const int code)
{
    switch (sys) {
        case SYS_GPS:
            switch (code) {
                case CODE_L1C: return CODE_L1C; /* L1C/A */
                case CODE_L1P:                  /* L1P */
                case CODE_L1W: return CODE_L1W; /* L1 Z-tracking */
                case CODE_L1S:                  /* L1C(D) */
                case CODE_L1L:                  /* L1C(P) */
                case CODE_L1X: return CODE_L1X; /* L1C(D+P) */
                case CODE_L2S:                  /* L2C(M) */
                case CODE_L2L:                  /* L2C(L) */
                case CODE_L2X: return CODE_L2X; /* L2C(M+L) */
                case CODE_L2P:                  /* L2P */
                case CODE_L2W: return CODE_L2W; /* L2 Z-tracking */
                case CODE_L5I: return CODE_L5I; /* L5I */
                case CODE_L5Q: return CODE_L5Q; /* L5Q */
                case CODE_L5X: return CODE_L5X; /* L5X */
            }
            break;
        case SYS_GLO:
            switch (code) {
                /* Changed: was returning L1P. Now map L1P->L1C (L2P->L2C)
                   to avoid rejecting observations without CodeBias. */
                case CODE_L1C:                  /* G1C/A */
                case CODE_L1P: return CODE_L1C; /* G1P */
                case CODE_L2C:                  /* G2C/A */
                case CODE_L2P: return CODE_L2C; /* G2P */
            }
            break;
        case SYS_GAL:
            switch (code) {
                case CODE_L1C:                  /* E1C */
                case CODE_L1B:                  /* E1B */
                case CODE_L1X: return CODE_L1X; /* E1B+C */
                case CODE_L5I:                  /* E5aI */
                case CODE_L5Q:                  /* E5aQ */
                case CODE_L5X: return CODE_L5X; /* E5aI+Q */
                case CODE_L7I:                  /* E5bI */
                case CODE_L7Q:                  /* E5bQ */
                case CODE_L7X: return CODE_L7X; /* E5bI+Q */
                case CODE_L6B:                  /* E6B */
                case CODE_L6C:                  /* E6C */
                case CODE_L6X: return CODE_L6B; /* E6B+C */
            }
            break;
        case SYS_QZS:
            switch (code) {
                case CODE_L1C: return CODE_L1C; /* L1C/A */
                case CODE_L1S:                  /* L1C(D) */
                case CODE_L1L:                  /* L1C(P) */
                case CODE_L1X: return CODE_L1X; /* L1C(D+P) */
                case CODE_L2S:                  /* L2C(M) */
                case CODE_L2L:                  /* L2C(L) */
                case CODE_L2X: return CODE_L2X; /* L2C(M+L) */
                case CODE_L5I:                  /* L5I */
                case CODE_L5Q:                  /* L5Q */
                case CODE_L5X: return CODE_L5X; /* L5X */
                case CODE_L1E: return CODE_L1E; /* L1C/B */
            }
            break;
        case SYS_CMP:
            switch (code) {
                case CODE_L2I:                  /* B1I */
                case CODE_L2Q:                  /* B1Q */
                case CODE_L2X: return CODE_L2I; /* B1I+Q */
                case CODE_L6I:                  /* B3I */
                case CODE_L6Q:                  /* B3Q */
                case CODE_L6X: return CODE_L6I; /* B3I+Q */
                case CODE_L7I:                  /* B2I */
                case CODE_L7Q:                  /* B2Q */
                case CODE_L7X: return CODE_L7I; /* B2I+Q */
                case CODE_L5D: return CODE_L5D; /* B2aI */
                case CODE_L5P: return CODE_L5P; /* B2aQ */
                case CODE_L5X: return CODE_L5X; /* B2aI+Q */
                case CODE_L1D: return CODE_L1D; /* B1C(D) */
                case CODE_L1P: return CODE_L1P; /* B1C(P) */
                case CODE_L1X: return CODE_L1X; /* B1C(D+P) */
                case CODE_L7D: return CODE_L7D; /* B2b */
            }
            break;
        }
    return CODE_NONE;
}
