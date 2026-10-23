/*------------------------------------------------------------------------------
* mdciono.c : QZSS L6D signal MADOCA-PPP wide area ionospheric correction message decode functions
*
* Copyright (C) 2025 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
*
* references :
*     [1]  CAO IS-QZSS-MDC-004, June, 2025
*
* history : 2025/12/26 1.0  new, for MALIB from TETC original tools.
*-----------------------------------------------------------------------------*/
#include "rtklib.h"

/* constants -----------------------------------------------------------------*/

#define L6PREAMB          0x1ACFFC1Du   /* QZSS L6 message preamble */
#define L6BYTELEN                 218   /* QZSS L6 message byte length w/o R-S CODE */
#define L6HEAD_BITLEN              49   /* L6 message header bit length */
#define L6DATA_BITLEN            1695   /* L6 message data part bit length */

#define MIONO_MAX_FRAME            40   /* dependent on Length of Correction Messages (Table 6.3.2-4) */

#define MIONO_MT_COV                1   /* STEC Coverage Message Type */
#define MIONO_MT_COR                2   /* STEC Correction Message Type */

#define MIONO_MAX_SYS               5   /* GNSS ID (Table 6.3.2-8) */
#define MIONO_SYS_GPS               0
#define MIONO_SYS_GLO               1
#define MIONO_SYS_GAL               2
#define MIONO_SYS_BDS               3
#define MIONO_SYS_QZS               4

#define MIONO_INVALID_14BIT     -8192   /* Invalid Value (Table 6.3.2-9) */
#define MIONO_INVALID_12BIT     -2048
#define MIONO_INVALID_10BIT      -512
#define MIONO_INVALID_8BIT       -128

#define MIONO_URA_UNDEF           0.0   /* Note, If undefined, worst case value is assumed. */

#define SQR(x)   ((x)*(x))

typedef struct {
    gtime_t gt;                     /* current Epoch Time 1s */
    int mgfid;                      /* current Msg. Gen. Facility ID */
    uint8_t buff[L6BYTELEN * MIONO_MAX_FRAME]; /* L6D Message */
    int ibuff;                      /* indicate the end of buff */
    int maxframe;                   /* set framelen[first subtype] */
    int frame;                      /* frame counter */
    int tow0;                       /* for Hourly Epoch Time 1s */
    int ep0;                        /* for Hourly Epoch Time 1s regresses check */
    int iod;                        /* IOD SSR */
} miono_t;

miono_t _miono[MIONO_MAX_PRN] = {0};

/* ---------------------------------------------------------------------------*/
static int gnssid2sys(int gnssid)
{
    switch(gnssid) {
        case MIONO_SYS_GPS : return SYS_GPS;
        case MIONO_SYS_GLO : return SYS_GLO;
        case MIONO_SYS_GAL : return SYS_GAL;
        case MIONO_SYS_BDS : return SYS_CMP;
        case MIONO_SYS_QZS : return SYS_QZS;
    }
    return -1;
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

/* decode MADOCA-PPP L6D MT1 STEC coverage message ---------------------------*/
static int decode_miono_coverage(miono_t *mi, miono_region_t *re, int i, int *imax, int *rid)
{
    int j;
    int ep;
    int udi;
    int sync;
    int iod;
    int ralert;
    int corlen;
    int narea;
    int anum;
    int ref[2];
    int span[2];

    ep     = getbitu(mi->buff, i, 20); i+=20; /* GNSS Epoch Time 1s */
    udi    = getbitu(mi->buff, i,  4); i+= 4; /* SSR Update Interval */
    sync   = getbitu(mi->buff, i,  1); i+= 1; /* Multiple Message Indicator */
    iod    = getbitu(mi->buff, i,  4); i+= 4; /* IOD SSR */
    *rid   = getbitu(mi->buff, i,  8); i+= 8; /* Region ID */
    ralert = getbitu(mi->buff, i,  1); i+= 1; /* Region Alert flag */
    corlen = getbitu(mi->buff, i, 16); i+=16; /* Length of Correction Messages */
    narea  = getbitu(mi->buff, i,  5); i+= 5; /* No. of Area */

    trace(4,"decode_miono_coverage: head ep=%d,udi=%d,sync=%d,iod=%d,rid=%d,ralert=%d,corlen=%d,narea=%d\n",
        ep, udi, sync, iod, *rid, ralert, corlen, narea);

    mi->tow0   = ep / 3600 * 3600;
    mi->ep0    = ep % 3600;
    adjweek(&mi->gt, ep);
    mi->iod    = iod;
    re->ralert = ralert;
    re->narea  = narea;
    re->rvalid  = 1;

    for (j = 0; j < narea; j++) {
        anum               = getbitu(mi->buff, i,  5); i+= 5; /* Area Number */
        re->area[anum].sid = getbitu(mi->buff, i,  1); i+= 1; /* Shape ID */
        if (re->area[anum].sid == 0) {
            ref[0]         = getbits(mi->buff, i, 11); i+=11; /* Reference latitude */
            ref[1]         = getbitu(mi->buff, i, 12); i+=12; /* Reference longitude */
            span[0]        = getbitu(mi->buff, i,  8); i+= 8; /* Latitude span */
            span[1]        = getbitu(mi->buff, i,  8); i+= 8; /* Longitude span */
            re->area[anum].ref[0]  = ref[0]  *  0.1;
            re->area[anum].ref[1]  = ref[1]  *  0.1;
            re->area[anum].span[0] = span[0] *  0.1;
            re->area[anum].span[1] = span[1] *  0.1;
        }
        else {
            ref[0]         = getbits(mi->buff, i, 15); i+=15; /* Reference latitude */
            ref[1]         = getbitu(mi->buff, i, 16); i+=16; /* Reference longitude */
            span[0]        = getbitu(mi->buff, i,  8); i+= 8; /* Effective range */
            span[1]        = 0;
            re->area[anum].ref[0]  = ref[0]  *  0.01;
            re->area[anum].ref[1]  = ref[1]  *  0.01;
            re->area[anum].span[0] = span[0] * 10.0;
            re->area[anum].span[1] = 0.0;
        }
        trace(4,"decode_miono_coverage: data %s,i=%4d,rid=%3d,j=%2d,anum=%2d,sid=%d,ref=%5d,%5d,%7.2f,%7.2f,span=%4d,%4d,%6.1f,%6.1f\n",
            time_str(mi->gt,3),i,*rid,j,anum,re->area[anum].sid,
            ref[0] ,ref[1], re->area[anum].ref[0], re->area[anum].ref[1], 
            span[0],span[1],re->area[anum].span[0],re->area[anum].span[1]);
    }
    *imax = i + corlen;
    return i;
}

/* decode MADOCA-PPP L6D MT2 STEC correction message -------------------------*/
static int decode_miono_correction(miono_t *mi, miono_region_t *re, int i, int apply)
{
    int j, k, l;
    int ep;
    int udi;
    int sync;
    int iod;
    int rid;
    int anum;
    int type;
    int ns[MIONO_MAX_SYS];
    int id;
    int sqi;
    int coef[6];
    int offp;
    int sat;
    char satid[8];

    ep     = getbitu(mi->buff, i, 12); i+=12; /* GNSS Hourly Epoch Time 1s */
    udi    = getbitu(mi->buff, i,  4); i+= 4; /* SSR Update Interval */
    sync   = getbitu(mi->buff, i,  1); i+= 1; /* Multiple Message Indicator */
    iod    = getbitu(mi->buff, i,  4); i+= 4; /* IOD SSR */
    rid    = getbitu(mi->buff, i,  8); i+= 8; /* STEC Region ID */
    anum   = getbitu(mi->buff, i,  5); i+= 5; /* STEC Area Number */
    type   = getbitu(mi->buff, i,  2); i+= 2; /* STEC Correction Type */

    if (mi->tow0 < 0) {
        trace(2,"decode_miono_correction: %s,unprocessed coverage\n",
            time_str(mi->gt,3));
        apply=0;
    }
    else {
        if (mi->ep0 > ep) ep += 3600;
        adjweek(&mi->gt, mi->tow0 + ep);
        if (mi->iod != iod) {
            trace(2,"decode_miono_correction: %s,iod mismatch %d != %d\n",
                time_str(mi->gt,3),mi->iod,iod);
            apply=0;
        }
    }
    
    for (j = 0; j < MIONO_MAX_SYS; j++) {
        ns[j]           = getbitu(mi->buff, i,  5); i+= 5; /* No. of Satellites */
    }

    trace(4,"decode_miono_correction: head ep=%d,udi=%d,sync=%d,iod=%d,rid=%d,anum=%d,type=%d,ns=%d,%d,%d,%d,%d\n",
        ep, udi, sync, iod, rid, anum, type, ns[0],ns[1],ns[2],ns[3],ns[4]);

    for (j = 0; j < MIONO_MAX_SYS; j++) {
        for (k = 0; k < ns[j]; k++) {
            for (l = 1; l < 6; l++) coef[l] = 0;
            id          = getbitu(mi->buff, i,  6); i+= 6; /* GNSS Satellite ID */
            sqi         = getbitu(mi->buff, i,  6); i+= 6; /* SSR STEC Quality Indicator */
            coef[0]     = getbits(mi->buff, i, 14); i+=14; /* STEC Polynomial Coefficients C00 */
            if (type >  0) {
                coef[1] = getbits(mi->buff, i, 12); i+=12; /* STEC Polynomial Coefficients C01 */
                coef[2] = getbits(mi->buff, i, 12); i+=12; /* STEC Polynomial Coefficients C10 */
            }
            if (type > 1) {
                coef[3] = getbits(mi->buff, i, 10); i+=10; /* STEC Polynomial Coefficients C11 */
            }
            if (type > 2) {
                coef[4] = getbits(mi->buff, i, 8); i+= 8; /* STEC Polynomial Coefficients C02 */
                coef[5] = getbits(mi->buff, i, 8); i+= 8; /* STEC Polynomial Coefficients C20 */
            }

            offp = (j == MIONO_SYS_QZS)? 192 : 0;
            sat  = satno(gnssid2sys(j),offp+id);
            satno2id(sat, satid);

            trace(4,"decode_miono_correction: data %s,i=%4d,rid=%3d,anum=%2d,type=%d,j=%d,k=%2d,sat=%2d,%3s,sqi=0x%02X,coef=%5d,%5d,%5d,%4d,%4d,%4d,%7.2f,%7.2f,%7.2f,%7.2f,%8.3f,%8.3f\n",
                time_str(mi->gt,3),i,rid,anum,type,j,k,id,satid,sqi,coef[0],coef[1],coef[2],coef[3],coef[4],coef[5],
                coef[0]*0.05,coef[1]*0.02,coef[2]*0.02,coef[3]*0.02,coef[4]*0.005,coef[5]*0.005);

            if (sat == 0) continue;                       /* unsupprted sat */
            if ((coef[0] == MIONO_INVALID_14BIT) ||
               (coef[1] == MIONO_INVALID_12BIT) ||
               (coef[2] == MIONO_INVALID_12BIT) ||
               (coef[3] == MIONO_INVALID_10BIT) ||
               (coef[4] == MIONO_INVALID_8BIT)  ||
               (coef[5] == MIONO_INVALID_8BIT)) continue;/* invalid value */
            
            if (apply) {
                re->area[anum].type               = type;
                re->area[anum].sat[sat-1].t0      = mi->gt;
                re->area[anum].sat[sat-1].sqi     = sqi;
                re->area[anum].sat[sat-1].coef[0] = coef[0] * 0.05;
                re->area[anum].sat[sat-1].coef[1] = coef[1] * 0.02;
                re->area[anum].sat[sat-1].coef[2] = coef[2] * 0.02;
                re->area[anum].sat[sat-1].coef[3] = coef[3] * 0.02;
                re->area[anum].sat[sat-1].coef[4] = coef[4] * 0.005;
                re->area[anum].sat[sat-1].coef[5] = coef[5] * 0.005;
            }
        }
    }
    if (apply)re->area[anum].avalid = 1;
    return i;
}

/* decode QZSS MADOCA ionospheric corrections message --------------------------
* decode QZSS MADOCA ionospheric corrections message and store
* args   : rtcm_t *rtcm IO   rtcm control struct
* return : status (-1: error message, 0: no message,
                   10: input MADOCA-PPP iono. corr. messages)
* note   : before calling the function, init_miono()
*-----------------------------------------------------------------------------*/
extern int decode_qzss_mdciono(rtcm_t *rtcm)
{
    unsigned int corlen;
    unsigned int narea;
    int i=L6HEAD_BITLEN;
    int j;
    int imax;
    int n;
    int ibuff;
    int mn;
    int apply;
    int ret;
    int optprn;
    char *str;
    uint8_t mask;
    qzssl6_t *l6=rtcm->l6info;

    l6->type = QZSS_L6TYPE_MIONO;

    switch(l6->prn) {                       /* ref [1] Table 3-1 */
        case 200 : j=0; break;
        case 201 : j=1; break;
        default  : trace(3,"decode_qzss_mdciono: invalid prn=%d\n",l6->prn); return -1;
    }

    if (l6->alert) {
        if (strstr(rtcm->opt,"-IGNORE_MIONO_ALERT")) {
            trace(3,"decode_qzss_mdciono: ignore alert\n",time_str(_miono[j].gt,3));
        }
        else {
            trace(2,"decode_qzss_mdciono: alert\n",time_str(_miono[j].gt,3));
            return -1;
        }
    }

    if ((str = strstr(rtcm->opt,"-MIONO_PRN"))) {
        optprn = atoi(strstr(str,"=")+1);
        if (optprn != l6->prn) {
            trace(3,"decode_qzss_mdciono: -MIONO_PRN=%d\n",optprn);
            return -1;
        }
    }

    if (l6->si == 1) _miono[j].maxframe = 0;    /* Subframe Indicator : First Data */

    /* frame recognition */
    if (_miono[j].maxframe <= 0) {
        mn = getbitu(rtcm->buff, i, 12); /* Message Number(note:not i++) */

        if (mn == MIONO_MT_COV) {
            corlen = getbitu(rtcm->buff, i+54, 16); /* Length of Correction Messages */
            narea  = getbitu(rtcm->buff, i+70,  5); /* No. of Area */
            _miono[j].maxframe = (75 + narea * 45 + corlen) / L6DATA_BITLEN + 1;
            trace(3,"decode_qzss_mdciono: %s,mn=%d,maxframe=%d,corlen=%d,narea=%d\n",
                time_str(_miono[j].gt,3),mn,_miono[j].maxframe,corlen,narea);
            _miono[j].frame=0;
            _miono[j].ibuff=0;
        } else {
            trace(3,"decode_qzss_mdciono: %s,invalid mn=%d\n",
                time_str(_miono[j].gt,3),mn);
            return 0;
        }
    }

    /* save data part (1695 bits) */
    _miono[j].frame++;
    trace(3,"decode_qzss_mdciono: %s,maxframe=%d,frame=%d\n",
        time_str(_miono[j].gt,3),_miono[j].maxframe,_miono[j].frame);
    switch(_miono[j].frame % 8) {
        case 1: ibuff =   0 + _miono[j].frame / 8; n = 8; mask = 0xFE; break;
        case 2: ibuff = 211 + _miono[j].frame / 8; n = 1; mask = 0xFC; break; 
        case 3: ibuff = 423 + _miono[j].frame / 8; n = 2; mask = 0xF8; break;
        case 4: ibuff = 635 + _miono[j].frame / 8; n = 3; mask = 0xF0; break;
        case 5: ibuff = 847 + _miono[j].frame / 8; n = 4; mask = 0xE0; break;
        case 6: ibuff =1059 + _miono[j].frame / 8; n = 5; mask = 0xC0; break;
        case 7: ibuff =1271 + _miono[j].frame / 8; n = 6; mask = 0x80; break;
        default:ibuff =1483 + _miono[j].frame / 8; n = 7; mask = 0xFF; break;
    }
    _miono[j].buff[ibuff++] |= (unsigned char)getbitu(rtcm->buff, i, n); i += n;

    while (i < L6HEAD_BITLEN + L6DATA_BITLEN) {
        _miono[j].buff[ibuff++] = (unsigned char)getbitu(rtcm->buff, i, 8); i += 8;
    }
    _miono[j].buff[ibuff - 1] &= mask;

    if (_miono[j].frame != _miono[j].maxframe) return 0; /* no message */

    ret=10; /* input MADOCA-PPP iono. messages */
    i=0;
    imax=0;
    while (i < L6DATA_BITLEN * _miono[j].maxframe) {
        mn = getbitu(_miono[j].buff,i,12); i+=12; /* Message Number */
        i+= 4;                                    /* Message Sub Type ID(skip) */
        if (mn == MIONO_MT_COV) {
            if (_miono[j].mgfid != l6->mgfid) {
                trace(2,"decode_qzss_mdciono: %s,mgfid switched %d -> %d\n",
                    time_str(_miono[j].gt,3),_miono[j].mgfid,l6->mgfid);
                _miono[j].mgfid = l6->mgfid;
            }
            corlen = getbitu(_miono[j].buff, i+38, 16); /* Length of Correction Messages */
            i = decode_miono_coverage(&_miono[j], &l6->re, i, &imax, &l6->rid);
            trace(5,"decode_qzss_mdciono: i=%d,imax=%d,corlen=%d\n",i,imax,corlen);
        }
        else if (mn == MIONO_MT_COR) {
            apply = 1;
            /* Note, ref[1] 4.2.1(3) ID 0 and 2 are the same, 1 and 3 are the same. */
            if ((_miono[j].mgfid & 0x1) != (l6->mgfid & 0x1)) {
                trace(2,"decode_qzss_mdciono: %s,mgfid mismatch %d != %d\n",
                    time_str(_miono[j].gt,3),_miono[j].mgfid,l6->mgfid);
                apply = 0;
            }
            i = decode_miono_correction(&_miono[j], &l6->re, i, apply);
        } else {
            trace(2,"decode_qzss_mdciono: invalid mn=%d\n",mn);
            ret = 0;
            break;
        }
        rtcm->time = _miono[j].gt;
        if (i >= imax) break;
    }
    _miono[j].maxframe = 0;
    trace(4,"decode_qzss_mdciono: %s,frames=%d\n",
        time_str(_miono[j].gt,3),_miono[j].frame);
    return ret; 
}

/* initialize MADOCA-PPP wide area ionospheric correction control --------------
* initialize MADOCA-PPP wide area ionospheric correction control struct
* args   : gtime_t *gt   I    GPST for week number determination
* return : none
*-----------------------------------------------------------------------------*/
extern void init_miono(const gtime_t gt)
{
    int i;

    for (i = 0; i < MIONO_MAX_PRN; i++) {
        _miono[i].gt    = gt; /* for week number determination */
        _miono[i].mgfid = -1; /* invalid value */
        _miono[i].tow0  = -1; /* invalid value */
        _miono[i].frame =  0; /* initial value */
    }
}

/* standard deviation by STEC URA --------------------------------------------*/
static double miono_std_stecura(int ura)
{
    /* ref [1] 6.3.2.3 (5) */
    if (ura<= 0) return 5.4665;     /* STEC URA undefined/unknown */
    if (ura>=63) return 5.4665;
    return (pow(3.0,(ura>>3)&7)*(1.0+(ura&7)/4.0)-1.0)*1E-3;
}

/* L1 ionosphric delay -------------------------------------------------------*/
static double miono_delay(const nav_t *nav, const int sat, const miono_area_t *a,
                          const double *ll)
{
    const double *c=a->sat[sat-1].coef;
    double freq;
    double fact;
    double stec;
    double delay;

    freq = sat2freq(sat, CODE_L1C, nav);

    /* ref [1] 6.3.2.3 (5) */
    fact = 40.31E16 / freq / freq;  /* TECU -> L1 ionospheric delay(m) */
    stec = c[0];                    /* STEC poly.coef.{C00,C01,C10,C11,C02,C20} */
    if (a->type >= 1) stec += c[1] *    (ll[0] - a->ref[0]) + c[2] *    (ll[1] - a->ref[1]);
    if (a->type >= 2) stec += c[3] *    (ll[0] - a->ref[0])        *    (ll[1] - a->ref[1]);
    if (a->type >= 3) stec += c[4] * SQR(ll[0] - a->ref[0]) + c[2] * SQR(ll[1] - a->ref[1]);

    delay = fact * stec;
    trace(5,"miono_delay: sat=%d,fact=%5.3f,stec=%8.3f,delay=%7.3f,coef=%7.2f,%7.2f,%7.2f,%7.2f,%8.3f,%8.3f\n",
        sat,fact,stec,delay,c[0],c[1],c[2],c[3],c[4],c[5]);
    return delay;
}

/* select MADOCA-PPP ionospheric correction area -----------------------------*/
static miono_area_t *miono_sel_area(miono_data_t *miono, const double *rr,
                                    const double *ll)
{
    const miono_region_t *re;
    int i,j,k;
    int min_i=-1;
    int min_j=0;
    double ref_ecef[3];
    double ref_llh[3]={0};
    double diff[3];
    double dist;
    double min_dist=1E5;

    trace(3,"miono_sel_area: lat=%.2f,lon=%.2f\n", ll[0],ll[1]);

    for (i = 0; i < MIONO_MAX_RID; i++) {
        re = &miono->re[i];
        if (!re->rvalid) continue;
        trace(5,"miono_sel_area: region i=%d,ralert=%d\n",i,re->ralert);
        if (re->ralert) continue;

        for (j = 0; j < MIONO_MAX_ANUM; j++) {
            if (!re->area[j].avalid) continue;
            for (k = 0; k < 2; k++) ref_llh[k] = re->area[j].ref[k] * D2R; /* deg -> rad */
            pos2ecef(ref_llh, ref_ecef);
            for (k = 0; k < 3; k++) diff[k] = rr[k] - ref_ecef[k];
            dist=norm(diff, 3) / 1E3;  /* m -> km */
            trace(5,"miono_sel_area: area i=%d,j=%d,sid=%d,ref=%6.2f,%7.2f,dist=%10.3f,min_dist=%10.3f\n",
                i,j,re->area[j].sid, re->area[j].ref[0], re->area[j].ref[1], dist, min_dist);
            if (dist > min_dist) continue;

            if (re->area[j].sid == 0) { /* rectangle */
                if ((ll[0] < (re->area[j].ref[0] - re->area[j].span[0])) ||
                   (ll[0] > (re->area[j].ref[0] + re->area[j].span[0])) ||
                   (ll[1] < (re->area[j].ref[1] - re->area[j].span[1])) ||
                   (ll[1] > (re->area[j].ref[1] + re->area[j].span[1]))) continue;
            }
            else {                     /* circle */
                if (dist > re->area[j].span[0]) continue;
            }
            trace(3,"miono_sel_area: closest RegionID=%d,AreaNo=%d,dist=%.3f\n", i, j, dist);
            min_i = i; min_j = j; min_dist = dist;
        }
    }

    if (min_i == -1) return NULL;
    miono->rid  = min_i;
    miono->anum = min_j;
    return &miono->re[min_i].area[min_j];
}

/* get MADOCA-PPP ionospheric correction data ----------------------------------
* get MADOCA-PPP ionospheric correction data using the user position
* args   : double  *rr     I   user postion(ECEF)
*          nav_t   *nav    O   ionospheric correction data
* return : status (0: not updated, 1: updated)
*-----------------------------------------------------------------------------*/
extern int miono_get_corr(const double *rr, nav_t *nav)
{
    miono_area_t *area;
    int i;
    double pos[3];
    double latlon[2];

    trace(4,"miono_get_corr: time=%d,rr=%.3f,%.3f,%.3f\n",
        (int)_miono[0].gt.time,rr[0],rr[1],rr[2]);

    if (_miono[0].gt.time == 0) return 0;
    if ((rr[0] == 0.0) && (rr[1] == 0.0) && (rr[2] == 0.0)) return 0;

    ecef2pos(rr, pos);
    latlon[0]=pos[0]*R2D;
    latlon[1]=pos[1]*R2D;

    if (!(area = miono_sel_area(&nav->miono, rr, latlon))) return 0;

    for (i = 0; i < MAXSAT; i++) {
        if (!area->sat[i].t0.time) {
            nav->miono.corr.t0[i].time = 0;
            continue;
        }
        nav->miono.corr.t0[i]  = area->sat[i].t0;
        nav->miono.corr.dly[i] = miono_delay(nav, i+1, area, latlon);
        nav->miono.corr.std[i] = miono_std_stecura(area->sat[i].sqi);
    }
    return 1;
}
