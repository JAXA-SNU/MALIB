/*------------------------------------------------------------------------------
* ppp.c : precise point positioning
*
* Copyright (C) 2024-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
* Copyright (C) 2010-2020 by T.TAKASU, All rights reserved.
*
* options : -DIERS_MODEL  use IERS tide model
*           -DOUTSTAT_AMB output ambiguity parameters to solution status
*
* references :
*    [1] D.D.McCarthy, IERS Technical Note 21, IERS Conventions 1996, July 1996
*    [2] D.D.McCarthy and G.Petit, IERS Technical Note 32, IERS Conventions
*        2003, November 2003
*    [3] D.A.Vallado, Fundamentals of Astrodynamics and Applications 2nd ed,
*        Space Technology Library, 2004
*    [4] J.Kouba, A Guide to using International GNSS Service (IGS) products,
*        May 2009
*    [5] RTCM Paper, April 12, 2010, Proposed SSR Messages for SV Orbit Clock,
*        Code Biases, URA
*    [6] MacMillan et al., Atmospheric gradients and the VLBI terrestrial and
*        celestial reference frames, Geophys. Res. Let., 1997
*    [7] G.Petit and B.Luzum (eds), IERS Technical Note No. 36, IERS
*         Conventions (2010), 2010
*    [8] J.Kouba, A simplified yaw-attitude model for eclipsing GPS satellites,
*        GPS Solutions, 13:1-12, 2009
*    [9] F.Dilssner, GPS IIF-1 satellite antenna phase center and attitude
*        modeling, InsideGNSS, September, 2010
*    [10] F.Dilssner, The GLONASS-M satellite yaw-attitude model, Advances in
*        Space Research, 2010
*    [11] IGS MGEX (http://igs.org/mgex)
*    [12] CAO IS-QZSS-MDC-004, May, 2025
*
* history : 2010/07/20 1.0  new
*                           added api:
*                               tidedisp()
*           2010/12/11 1.1  enable exclusion of eclipsing satellite
*           2012/02/01 1.2  add gps-glonass h/w bias correction
*                           move windupcorr() to rtkcmn.c
*           2013/03/11 1.3  add otl and pole tides corrections
*                           involve iers model with -DIERS_MODEL
*                           change initial variances
*                           suppress acos domain error
*           2013/09/01 1.4  pole tide model by iers 2010
*                           add mode of ionosphere model off
*           2014/05/23 1.5  add output of trop gradient in solution status
*           2014/10/13 1.6  fix bug on P0(a[3]) computation in tide_oload()
*                           fix bug on m2 computation in tide_pole()
*           2015/03/19 1.7  fix bug on ionosphere correction for GLO and BDS
*           2015/05/10 1.8  add function to detect slip by MW-LC jump
*                           fix ppp solution problem with large clock variance
*           2015/06/08 1.9  add precise satellite yaw-models
*                           cope with day-boundary problem of satellite clock
*           2015/07/31 1.10 fix bug on nan-solution without glonass nav-data
*                           pppoutsolsat() -> pppoutstat()
*           2015/11/13 1.11 add L5-receiver-dcb estimation
*                           merge post-residual validation by rnx2rtkp_test
*                           support support option opt->pppopt=-GAP_RESION=nnnn
*           2016/01/22 1.12 delete support for yaw-model bug
*                           add support for ura of ephemeris
*           2018/10/10 1.13 support api change of satexclude()
*           2020/11/30 1.14 use sat2freq() to get carrier frequency
*                           use E1-E5b for Galileo iono-free LC
*           2021/01/04 1.15 fixed divide-by-zero exception in udbias_ppp()
*           2024/02/01 1.16 branch from ver.2.4.3b35 for MALIB
*                           add function to support constrains to local correction
*                           change for suppress -Wstringop-overflow warnings.
*           2024/08/02 1.17 add sat elevation argument for pppcorr_stec()
*           2024/09/26 1.18 support output DOP to stat file
*                           fix bug on sol.dtr update in update_stat()
*           2024/12/20 1.19 support gnss atttude model
*                           support Bias-SINEX and FCB files correction
*                           add reciver code bias correction
*                           delete L5-receiver-dcb estimation
*                           add BeiDou-2 ISB estimation
*           2025/02/06 1.20 fix bug sat_yaw()
*                           update local correction
*                           change the sign of the code/phase bias correction 
*                           in corr_meas()
*           2025/03/24 1.21 support ins navigation
*           2025/12/26 1.22 support MADOCA-PPP ionosphere corrections
*                           support substitute ppp options
*                           move select code/phase bias function from rtkpos.c
*                           code/phase bias flags for discontinuity
*                           add code/phase bias discontinuity detection
*                           fix stationary PPP state anomaly in udpos_ppp_dyna()
*                           add accuracy check and reinit in udpos_ppp_ins()
*                           add dead reckoning function
*                           add azelmask/exclsats_ep in ppp_res()
*                           add output PPP kalman filter state/covariance
*           2026/01/23 1.23 init obs if bias invalid and MADOCA-PPP ionosphere 
*                            corrections enabled in corr_meas()
*           2026/02/27 1.24 support triple/quad-frequency PPP
*                           support BeiDou-2 and BeiDou-3
*                           change function for SNR weighting to variance
*                           add phase bias update using BIA for udsatpb()
*                           add MADOCA-PPP CSSR / RTCM3 branching for 
*                            code/phase bias selection
*                           change VAR_ZTD from SQR(0.6) to SQR(0.12)
*           2026/03/26 1.25 fix bug on antenna index handling
*                           fix bug on state covariance output
*-----------------------------------------------------------------------------*/
#include <float.h>
#include <limits.h>
#include "rtklib.h"

#define SQR(x)      ((x)*(x))
#define SQRT(x)     ((x)<=0.0||(x)!=(x)?0.0:sqrt(x))
#define MAX(x,y)    ((x)>(y)?(x):(y))
#define MIN(x,y)    ((x)<(y)?(x):(y))
#define ROUND(x)    (int)floor((x)+0.5)

#define SOLQ_FIX_WL 99              /* solution status: wide-lane fixed */

#define MAX_ITER    8               /* max number of iterations */
#define MAX_STD_FIX 0.15            /* max std-dev (3d) to fix solution */
#define MIN_NSAT_SOL 4              /* min satellite number for solution */
#define THRES_REJECT 4.0            /* reject threshold of posfit-res (sigma) */

#define THRES_MW_JUMP 10.0

#define VAR_POS     SQR(60.0)       /* init variance receiver position (m^2) */
#define VAR_VEL     SQR(10.0)       /* init variance of receiver vel ((m/s)^2) */
#define VAR_ACC     SQR(10.0)       /* init variance of receiver acc ((m/ss)^2) */
#define VAR_CLK     SQR(60.0)       /* init variance receiver clock (m^2) */
#define VAR_ZTD     SQR(0.12)       /* init variance ztd (m^2) */
#define VAR_GRA     SQR(0.01)       /* init variance gradient (m^2) */
#define VAR_IFB     SQR(30.0)       /* init variance ifb (m^2) */
#define VAR_BDSB    SQR(30.0)       /* init variance BeiDou-2 bias (m^2) */
#define VAR_BIAS    SQR(60.0)       /* init variance phase-bias (m^2) */
#define VAR_IONO    SQR(60.0)       /* init variance iono-delay */
#define VAR_GLO_IFB SQR( 0.6)       /* variance of glonass ifb */

#define ERR_SAAS    0.3             /* saastamoinen model error std (m) */
#define ERR_BRDCI   0.5             /* broadcast iono model error factor */
#define REL_HUMI    0.7             /* relative humidity for saastamoinen model */
#define GAP_RESION  120             /* default gap to reset ionos parameters (ep) */

#define IONO_REJ_STD     1.0        /* reject   STEC corr. std (m) */
#define IONO_COV_RATIO   1.0        /* ratio of STEC corr. cov (m) */
#define IONO_THRE_H      2.0        /* Horizontal convergence threshold (m) */
#define IONO_THRE_V      3.0        /* Vertical   convergence threshold (m) */

#define THRESH_VEL       0.1        /* threshold for velocity to detect static mode (m/s) */
#define THRESH_ACC       0.01       /* threshold for acceleration to detect static mode (m/s^2) */

#define KF_OPEN          0          /* kf file open flag*/
#define KF_CLOSE         1          /* kf file close flag */

/* number and index of states */
#define NF(opt)     ((opt)->ionoopt==IONOOPT_IFLC?1:(opt)->nf)
/* Number of state variables related to position 
* nominal  : position{x,y,z}
* dynamics : position{x,y,z}, velocity{x,y,z}, acceleration{x,y,z}
* ins      : delta position{x,y,z}, delta velocity{x,y,z}, 
*            delta attitude{roll,pitch,heading}, delta accelerometer bias{x,y,z},
*            delta gyro bias{roll,pitch,heading}, clock drift */
#define NP(opt)     ((opt)->dynamics==DY_DYNA?9:((opt)->dynamics==DY_INS?16:3))
#define NC(opt)     (CLKNSYS)
#define NT(opt)     ((opt)->tropopt<TROPOPT_EST?0:((opt)->tropopt==TROPOPT_EST?1:3))
#define NI(opt)     ((opt)->ionoopt==IONOOPT_EST?MAXSAT:0)
#define ND(opt)     ((opt)->navsys&SYS_CMP?3:0)
#define N3(opt)     (NF(opt)>=3?NC(opt):0)   /* 3rd-frequency receivre bias */
#define N4(opt)     (NF(opt)>=4?NC(opt):0)   /* 4th-frequency receivre bias */
#define NR(opt)     (NP(opt)+NC(opt)+NT(opt)+NI(opt)+ND(opt)+N3(opt)+N4(opt))
#define NB(opt)     (NF(opt)*MAXSAT)
#define NX(opt)     (NR(opt)+NB(opt))
#define IC(s,opt)   (NP(opt)+(s))
#define IT(opt)     (NP(opt)+NC(opt))
#define II(s,opt)   (NP(opt)+NC(opt)+NT(opt)+(s)-1)
#define ID(t,opt)   (NP(opt)+NC(opt)+NT(opt)+NI(opt)+t)
#define I3(s,opt)   (NP(opt)+NC(opt)+NT(opt)+NI(opt)+ND(opt)+(s))
#define I4(s,opt)   (NP(opt)+NC(opt)+NT(opt)+NI(opt)+ND(opt)+N3(opt)+(s))
#define IB(s,f,opt) (NR(opt)+MAXSAT*(f)+(s)-1)

/* BeiDou-2 satellite type table ---------------------------------------------*/
static const char *bds2_sat_type[3]={
    "BEIDOU-2G","BEIDOU-2I","BEIDOU-2M"
};
/* standard deviation of state -----------------------------------------------*/
static double STD(rtk_t *rtk, int i)
{
    if (rtk->sol.stat==SOLQ_FIX) return SQRT(rtk->Pa[i+i*rtk->nx]);
    return SQRT(rtk->P[i+i*rtk->nx]);
}
/* write solution status for PPP ---------------------------------------------*/
extern int pppoutstat(rtk_t *rtk, char *buff)
{
    ssat_t *ssat;
    double tow,pos[3],vel[3],acc[3],*x;
    int i,j,week,ssr,sub;
    char id[32],*p=buff;
    int ind,sys[5]={SYS_GLO,SYS_GAL,SYS_QZS,SYS_CMP,SYS_BD2};
    double dtr[5+3]={0.0},qdtr[5+3]={0.0},rb[3]={0.0};
    int ns;
    double azels[MAXOBS*2],dop[4];

    if (!rtk->sol.stat) return 0;
    
    trace(3,"pppoutstat:\n");
    
    tow=time2gpst(rtk->sol.time,&week);
    
    x=rtk->sol.stat==SOLQ_FIX?rtk->xa:rtk->x;
    
    /* receiver position */
    p+=sprintf(p,"$POS,%d,%.3f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",week,tow,
            rtk->sol.stat,rtk->sol.rr[0],rtk->sol.rr[1],rtk->sol.rr[2],
            STD(rtk,0),STD(rtk,1),STD(rtk,2));
    
    /* receiver velocity and acceleration */
    if (rtk->opt.dynamics==DY_DYNA) {
        ecef2pos(rtk->sol.rr,pos);
        ecef2enu(pos,rtk->x+3,vel);
        ecef2enu(pos,rtk->x+6,acc);
        p+=sprintf(p,"$VELACC,%d,%.3f,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.4f,%.4f,"
                   "%.4f,%.5f,%.5f,%.5f\n",week,tow,rtk->sol.stat,vel[0],vel[1],
                   vel[2],acc[0],acc[1],acc[2],0.0,0.0,0.0,0.0,0.0,0.0);
    }
    /* ins velocity, acceleration, attitude, gyro and clock drift */
    if (rtk->opt.dynamics==DY_INS&& rtk->insd.insstat==INS_ACT) {
        ecef2pos(rtk->sol.rr,pos);
        /* receiver velocity */
        p+=sprintf(p,"$VEL,%d,%.3f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", 
                   week,tow,rtk->sol.stat,
                   rtk->sol.rr[3],rtk->sol.rr[4],rtk->sol.rr[5], 
                   STD(rtk,3),STD(rtk,4),STD(rtk,5));
        /* attitude */
        ecef2body(pos,rtk->insd.curb2e,rb);
        p+=sprintf(p,"$ATTE,%d,%.3f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", 
                   week, tow, rtk->sol.stat, rb[0]*R2D, rb[1]*R2D, rb[2]*R2D, 
                   STD(rtk,6), STD(rtk,7), STD(rtk,8));
        /* receiver acceleration */
        p+=sprintf(p,"$ACCB,%d,%.3f,%d,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n", 
            week, tow, rtk->sol.stat, x[9], x[10], x[11], 
            STD(rtk,9), STD(rtk,10), STD(rtk,11));
        /* gyro */
        p+=sprintf(p,"$GYROB,%d,%.3f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", 
            week, tow, rtk->sol.stat, x[12]*R2D, x[13]*R2D, x[14]*R2D, 
            STD(rtk,12), STD(rtk,13), STD(rtk,14));
        /* clock drift */
        p+=sprintf(p,"$CLKD,%d,%.3f,%d,%.4f,%.4f\n", 
            week, tow, rtk->sol.stat, x[15], STD(rtk,15));
    }
    /* receiver clocks */
    i=IC(0,&rtk->opt);
    p+=sprintf(p,"$CLK,%d,%.3f,%d,%d,%.3f,%.3f,%.3f,%.3f\n",
               week,tow,rtk->sol.stat,1,x[i]*M2NS,x[i+1]*M2NS,
               STD(rtk,i)*M2NS,STD(rtk,i+1)*M2NS);
    /* receiver ISBs */
    for (i=0;i<5;i++) {
        ind=IC(sys2pppsys(sys[i]),&rtk->opt);
        if ((sys[i]&rtk->opt.navsys)) {
            dtr[i]=(x[ind]-x[IC(0,&rtk->opt)])*M2NS;
            qdtr[i]=STD(rtk,ind)*M2NS;
        }
    }
    /* BeiDou2 Bias */
    if ((rtk->opt.navsys&SYS_CMP) && (rtk->opt.bds2bias == 1)) {
        ind=ID(0,&rtk->opt);
        for (i=0;i<3;i++) {
            dtr[5+i]=dtr[3]+x[ind+i]*M2NS;
            qdtr[5+i]=STD(rtk,ind+i)*M2NS;
        }
    }
    p+=sprintf(p,"$ISB,%d,%.3f,%d,",week,tow,rtk->sol.stat);
    p+=sprintf(p,"%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,",dtr[0],dtr[1],dtr[2],dtr[3],dtr[4],dtr[5],dtr[6],dtr[7]);
    p+=sprintf(p,"%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",qdtr[0],qdtr[1],qdtr[2],qdtr[3],qdtr[4],qdtr[5],qdtr[6],qdtr[7]);
    
    /* tropospheric parameters */
    if (rtk->opt.tropopt==TROPOPT_EST||rtk->opt.tropopt==TROPOPT_ESTG) {
        i=IT(&rtk->opt);
        p+=sprintf(p,"$TROP,%d,%.3f,%d,%d,%.4f,%.4f\n",week,tow,rtk->sol.stat,
                   1,x[i],STD(rtk,i));
    }
    if (rtk->opt.tropopt==TROPOPT_ESTG) {
        i=IT(&rtk->opt);
        p+=sprintf(p,"$TRPG,%d,%.3f,%d,%d,%.5f,%.5f,%.5f,%.5f\n",week,tow,
                   rtk->sol.stat,1,x[i+1],x[i+2],STD(rtk,i+1),STD(rtk,i+2));
    }
    /* DOP */
    for (i=ns=0;i<MAXSAT;i++) {
        ssat=rtk->ssat+i;
        if (!ssat->vsat[0]) continue;
        azels[  ns*2]=ssat->azel[0];
        azels[1+ns*2]=ssat->azel[1];
        ns++;
    }
    dops(ns,azels,rtk->opt.elmin,dop);
    p+=sprintf(p,"$DOP,%d,%.3f,%d,%d,%.1f,%.1f,%.1f,%.1f\n",week,tow,
               rtk->sol.stat,ns,dop[0],dop[1],dop[2],dop[3]);
    /* substitute PPP status */
    switch(rtk->sol.sub) {
        case SUBPPP_BRDC:   ssr=1; sub=SUBPPP_BRDC;  break;
        case SUBPPP_RT:     ssr=1; sub=SUBPPP_RT;    break;
        case SUBPPP_DR:     ssr=0; sub=SUBPPP_DR;  break;
        default:            ssr=0; sub=SUBPPP_OFF;
    }
    p+=sprintf(p,"$SUB,%d,%.3f,%d,%d,%d\n",
            week,tow,rtk->sol.stat,ssr,sub);
    /* ionosphere parameters */
    if (rtk->opt.ionoopt==IONOOPT_EST) {
        for (i=0;i<MAXSAT;i++) {
            ssat=rtk->ssat+i;
            if (!ssat->vs) continue;
            j=II(i+1,&rtk->opt);
            if (rtk->x[j]==0.0) continue;
            satno2id(i+1,id);
            p+=sprintf(p,"$ION,%d,%.3f,%d,%s,%.1f,%.1f,%.4f,%.4f\n",week,tow,
                       rtk->sol.stat,id,rtk->ssat[i].azel[0]*R2D,
                       rtk->ssat[i].azel[1]*R2D,x[j],STD(rtk,j));
        }
    }
#ifdef OUTSTAT_AMB
    /* ambiguity parameters */
    for (i=0;i<MAXSAT;i++) for (j=0;j<NF(&rtk->opt);j++) {
        k=IB(i+1,j,&rtk->opt);
        if (rtk->x[k]==0.0) continue;
        satno2id(i+1,id);
        p+=sprintf(p,"$AMB,%d,%.3f,%d,%s,%d,%.4f,%.4f\n",week,tow,
                   rtk->sol.stat,id,j+1,x[k],STD(rtk,k));
    }
#endif
    return (int)(p-buff);
}
/* exclude meas of eclipsing satellite (block IIA) ---------------------------*/
static void testeclipse(const obsd_t *obs, int n, const nav_t *nav, double *rs)
{
    double rsun[3],esun[3],r,ang,erpv[5]={0},cosa;
    int i,j;
    const char *type;
    
    trace(3,"testeclipse:\n");
    
    /* unit vector of sun direction (ecef) */
    sunmoonpos(gpst2utc(obs[0].time),erpv,rsun,NULL,NULL);
    normv3(rsun,esun);
    
    for (i=0;i<n;i++) {
        type=nav->pcvs[obs[i].sat-1].type;
        
        if ((r=norm(rs+i*6,3))<=0.0) continue;
        
        /* only block IIA */
        if (*type&&!strstr(type,"BLOCK IIA")) continue;
        
        /* sun-earth-satellite angle */
        cosa=dot(rs+i*6,esun,3)/r;
        cosa=cosa<-1.0?-1.0:(cosa>1.0?1.0:cosa);
        ang=acos(cosa);
        
        /* test eclipse */
        if (ang<PI/2.0||r*sin(ang)>RE_WGS84) continue;
        
        trace(3,"eclipsing sat excluded %s sat=%2d\n",time_str(obs[0].time,0),
              obs[i].sat);
        
        for (j=0;j<3;j++) rs[j+i*6]=0.0;
    }
}
/* nominal yaw-angle ---------------------------------------------------------*/
static double yaw_nominal(double beta, double mu)
{
    if (fabs(beta)<1E-12&&fabs(mu)<1E-12) return PI;
    return atan2(-tan(beta),sin(mu))+PI;
}
/* yaw-angle of satellite ----------------------------------------------------*/
extern int yaw_angle(int sat, const char *type, int opt, double beta, double mu,
                     double *yaw)
{
    *yaw=yaw_nominal(beta,mu);
    return 1;
}
/* satellite attitude model --------------------------------------------------*/
static int sat_yaw(gtime_t time, int sat, const char *type, int opt,
                   const double *rs, double *exs, double *eys)
{
    double rsun[3],ri[6],es[3],esun[3],n[3],p[3],en[3],ep[3],ex[3],E,beta,mu;
    double yaw,cosy,siny,erpv[5]={0};
    int i;
    gtime_t tutc;
    
    tutc=gpst2utc(time);

    sunmoonpos(tutc,erpv,rsun,NULL,NULL);
    
    /* beta and orbit angle */
    matcpy(ri,rs,6,1);
    ri[3]-=OMGE*ri[1];
    ri[4]+=OMGE*ri[0];
    cross3(ri,ri+3,n);
    cross3(rsun,n,p);
    if (!normv3(rs,es)||!normv3(rsun,esun)||!normv3(n,en)||
        !normv3(p,ep)) return 0;
    beta=PI/2.0-acos(dot(esun,en,3));
    E=acos(dot(es,ep,3));
    mu=PI/2.0+(dot(es,esun,3)<=0?-E:E);
    if      (mu<-PI/2.0) mu+=2.0*PI;
    else if (mu>=PI/2.0) mu-=2.0*PI;
    
    /* yaw-angle of satellite */
    if (!yaw_angle(sat,type,opt,beta,mu,&yaw)) return 0;

    /* satellite fixed x,y-vector */
    cross3(en,es,ex);
    cosy=cos(yaw);
    siny=sin(yaw);
    for (i=0;i<3;i++) {
        exs[i]=-siny*en[i]+cosy*ex[i];
        eys[i]=-cosy*en[i]-siny*ex[i];
    }
    return 1;
}
/* phase windup model --------------------------------------------------------*/
static int model_phw(gtime_t time, int sat, const char *type, int opt,
                     const double *rs, const double *rr, double *phw)
{
    double exs[3],eys[3],ek[3],exr[3],eyr[3],eks[3],ekr[3],E[9];
    double dr[3],ds[3],drs[3],r[3],pos[3],cosp,ph;
    int i;
    
    if (opt<=0) return 1; /* no phase windup */
    
    /* satellite yaw attitude model */
    if (!sat_yaw(time,sat,type,opt,rs,exs,eys)) return 0;
    
    /* unit vector satellite to receiver */
    for (i=0;i<3;i++) r[i]=rr[i]-rs[i];
    if (!normv3(r,ek)) return 0;
    
    /* unit vectors of receiver antenna */
    ecef2pos(rr,pos);
    xyz2enu(pos,E);
    exr[0]= E[1]; exr[1]= E[4]; exr[2]= E[7]; /* x = north */
    eyr[0]=-E[0]; eyr[1]=-E[3]; eyr[2]=-E[6]; /* y = west  */
    
    /* phase windup effect */
    cross3(ek,eys,eks);
    cross3(ek,eyr,ekr);
    for (i=0;i<3;i++) {
        ds[i]=exs[i]-ek[i]*dot(ek,exs,3)-eks[i];
        dr[i]=exr[i]-ek[i]*dot(ek,exr,3)+ekr[i];
    }
    cosp=dot(ds,dr,3)/norm(ds,3)/norm(dr,3);
    if      (cosp<-1.0) cosp=-1.0;
    else if (cosp> 1.0) cosp= 1.0;
    ph=acos(cosp)/2.0/PI;
    cross3(ds,dr,drs);
    if (dot(ek,drs,3)<0.0) ph=-ph;
    
    *phw=ph+floor(*phw-ph+0.5); /* in cycle */
    return 1;
}
/* measurement error variance ------------------------------------------------*/
static double varerr(int sat, int sys, double el, int idx, int type,
                     const prcopt_t *opt)
{
    double fact=1.0,sinel=sin(el);
    
    if (type==1) fact*=opt->eratio[idx==0?0:1];
    fact*=sys==SYS_GLO?EFACT_GLO:(sys==SYS_SBS?EFACT_SBS:EFACT_GPS);
    
    if (opt->ionoopt==IONOOPT_IFLC) fact*=3.0;
    return SQR(fact*opt->err[1])+SQR(fact*opt->err[2]/sinel);
}
/* rate measurement error variance -------------------------------------------*/
static double varerr_rate(int sys, double el, double freq, const prcopt_t *opt,
                          double factor)
{
    double fact=1.0,sinel=sin(el);

    fact*=sys==SYS_GLO?EFACT_GLO:(sys==SYS_SBS?EFACT_SBS:EFACT_GPS);
    return SQR(fact*(opt->err[4]*CLIGHT/freq))+
           SQR(fact*((factor*opt->err[4])*CLIGHT/freq)/sinel);
}
/* initialize state and covariance -------------------------------------------*/
static void initxp(double *x, double *P, int nx, double xi, double var, int i)
{
    int j;
    x[i]=xi;
    for (j=0;j<nx;j++) {
        P[i+j*nx]=P[j+i*nx]=i==j?var:0.0;
    }
}
/* initialize state and covariance using rtk struct --------------------------*/
static void initx(rtk_t *rtk, double xi, double var, int i)
{
    initxp(rtk->x,rtk->P,rtk->nx,xi,var,i);
}
/* geometry-free phase measurement -------------------------------------------*/
static double gfmeas(const obsd_t *obs, const nav_t *nav, int f)
{
    double freq1,freq2;

    freq1=sat2freq(obs->sat,obs->code[0],nav);
    freq2=sat2freq(obs->sat,obs->code[f],nav);
    if (freq1==0.0||freq2==0.0||obs->L[0]==0.0||obs->L[f]==0.0) return 0.0;
    return (obs->L[0]/freq1-obs->L[f]/freq2)*CLIGHT;
}
/* Melbourne-Wubbena linear combination --------------------------------------*/
static double mwmeas(const obsd_t *obs, const nav_t *nav, int f)
{
    double freq1,freq2;

    freq1=sat2freq(obs->sat,obs->code[0],nav);
    freq2=sat2freq(obs->sat,obs->code[f],nav);
    
    if (freq1==0.0||freq2==0.0||obs->L[0]==0.0||obs->L[f]==0.0||
        obs->P[0]==0.0||obs->P[f]==0.0) return 0.0;
    return (obs->L[0]-obs->L[f])*CLIGHT/(freq1-freq2)-
           (freq1*obs->P[0]+freq2*obs->P[f])/(freq1+freq2);
}
/* antenna corrected measurements --------------------------------------------*/
static void corr_meas(const obsd_t *obs, const nav_t *nav, const double *azel,
                      const prcopt_t *opt, const double *dantr,
                      const double *dants, double phw, double *L, double *P,
                      double *Lc, double *Pc)
{
    char satid[8],*tstr=time_str(obs->time, 3);
    double freq[NFREQ]={0},C1,C2;
    int i;
    int sys,idx,freq_num,fidx;
    
    sys=satsys(obs->sat,NULL);
    satno2id(obs->sat, satid);
    for (i=0;i<opt->nf&&i<NFREQ;i++) {
        L[i]=P[i]=0.0;
        freq[i]=sat2freq(obs->sat,obs->code[i],nav);
        if (freq[i]==0.0||obs->L[i]==0.0||obs->P[i]==0.0) continue;
        freq_num=code2freq_num(obs->code[i]);
        if (testsnr(0,freq_num,azel[1],obs->SNR[i]*SNR_UNIT,&opt->snrmask)) continue;
        
        /* antenna index */
        fidx=code2idx(satsys_bd2(obs->sat,NULL,nav->pcvs[obs->sat-1].type),obs->code[i]);
        idx=freq_idx2ant_idx(satsys_bd2(obs->sat,NULL,nav->pcvs[obs->sat-1].type),fidx);
        if (idx<0) continue;

        /* antenna phase center and phase windup correction */
        L[i]=obs->L[i]*CLIGHT/freq[i]-dants[idx]-dantr[idx]-phw*CLIGHT/freq[i];
        P[i]=obs->P[i]-dants[idx]-dantr[idx];
        trace(3,"corr_meas %s %s i=%d idx=%d code=%d dants=%.3f dantr=%.3f\n",
              tstr,satid,i,idx,obs->code[i],dants[idx],dantr[idx]);
        /* satellite bias correction */
        if (nav->osb.vscb[obs->sat-1][obs->code[i]-1]!=BIAS_INVALID) {
            P[i]+=nav->osb.scb[obs->sat-1][obs->code[i]-1];
            trace(3,"corr_meas satellite code bias %s %s obscode=%2d bias=%7.3f\n",
                tstr,satid,obs->code[i],nav->osb.scb[obs->sat-1][obs->code[i]-1]);
        }
        else if (opt->unbias>0) {
            P[i]=0.0;
            trace(2,"corr_meas: exclude satellite with invalid code bias. %s %s obscode=%2d\n",tstr,satid,obs->code[i]);
            continue;
        }

        if (nav->osb.vspb[obs->sat-1][obs->code[i]-1]!=BIAS_INVALID) {
            if (opt->modear!=ARMODE_OFF && (opt->arsys & sys)) {
                L[i]+=nav->osb.spb[obs->sat-1][obs->code[i]-1];
                trace(3,"corr_meas satellite phase bias %s %s obscode=%2d bias=%7.3f\n",
                    tstr,satid,obs->code[i],nav->osb.spb[obs->sat-1][obs->code[i]-1]);
            }
        }
        else if (opt->unbias>1) {
            L[i]=0.0;
            trace(2,"corr_meas: exclude satellite with invalid phase bias. %s %s obscode=%2d\n",tstr,satid,obs->code[i]);
            continue;
        }
        
        /* reciver bias correction */
        if (nav->osb.vrsatcb[obs->sat-1][obs->code[i]-1]!=BIAS_INVALID) {
            P[i]+=nav->osb.rsatcb[obs->sat-1][obs->code[i]-1];
            trace(3,"corr_meas receiver bias(sat) %s %s obscode=%2d bias=%7.3f\n",
                tstr,satid,obs->code[i],nav->osb.rsatcb[obs->sat-1][obs->code[i]-1]);
        }
        else if (nav->osb.vrsyscb[getsysno(obs->sat)][obs->code[i]-1]!=BIAS_INVALID) {
            P[i]+=nav->osb.rsyscb[getsysno(obs->sat)][obs->code[i]-1];
            trace(3,"corr_meas receiver bias(sys) %s %s obscode=%2d bias=%7.3f\n",
                tstr,satid,obs->code[i],nav->osb.rsyscb[getsysno(obs->sat)][obs->code[i]-1]);
        }
        else if (opt->unbias==1) {
            P[i]=0.0;
            trace(2,"corr_meas: exclude satellite with invalid receiver bias. %s %s obscode=%2d\n",tstr,satid,obs->code[i]);
            continue;
        }
    }

    /* iono-free LC */
    *Lc=*Pc=0.0;
    if (freq[0]==0.0||freq[1]==0.0) return;
    C1= SQR(freq[0])/(SQR(freq[0])-SQR(freq[1]));
    C2=-SQR(freq[1])/(SQR(freq[0])-SQR(freq[1]));
    
    if (L[0]!=0.0&&L[1]!=0.0) *Lc=C1*L[0]+C2*L[1];
    if (P[0]!=0.0&&P[1]!=0.0) *Pc=C1*P[0]+C2*P[1];
}
/* detect cycle slip by LLI --------------------------------------------------*/
static void detslp_ll(rtk_t *rtk, const obsd_t *obs, int n)
{
    int i,j;
    char id[4];
    
    trace(3,"detslp_ll: n=%d\n",n);
    
    /* Note, When using gcc, use constants to suppress
             -Wstringop-overflow warnings. */
    for (i=0;i<n&&i<MAXOBS;i++) for (j=0;j<rtk->opt.nf&&j<NFREQ;j++) {
        if (obs[i].L[j]==0.0||!(obs[i].LLI[j]&3)) continue;
        satno2id(obs[i].sat, id);
        trace(3,"detslp_ll: slip detected %s sat=%2d f=%d\n",id,obs[i].sat,j+1);
        
        rtk->ssat[obs[i].sat-1].slip[j]=1;
    }
}
/* detect cycle slip by geometry free phase jump -----------------------------*/
static void detslp_gf(rtk_t *rtk, const obsd_t *obs, int n, const nav_t *nav)
{
    double g0,g1;
    int i,f;
    char id[4];
    
    trace(3,"detslp_gf: n=%d\n",n);
    
    for (i=0;i<n&&i<MAXOBS;i++) {
        for (f=1;f<rtk->opt.nf&&f<NFREQ;f++) {
            if ((g1=gfmeas(obs+i,nav,f))==0.0) continue;
            
            g0=rtk->ssat[obs[i].sat-1].gf[f];
            rtk->ssat[obs[i].sat-1].gf[f]=g1;
            trace(4,"detslip_gf: sat=%2d f=%d gf0=%8.3f gf1=%8.3f\n",obs[i].sat,f+1,g0,g1);
            
            if (g0!=0.0&&fabs(g1-g0)>rtk->opt.thresslip) {
                satno2id(obs[i].sat, id);
                trace(2,"detslip_gf: slip detected %s sat=%2d f=%d gf=%8.3f->%8.3f\n",
                      id,obs[i].sat,f+1,g0,g1);
                
                rtk->ssat[obs[i].sat-1].slip[0]|=1;
                rtk->ssat[obs[i].sat-1].slip[f]|=1;
            }
        }
    }
}
/* detect slip by Melbourne-Wubbena linear combination jump ------------------*/
static void detslp_mw(rtk_t *rtk, const obsd_t *obs, int n, const nav_t *nav)
{
    double w0,w1;
    int i,f;
    char id[4];
    trace(3,"detslp_mw: n=%d\n",n);
    
    for (i=0;i<n&&i<MAXOBS;i++) {
        for (f=1;f<rtk->opt.nf&&f<NFREQ;f++) {
            if ((w1=mwmeas(obs+i,nav,f))==0.0) continue;
            
            w0=rtk->ssat[obs[i].sat-1].mw[f];
            rtk->ssat[obs[i].sat-1].mw[f]=w1;
            
            trace(4,"detslip_mw: sat=%2d f=%d mw0=%8.3f mw1=%8.3f\n",obs[i].sat,f+1,w0,w1);
            
            if (w0!=0.0&&fabs(w1-w0)>THRES_MW_JUMP) {
                satno2id(obs[i].sat, id);
                trace(2,"detslip_mw: slip detected %s sat=%2d f=%d mw=%8.3f->%8.3f\n",
                      id,obs[i].sat,f+1,w0,w1);
                
                rtk->ssat[obs[i].sat-1].slip[0]|=1;
                rtk->ssat[obs[i].sat-1].slip[f]|=1;
            }
        }
    }
}
/* detect discontinuity in bias ----------------------------------------------*/
static void detdisc_bias(rtk_t *rtk, const obsd_t *obs, int n, const nav_t *nav, 
                       int selssr)
{
    int i,j;
    int ssrcode;
    int sys;
    const ssr_t *pssr;
    ssat_t *pssat;
    char id[8];
    char str[32];
    
    trace(3,"detdisc_bias: n=%d\n",n);
    time2str(obs[0].time,str,3);
    
    for (i=0;i<n&&i<MAXOBS;i++) {
        pssr=&nav->ssr[selssr][obs[i].sat-1];
        pssat=&rtk->ssat[obs[i].sat-1];
        sys=satsys(obs[i].sat, NULL);
        satno2id(obs[i].sat,id);
    
        for (j=0;j<rtk->opt.nf&&j<NFREQ;j++) {
            pssat->dflg[j]=0;
            
            ssrcode=obs[i].code[j];
            if (pssr->vendor==SSR_VENDOR_L6) {
                ssrcode=mcssr_sel_biascode(sys,obs[i].code[j]);
            }
            else if (pssr->vendor==SSR_VENDOR_RTCM) {
                ssrcode=rtcm_sel_biascode(sys,obs[i].code[j]);
            }
            if (ssrcode==CODE_NONE) continue;

            /* flag if SSR code bias source has changed */
            if ((pssr->t0[4].time!=0) && 
                (pssat->prevcb[j] != nav->osb.vscb[obs[i].sat-1][ssrcode-1])) {
                pssat->dflg[j]|=DISC_CBVALID;
                trace(3,"detdisc_bias : code bias %s %3s code=%2d valid=%d->%d\n",
                    str, id, ssrcode, pssat->prevcb[j], 
                    nav->osb.vscb[obs[i].sat-1][ssrcode-1]);
            }

            if (rtk->opt.modear==ARMODE_OFF) continue;
            if (!(rtk->opt.arsys & sys)) continue;
        
            /* flag if SSR phase bias discontinuity indicator is incremented */
            if ((pssat->predisc[j] != pssr->disc[ssrcode-1])) {
                pssat->dflg[j]|=DISC_PBINDI;
                trace(3,"detdisc_bias : phase bias %s %3s code=%2d disc=%d->%d \n",
                    str, id, ssrcode, pssat->predisc[j], pssr->disc[ssrcode-1]);

            }
            /* flag if SSR phase bias source has changed */
            if (pssat->prevpb[j] != nav->osb.vspb[obs[i].sat-1][ssrcode-1]) {
                pssat->dflg[j]|=DISC_PBVALID;
                trace(3,"detdisc_bias : phase bias %s %3s code=%2d valid=%d->%d pb=%.3f->%.3f\n",
                    str, id, ssrcode, pssat->prevpb[j], 
                    nav->osb.vspb[obs[i].sat-1][ssrcode-1],
                    pssat->prepb[j],nav->osb.spb[obs[i].sat-1][ssrcode-1]);
            }
        }
    }
}
/* satellite antenna phase center variation ----------------------------------*/
static void satantpcv(const double *rs, const double *rr, const pcv_t *pcv,
                      double *dant)
{
    double ru[3],rz[3],eu[3],ez[3],nadir,cosa;
    int i;
    
    for (i=0;i<3;i++) {
        ru[i]=rr[i]-rs[i];
        rz[i]=-rs[i];
    }
    if (!normv3(ru,eu)||!normv3(rz,ez)) return;
    
    cosa=dot(eu,ez,3);
    cosa=cosa<-1.0?-1.0:(cosa>1.0?1.0:cosa);
    nadir=acos(cosa);
    
    antmodel_s(pcv,nadir,dant);
}
/* temporal update of position -----------------------------------------------*/
static void udpos_ppp(rtk_t *rtk)
{
    int i;
    
    trace(3,"udpos_ppp:\n");
    
    /* fixed mode */
    if (rtk->opt.mode==PMODE_PPP_FIXED) {
        for (i=0;i<3;i++) initx(rtk,rtk->opt.ru[i],1E-8,i);
        return;
    }
    /* initialize position for first epoch */
    if (norm(rtk->x,3)<=0.0) {
        for (i=0;i<3;i++) initx(rtk,rtk->sol.rr[i],VAR_POS,i);
    }
    /* static ppp mode */
    if (rtk->opt.mode==PMODE_PPP_STATIC) {
        for (i=0;i<3;i++) {
            rtk->P[i*(1+rtk->nx)]+=SQR(rtk->opt.prn[5])*fabs(rtk->tt);
        }
        return;
    }

    for (i=0;i<3;i++) {
        initx(rtk,rtk->sol.rr[i],VAR_POS,i);
    }
}
/* temporal update of position for dynamics ----------------------------------*/
static void udpos_ppp_dyna(rtk_t *rtk)
{
    double *F,*P,*FP,*x,*xp,pos[3],Qv[9];
    double Q[9]={0};
    int i,j,*ix,nx;

    trace(3,"udpos_ppp_dyna:\n");

    /* initialize position for first epoch */
    if (norm(rtk->x,3)<=0.0 || 
        (norm(rtk->x+3,3)<THRESH_VEL && norm(rtk->x+6,3)<THRESH_ACC)) {
        for (i=0;i<3;i++) initx(rtk,rtk->sol.rr[i],VAR_POS,i);
        for (i=3;i<6;i++) initx(rtk,rtk->sol.rr[i],VAR_VEL,i);
        for (i=6;i<9;i++) initx(rtk,1E-6,VAR_ACC,i);
        return;
    }
    /* generate valid state index */
    ix=imat(rtk->nx,1);
    for (i=nx=0;i<rtk->nx;i++) {
        if (rtk->x[i]!=0.0&&rtk->P[i+i*rtk->nx]>0.0) {
            ix[nx]=i;
            nx++;
        }
    }
    if (nx<9) {
        free(ix);
        return;
    }
    /* state transition of position/velocity/acceleration */
    F=eye(nx); P=mat(nx,nx); FP=mat(nx,nx); x=mat(nx,1); xp=mat(nx,1);
    
    for (i=0;i<6;i++) {
        F[i+(i+3)*nx]=rtk->tt;
    }
    for (i=0;i<3;i++) {
        F[i+(i+6)*nx]=SQR(rtk->tt)/2.0;
    }
    for (i=0;i<nx;i++) {
        x[i]=rtk->x[ix[i]];
        for (j=0;j<nx;j++) {
            P[i+j*nx]=rtk->P[ix[i]+ix[j]*rtk->nx];
        }
    }
    /* x=F*x, P=F*P*F+Q */
    matmul("NN",nx,1,nx,1.0,F,x,0.0,xp);
    matmul("NN",nx,nx,nx,1.0,F,P,0.0,FP);
    matmul("NT",nx,nx,nx,1.0,FP,F,0.0,P);
    
    for (i=0;i<nx;i++) {
        rtk->x[ix[i]]=xp[i];
        for (j=0;j<nx;j++) {
            rtk->P[ix[i]+ix[j]*rtk->nx]=P[i+j*nx];
        }
    }
    /* process noise added to only acceleration */
    Q[0]=Q[4]=SQR(rtk->opt.prn[3])*fabs(rtk->tt);
    Q[8]=SQR(rtk->opt.prn[4])*fabs(rtk->tt);
    ecef2pos(rtk->x,pos);
    covecef(pos,Q,Qv);
    for (i=0;i<3;i++) for (j=0;j<3;j++) {
        rtk->P[i+6+(j+6)*rtk->nx]+=Qv[i+j*3];
    }
    free(ix); free(F); free(P); free(FP); free(x); free(xp);
}
/* temporal update of position for ins ---------------------------------------*/
static void udpos_ppp_ins(rtk_t *rtk, int dr)
{
    double *F,*P,*FP,llh[3],omgies[9],b2eacc[3],b2eaccs[9],georad,ar[3],are[9];
    double Q[12]={0},e2=FE_WGS84*(2.0-FE_WGS84),omgie[3]={0.0},*E=eye(3);
    int i,j,*ix,nx;

    trace(3,"udpos_ppp_ins:\n");

    /* skew symmetric matrix of Earth rate */
    omgie[2]=OMGIE;
    skews(omgie,omgies);

    /* initialize position for first epoch */
    if (rtk->insd.insstat==INS_INACT || rtk->insd.gt.time==0 || 
        rtk->sol.resetins!=0 || 
        (0.0<rtk->opt.insreset && rtk->opt.insreset<rtk->insd.misstime)) {
        for (i=0;i<3;i++) rtk->insd.ecef[i] = rtk->sol.rr[i];
        for (i=0;i<3;i++) initx(rtk,1E-8,VAR_POS,i);
        for (i=3;i<6;i++) initx(rtk,1E-8,VAR_VEL,i);
        rtk->insd.insstat=INS_INACT;
        trace(2,"udpos_ppp_ins:reset position %s flag=%d,misstime=%.3f \n",
            time_str(rtk->sol.time,3), rtk->sol.resetins, rtk->insd.misstime);
        return;
    }
    else if (rtk->insd.insstat==INS_INIT) {
        for (i=0;i<3;i++)   initx(rtk,1E-8,SQR(rtk->opt.stdpos)              ,i);
        for (i=3;i<6;i++)   initx(rtk,1E-8,SQR(rtk->opt.stdvel)              ,i);
        for (i=6;i<9;i++)   initx(rtk,1E-8,SQR(rtk->opt.stdatt*PI/180)       ,i);
        for (i=9;i<12;i++)  initx(rtk,1E-8,SQR(rtk->opt.stdaccb*MICROG2M)    ,i);
        for (i=12;i<15;i++) initx(rtk,1E-8,SQR(rtk->opt.stdgyrob*PI/180/3600),i);
        rtk->insd.insstat=INS_ACT;
    }
    else {
        for (i=0;i<(NP(&rtk->opt)-1);i++) {
            rtk->x[i]=1E-8;
        }
    }
    /* generate valid state index */
    ix=imat(rtk->nx,1);
    for (i=nx=0;i<rtk->nx;i++) {
        if (rtk->x[i]!=0.0&&rtk->P[i+i*rtk->nx]>0.0) {
            ix[nx]=i;
            nx++;
        }
    }
    if (nx<(NP(&rtk->opt)-1)) {
        free(ix);
        return;
    }
    /* state transition of position/velocity/acceleration */
    F=eye(nx); P=mat(nx,nx); FP=mat(nx,nx);

    matmul("NN",3,1,3,1.0,rtk->insd.curb2e,rtk->insd.measacc,0.0,b2eacc);
    skews(b2eacc,b2eaccs);
    /* geocentric radius */
    ecef2pos(rtk->insd.ecef,llh);
    georad = RE_WGS84 / sqrt(1-e2*SQR(sin(llh[0]))) * 
                 sqrt(SQR(cos(llh[0])) + SQR((1-e2)) * SQR(sin(llh[0])));
    gravity_ecef(rtk->insd.ecef,ar);
    matmul("NT",3,3,1,1.0,ar,rtk->insd.ecef,0.0,are);

    for (i=0;i<3;i++) {
        for (j=0;j<3;j++) {
            /* att x att */
            F[i+6+(6+j)*nx]  =F[i+6+(6+j)*nx] - omgies[i+3*j] * rtk->insd.tor;
            /* att x gyro */
            F[i+6+(12+j)*nx] =rtk->insd.curb2e[i+3*j] * rtk->insd.tor;
            /* vel x att */
            F[i+3+(6+j)*nx]  =-b2eaccs[i+3*j] * rtk->insd.tor;
            /* vel x vel */
            F[i+3+(3+j)*nx]  =F[i+3+(3+j)*nx] -2 * omgies[i+3*j] * rtk->insd.tor;
            /* vel x pos */
            F[i+3+nx*j]      =-2 * are[i+3*j]  / georad / norm(rtk->insd.ecef,3) * rtk->insd.tor;
            /* vel x acc */
            F[i+3+(9+j)*nx]  =rtk->insd.curb2e[i+3*j] * rtk->insd.tor;
            /* pos x vel */
            F[i+(3+j)*nx]    =E[i+3*j] * rtk->insd.tor;
        }
    }
    for (i = 0;i<nx;i++) {
        for (j=0;j<nx;j++) {
            P[i+j*nx] = rtk->P[ix[i]+ix[j]*rtk->nx];
        }
    }
    /* x=F*x, P=F*P*F+Q */
    matmul("NN",nx,nx,nx,1.0,F,P,0.0,FP);
    matmul("NT",nx,nx,nx,1.0,FP,F,0.0,P);

    /* process noises */
    for (i=0;i<3;i++) {
        Q[i]=SQR(rtk->opt.prnaccn  * MICROG2M/rtk->opt.insint)  * rtk->insd.tor;
    }
    for (i=3;i<6;i++) {
        Q[i]=SQR(rtk->opt.prngyron * PI/180/60/rtk->opt.insint) * rtk->insd.tor;
    }
    for (i=6;i<9;i++) {
        Q[i]=SQR(rtk->opt.prnaccb  * 1000*MICROG2M)             * rtk->insd.tor;
    }
    for (i=9;i<12;i++) {
        Q[i]=SQR(rtk->opt.prngyrob * PI/180/3600)               * rtk->insd.tor;
    }
    for (i=0;i<nx;i++) {
        for (j = 0;j<nx;j++) {
            rtk->P[ix[i]+ix[j]*rtk->nx]=P[i+j*nx];
            if ((ix[i]==ix[j]) && (3<=ix[i]) && (ix[i]<=14)) {
                rtk->P[ix[i]+ix[j]*rtk->nx]+=Q[ix[i]-3];
            }
        }
    }
    free(ix); free(F); free(P); free(FP); free(E);
}
/* temporal update of clock drift --------------------------------------------*/
static void udclkd_ppp(rtk_t *rtk, double *xp, double *Pp)
{
    int j;
    double vel = 0.0;

    trace(3,"udclkd_ppp:\n");
    vel = norm(rtk->sol.rr + 3, 3);
    
    j=15;
    if (rtk->opt.clkdopt) {
        if (rtk->insd.clkstat==0 || xp[j]==0.0 || rtk->sol.resetins!=0) {
            /* initialize every epoch for clock (white noise) */
            initxp(xp,Pp,rtk->nx,CLIGHT*rtk->sol.dtrd,SQR(rtk->opt.stdclkd),j);
        }
        else {
            Pp[(NP(&rtk->opt)-1)*(1+rtk->nx)] += 
                SQR(rtk->opt.prn[7])*fabs(rtk->tt);
        }
    }
    else {
        /* initialize every epoch for clock (white noise) */
        initxp(xp,Pp,rtk->nx,CLIGHT*rtk->sol.dtrd,SQR(rtk->opt.stdclkd),j);
    }
}
/* temporal update of clock --------------------------------------------------*/
static void udclk_ppp(rtk_t *rtk, int ephreset, double *xp, double *Pp)
{
    double dtr=0.0;
    int i, j;
    
    trace(3,"udclk_ppp:\n");
    
    if (rtk->opt.dynamics==DY_INS) {
        udclkd_ppp(rtk, xp, Pp);
    }
    /* initialize every epoch for clock (white noise) */
    for (i=0;i<CLKNSYS;i++) {
        j=IC(i,&rtk->opt);
        if ((rtk->opt.dynamics==DY_INS) && rtk->opt.clkopt ) {
            dtr=i==0?rtk->sol.dtr[0]:rtk->sol.dtr[0]+rtk->sol.dtr[i];
            if (rtk->insd.clkstat==0 || xp[j]==0.0 || ephreset!=0 || 
                rtk->sol.resetins!=0 ) {
                initxp(xp,Pp,rtk->nx,CLIGHT*dtr,VAR_CLK,j);
                trace(3,"udclk_ppp : clock reset %s \n", 
                    time_str(rtk->sol.time,3));
                rtk->insd.clkstat = 1;
            }
            else {
                xp[j] += xp[NP(&rtk->opt)-1]*fabs(rtk->tt);
                Pp[j*(1+rtk->nx)] += SQR(rtk->opt.prn[8+i])*fabs(rtk->tt);
            }
            if (rtk->opt.clkjump!=0.0 && 
                fabs(xp[j]-dtr*CLIGHT)>rtk->opt.clkjump*CLIGHT) {
                trace(2,"udclk_ppp: %s clock jump[%d] diff=%.5f(s) \n",
                    time_str(rtk->sol.time,3), i, 
                    fabs(xp[j]-dtr*CLIGHT)/CLIGHT);
                initxp(xp,Pp,rtk->nx,CLIGHT*dtr,VAR_CLK,j);
            }
        }
        else {
            if (rtk->opt.sateph==EPHOPT_PREC) {
                /* time of prec ephemeris is based gpst */
                /* negelect receiver inter-system bias  */
                dtr=rtk->sol.dtr[0];
            }
            else {
                dtr=i==0?rtk->sol.dtr[0]:rtk->sol.dtr[0]+rtk->sol.dtr[i];
            }
            initxp(xp,Pp,rtk->nx,CLIGHT*dtr,VAR_CLK,j);
        }
    }
}
/* temporal update of tropospheric parameters --------------------------------*/
static void udtrop_ppp(rtk_t *rtk)
{
    double pos[3],azel[]={0.0,PI/2.0},ztd,var;
    int i=IT(&rtk->opt),j;
    
    trace(3,"udtrop_ppp:\n");
    
    if (rtk->x[i]==0.0) {
        ecef2pos(rtk->sol.rr,pos);
        ztd=sbstropcorr(rtk->sol.time,pos,azel,&var);
        initx(rtk,ztd,VAR_ZTD,i);
        
        if (rtk->opt.tropopt>=TROPOPT_ESTG) {
            for (j=i+1;j<i+3;j++) initx(rtk,1E-6,VAR_GRA,j);
        }
    }
    else {
        rtk->P[i+i*rtk->nx]+=SQR(rtk->opt.prn[2])*fabs(rtk->tt);
        if (rtk->opt.tropopt>=TROPOPT_ESTG) {
            for (j=i+1;j<i+3;j++) {
                rtk->P[j+j*rtk->nx]+=SQR(rtk->opt.prn[2]*0.1)*fabs(rtk->tt);
            }
        }
    }
}
/* temporal update of ionospheric parameters ---------------------------------*/
static void udiono_ppp(rtk_t *rtk, const obsd_t *obs, int n, const double *rs,
                       const nav_t *nav, int ephreset, double *xp, double *Pp,
                       double *ion)
{
    double freq1,freq2,ionp,ionc,sinel,pos[3],*azel;
    char *p;
    int i,j;
    int gap_resion=GAP_RESION;
    int dflg=0,slip=0;
    double L[NFREQ],P[NFREQ],Lc,Pc;
    double dantr[NFREQPCV]={0},dants[NFREQPCV]={0};

    trace(3,"udiono_ppp:n=%d\n",n);
    
    if ((p=strstr(rtk->opt.pppopt,"-GAP_RESION="))) {
        sscanf(p,"-GAP_RESION=%d",&gap_resion);
    }
    for (i=0;i<MAXSAT;i++) {
        j=II(i+1,&rtk->opt);
        if (xp[j]!=0.0&&(int)rtk->ssat[i].outc[0]>gap_resion) {
            xp[j]=0.0;
        }
    }
    for (i=0;i<n;i++) {
        j=II(obs[i].sat,&rtk->opt);
        dflg=rtk->ssat[obs[i].sat-1].dflg[0]||rtk->ssat[obs[i].sat-1].dflg[1];
        slip=rtk->ssat[obs[i].sat-1].slip[0]||rtk->ssat[obs[i].sat-1].slip[1];
        azel=rtk->ssat[obs[i].sat-1].azel;
        /* antenna model */
        antmodel(rtk->opt.pcvr,rtk->opt.antdel[0],azel,rtk->opt.posopt[1],dantr);
        if (rtk->opt.posopt[0]) {
            satantpcv(rs+i*6,rtk->sol.rr,nav->pcvs+obs[i].sat-1,dants);
        }
        /* corrected phase and code measurements */
        corr_meas(obs+i,nav,azel,&rtk->opt,dantr,dants,
            rtk->ssat[obs[i].sat-1].phw,L,P,&Lc,&Pc);
        freq1=sat2freq(obs[i].sat,obs[i].code[0],nav);
        freq2=sat2freq(obs[i].sat,obs[i].code[1],nav);
        ecef2pos(rtk->sol.rr,pos);

        if (xp[j]==0.0||dflg!=0||ephreset!=0) {
            if (P[0]==0.0||P[1]==0.0||freq1==0.0||freq2==0.0) {
                continue;
            }
            ionp=(P[0]-P[1])/(SQR(FREQ1/freq1)-SQR(FREQ1/freq2));
            ionp/=ionmapf(pos,azel);
            initxp(xp,Pp,rtk->nx,ionp,VAR_IONO,j);

            if (L[0]==0.0||L[1]==0.0) {
                ion[obs[i].sat-1]=0.0;
            }
            else {
                ionc=-1.0*(L[0]-L[1])/(SQR(FREQ1/freq1)-SQR(FREQ1/freq2));
                ion[obs[i].sat-1]=ionc;
            }
        }
        else {
            sinel=sin(MAX(rtk->ssat[obs[i].sat-1].azel[1],5.0*D2R));
            Pp[j+j*rtk->nx]+=SQR(rtk->opt.prn[1]/sinel)*fabs(rtk->tt);

            if (L[0]==0.0||L[1]==0.0||freq1==0.0||freq2==0.0||slip!=0) {
                ion[obs[i].sat-1]=0.0;
                continue;
            }
            ionc=-1.0*(L[0]-L[1])/(SQR(FREQ1/freq1)-SQR(FREQ1/freq2));
#if 0
            if (rtk->ssat[obs[i].sat-1].ionc!=0.0) {
                xp[j]+=(ionc-rtk->ssat[obs[i].sat-1].ionc)/ionmapf(pos,azel);
            }
#endif
            ion[obs[i].sat-1]=ionc;
        }
    }
}
/* temporal update of BeiDou-2 isb parameters --------------------------------*/
static void udbds2b_ppp(rtk_t *rtk, int ephreset, double *xp, double *Pp)
{
    int i, j;
    
    trace(3,"udbds2isb_ppp:\n");
    
    for (i=0;i<ND(&rtk->opt);i++) {
        j=ID(i,&rtk->opt);
        if (xp[j]==0.0 || ephreset!=0) {
            initxp(xp,Pp,rtk->nx,1E-6,VAR_BDSB,j);
        }
    }
}
/* temporal update of 3rd/4th-freqency-receiver-bias parameters --------------*/
static void udifb_ppp(rtk_t *rtk, int ephreset, double *xp, double *Pp)
{
    int i,j;
    
    trace(3,"udifb_ppp:\n");
    
    /* 3rd-freqency-receiver-bias */
    for (i=0;i<N3(&rtk->opt);i++) {
        j=I3(i,&rtk->opt);       
        if (xp[j]==0.0 || ephreset!=0) {
            initxp(xp,Pp,rtk->nx,1E-6,VAR_IFB,j);
        }
        else {
            /* add process noise */
            Pp[j+j*rtk->nx]+=SQR(rtk->opt.prn[6])*fabs(rtk->tt);
        }
    }
    
    /* 4th-freqency-receiver-bias */
    for (i=0;i<N4(&rtk->opt);i++) {
        j=I4(i,&rtk->opt);
        if (xp[j]==0.0 || ephreset!=0) {
            initxp(xp,Pp,rtk->nx,1E-6,VAR_IFB,j);
        }
        else {
            /* add process noise */
            Pp[j+j*rtk->nx]+=SQR(rtk->opt.prn[6])*fabs(rtk->tt);
        }
    }
}
/* temporal update of phase biases -------------------------------------------*/
static void udbias_ppp(rtk_t *rtk, const obsd_t *obs, int n, const nav_t *nav,
                       int ephreset, double *xp, double *Pp)
{
    double L[NFREQ],P[NFREQ],Lc,Pc,bias[MAXOBS],offset=0.0,pos[3]={0};
    double freq1,freq2,freq3,ion,dantr[NFREQPCV]={0},dants[NFREQPCV]={0};
    int i,j,k,f,sat,slip[MAXOBS]={0},clk_jump=0,dflg[MAXOBS]={0};
    
    trace(3,"udbias  : n=%d\n",n);
    
    /* handle day-boundary clock jump */
    if (rtk->opt.posopt[5]) {
        clk_jump=ROUND(time2gpst(obs[0].time,NULL)*10)%864000==0;
    }
    ecef2pos(rtk->sol.rr,pos);
    
    for (f=0;f<NF(&rtk->opt);f++) {
        
        /* reset phase-bias if expire obs outage counter */
        for (i=0;i<MAXSAT;i++) {
            if (rtk->ssat[i].outc[f]>(uint32_t)rtk->opt.maxout||
                rtk->opt.modear==ARMODE_INST||clk_jump) {
                initxp(xp,Pp,rtk->nx,0.0,0.0,IB(i+1,f,&rtk->opt));
            }
        }
        for (i=k=0;i<n&&i<MAXOBS;i++) {
            sat=obs[i].sat;
            j=IB(sat,f,&rtk->opt);
            /* Note, dants/dantr are ignored since they cancel out in 
               single-frequency code-minus-carrier (P-L) */
            corr_meas(obs+i,nav,rtk->ssat[sat-1].azel,&rtk->opt,dantr,dants,
                      0.0,L,P,&Lc,&Pc);
            bias[i]=0.0;
            
            if (rtk->opt.ionoopt==IONOOPT_IFLC) {
                bias[i]=Lc-Pc;
                slip[i]=rtk->ssat[sat-1].slip[0] | rtk->ssat[sat-1].slip[1];
                dflg[i]=rtk->ssat[sat-1].dflg[0] | rtk->ssat[sat-1].dflg[1];
                dflg[i]&=DISC_PBINDI | DISC_PBVALID;
            }
            else if (L[f]!=0.0&&P[f]!=0.0) {
                freq1=sat2freq(sat,obs[i].code[0],nav);
                freq2=sat2freq(sat,obs[i].code[1],nav);
                freq3=sat2freq(sat,obs[i].code[f],nav);
                slip[i]=rtk->ssat[sat-1].slip[f];
                dflg[i]=rtk->ssat[sat-1].dflg[f];
                if (obs[i].P[0]==0.0||obs[i].P[1]==0.0||obs[i].P[f]==0.0||
                    freq1==0.0||freq2==0.0||freq3==0.0) {
                    continue;
                }
                ion=xp[II(sat,&rtk->opt)]*ionmapf(pos,rtk->ssat[sat-1].azel);
                bias[i]=L[f]-P[f]+2.0*ion*SQR(freq1/freq3);
            }
            if (xp[j]==0.0||slip[i]||dflg[i]||bias[i]==0.0||ephreset!=0) continue;
            
            offset+=bias[i]-xp[j];
            k++;
        }
        /* correct phase-code jump to ensure phase-code coherency */
        if (k>=2&&fabs(offset/k)>0.0005*CLIGHT) {
            for (i=0;i<MAXSAT;i++) {
                j=IB(i+1,f,&rtk->opt);
                if (xp[j]!=0.0) xp[j]+=offset/k;
            }
            trace(2,"phase-code jump corrected: %s n=%2d dt=%12.9fs\n",
                  time_str(rtk->sol.time,0),k,offset/k/CLIGHT);
        }
        for (i=0;i<n&&i<MAXOBS;i++) {
            sat=obs[i].sat;
            j=IB(sat,f,&rtk->opt);
            Pp[j+j*rtk->nx]+=SQR(rtk->opt.prn[0])*fabs(rtk->tt);

            if (bias[i]==0.0||(xp[j]!=0.0&&!slip[i]&&!dflg[i]&&ephreset==0)) continue;
            
            /* reinitialize phase-bias */
            initxp(xp,Pp,rtk->nx,bias[i],VAR_BIAS,j);
            
            /* reset fix flags */
            for (k=0;k<MAXSAT;k++) rtk->ambc[sat-1].flags[k]=0;
            
            trace(5,"udbias_ppp: sat=%2d bias=%.3f\n",sat,bias[i]);
        }
    }
}
/* temporal update of states -------------------------------------------------*/
static void udstate_ppp(rtk_t *rtk, const obsd_t *obs, int n, const nav_t *nav,
                        int dr)
{
    int i,j,f;

    trace(3,"udstate_ppp: n=%d\n",n);
    
    if (dr && rtk->insd.insstat!=INS_ACT) {
        trace(2,"udstate_ppp: no update dr=%d ins=%d\n",dr,rtk->insd.insstat);
        return;
    }

    /* temporal update of position */
    switch(rtk->opt.dynamics) {
        case DY_OFF:
            udpos_ppp(rtk);
            break;
        case DY_DYNA:
            udpos_ppp_dyna(rtk);
            break;
        case DY_INS:
            udpos_ppp_ins(rtk,dr);
            break;
    }
    
    /* temporal update of tropospheric parameters */
    if (rtk->opt.tropopt==TROPOPT_EST||rtk->opt.tropopt==TROPOPT_ESTG) {
        udtrop_ppp(rtk);
    }
    if (dr==0) {
        for (i=0;i<MAXSAT;i++) for (j=0;j<rtk->opt.nf&&j<NFREQ;j++) {
            rtk->ssat[i].slip[j]=0;
        }
        /* detect cycle slip by LLI */
        detslp_ll(rtk,obs,n);
    
        /* detect cycle slip by geometry-free phase jump */
        detslp_gf(rtk,obs,n,nav);
    
        /* detect slip by Melbourne-Wubbena linear combination jump */
        detslp_mw(rtk,obs,n,nav);
        
        /* obs outage counter */
        for (f=0;f<NF(&rtk->opt);f++) {
            for (i=0;i<MAXSAT;i++) {
                rtk->ssat[i].outc[f]++;
            }
        }
    }
}
/* precise tropospheric model ------------------------------------------------*/
static double trop_model_prec(gtime_t time, const double *pos,
                              const double *azel, const double *x, double *dtdx,
                              double *var)
{
    const double zazel[]={0.0,PI/2.0};
    double zhd,m_h,m_w,cotz,grad_n,grad_e;
    
    /* zenith hydrostatic delay */
    zhd=tropmodel(time,pos,zazel,0.0);
    
    /* mapping function */
    m_h=tropmapf(time,pos,azel,&m_w);
    
    if (azel[1]>0.0) {
        
        /* m_w=m_0+m_0*cot(el)*(Gn*cos(az)+Ge*sin(az)): ref [6] */
        cotz=1.0/tan(azel[1]);
        grad_n=m_w*cotz*cos(azel[0]);
        grad_e=m_w*cotz*sin(azel[0]);
        m_w+=grad_n*x[1]+grad_e*x[2];
        dtdx[1]=grad_n*(x[0]-zhd);
        dtdx[2]=grad_e*(x[0]-zhd);
    }
    dtdx[0]=m_w;
    *var=SQR(0.01);
    return m_h*zhd+m_w*(x[0]-zhd);
}
/* tropospheric model --------------------------------------------------------*/
static int model_trop(gtime_t time, const double *pos, const double *azel,
                      const prcopt_t *opt, const double *x, double *dtdx,
                      const nav_t *nav, double *dtrp, double *var)
{
    double trp[3]={0};
    
    if (opt->tropopt==TROPOPT_SAAS) {
        *dtrp=tropmodel(time,pos,azel,REL_HUMI);
        *var=SQR(ERR_SAAS);
        return 1;
    }
    if (opt->tropopt==TROPOPT_SBAS) {
        *dtrp=sbstropcorr(time,pos,azel,var);
        return 1;
    }
    if (opt->tropopt==TROPOPT_EST||opt->tropopt==TROPOPT_ESTG) {
        matcpy(trp,x+IT(opt),opt->tropopt==TROPOPT_EST?1:3,1);
        *dtrp=trop_model_prec(time,pos,azel,trp,dtdx,var);
        return 1;
    }
    return 0;
}
/* ionospheric model ---------------------------------------------------------*/
static int model_iono(gtime_t time, const double *pos, const double *azel,
                      const prcopt_t *opt, int sat, const double *x,
                      const nav_t *nav, double *dion, double *var)
{
    if (opt->ionoopt==IONOOPT_SBAS) {
        return sbsioncorr(time,nav,pos,azel,dion,var);
    }
    if (opt->ionoopt==IONOOPT_TEC) {
        return iontec(time,nav,pos,azel,1,dion,var);
    }
    if (opt->ionoopt==IONOOPT_BRDC) {
        *dion=ionmodel(time,nav->ion_gps,pos,azel);
        *var=SQR(*dion*ERR_BRDCI);
        return 1;
    }
    if (opt->ionoopt==IONOOPT_EST) {
        *dion=x[II(sat,opt)];
        *var=0.0;
        return 1;
    }
    if (opt->ionoopt==IONOOPT_IFLC) {
        *dion=*var=0.0;
        return 1;
    }
    return 0;
}
/* constraint to local correction --------------------------------------------*/
static int const_corr(const obsd_t *obs, int n, const int *exc,
                      const nav_t *nav, const double *x, const double *pos,
                      const double *azel, rtk_t *rtk, double *v, double *H,
                      double *var)
{
    gtime_t time=obs[0].time;
    double trp[3]={0.0},std_trp[3]={0.0},ion[MAXSAT]={0.0},std_ion[MAXSAT]={0.0};
    int i,j,k,sat,nv=0;
    double el[MAXSAT]={0};
    char str[32], id[8];

    time2str(obs[0].time,str,2);
    /* constraint to external troposphere correction */
    if ((rtk->opt.tropopt==TROPOPT_EST||rtk->opt.tropopt==TROPOPT_ESTG)&&
        corr_trop_distwgt(&nav->stat,time,pos,MAX_DIST_TRP,trp,std_trp)) {
        
        for (i=0;i<(rtk->opt.tropopt==TROPOPT_EST?1:3);i++) {
            if (std_trp[i]==0.0) continue;
            j=IT(&rtk->opt)+i;
            v[nv]=trp[i]-x[j];
            for (k=0;k<rtk->nx;k++) H[k+nv*rtk->nx]=k==j?1.0:0.0;
            var[nv]=SQR(std_trp[i]);
            trace(4,"%s trop [%d] trop=%9.4f res=%9.4f std=%9.4f\n",str,i,
                  trp[i],v[nv],sqrt(var[nv]));
            nv++;
        }
    }

    /* constraint to external ionosphere correction */
    for (i=0;i<n;i++) el[obs[i].sat-1]=azel[i*2+1];
    if (rtk->opt.ionoopt==IONOOPT_EST&&
        corr_iono_distwgt(&nav->stat, time, pos, el, MAX_DIST_ION , ion,
                          std_ion, -1.0, NULL, NULL) == 1) {
        for (i=0;i<n;i++) {
            sat=obs[i].sat;
            satno2id(sat,id);
            if (exc[i]||ion[sat-1]==0.0||std_ion[sat-1]>1.0) continue;
            j=II(sat,&rtk->opt);
            v[nv]=ion[sat-1]-x[j];
            for (k=0;k<rtk->nx;k++) H[k+nv*rtk->nx]=k==j?1.0:0.0;
            var[nv]=SQR(std_ion[sat-1]);
            trace(4,"%s iono sat=%s iono=%9.4f res=%9.4f std=%9.4f el=%4.1f\n",
                str, id, ion[sat-1], v[nv], sqrt(var[nv]), el[sat-1]*R2D);
            nv++;
        }
    }

    trace(3,"%s local nv=%d\n",str,nv);

    return nv;
}
/* validate convergence ------------------------------------------------------*/
static int valconv(const double *qr, const double *rr, const double thre[2])
{
    double pos[3];
    double P[9];
    double Q[9];
    double sd[2]={0};
    
    if (fabs(thre[0])<=DBL_MIN) return 1;
    
    P[0]     =qr[0]; /* xx */
    P[4]     =qr[1]; /* yy */
    P[8]     =qr[2]; /* zz */
    P[1]=P[3]=qr[3]; /* xy */
    P[5]=P[7]=qr[4]; /* yz */
    P[2]=P[6]=qr[5]; /* zx */
    
    if (thre[1]>0.0) {
        /* validate ENU */
        ecef2pos(rr,pos);
        covenu(pos,P,Q);
        sd[0] = SQRT(Q[0]+Q[4]);   /* horizontal std */
        sd[1] = SQRT(Q[8]);        /* vertical   std */
        if ((sd[0]!=0.0)&&(sd[1]!=0.0)&&(sd[0]<thre[0])&&(sd[1]<thre[1])) {
            return 1;
        }
    }
    else {
        /* validate ECEF */
        sd[0] = SQRT(P[0]+P[4]+P[8]);
        if ((sd[0]!=0.0)&&(sd[0]<thre[0])) return 1;
    }
    trace(4,"valconv : sd=%8.4f,%8.4f,thre=%8.4f,%8.4f\n",
          sd[0],sd[1],thre[0],thre[1]);
    
    return 0;
}

/* constraint to ionospheric correction --------------------------------------*/
extern int const_miono_corr(rtk_t *rtk, const obsd_t *obs, const nav_t *nav,
                           const int n, const double *azel, const int *exc,
                           const double *rr, const double *x, double *v,
                           double *H, double *var)
{
    gtime_t time=obs[0].time;
    miono_corr_t mcorr;
    int i,j,k;
    int sat;
    int nv=0;
    int s;
    int ns;
    double tt;
    double std;
    double pos[3];
    double rejstd=IONO_REJ_STD;
    double covratio=IONO_COV_RATIO;
    double thre[2]={IONO_THRE_H,IONO_THRE_V};
    double ave;
    double f;
    double bsys[NSYS]={0.0};
    char *p;
    char *tstr;
    char satid[8];
    const int sys[NSYS]={SYS_GPS,SYS_GLO,SYS_GAL,SYS_QZS,0};

    if ((p=strstr(rtk->opt.pppopt,"-IONOCORR_THRE_H="))) {
        sscanf(p,"-IONOCORR_THRE_H=%lf",&thre[0]);
    }
    if ((p=strstr(rtk->opt.pppopt,"-IONOCORR_THRE_V="))) {
        sscanf(p,"-IONOCORR_THRE_V=%lf",&thre[1]);
    }
    if ((p=strstr(rtk->opt.pppopt,"-IONOCORR_REJ_STD="))) {
        sscanf(p,"-IONOCORR_REJ_STD=%lf",&rejstd);
    }
    if ((p=strstr(rtk->opt.pppopt,"-IONOCORR_COV_RATIO="))) {
        sscanf(p,"-IONOCORR_COV_RATIO=%lf",&covratio);
    }

    if (valconv(rtk->prev_qr,rr,thre)) return 0;

    ecef2pos(rr,pos);
    
    mcorr = nav->miono.corr;
    /* estimate system bias */
    for (s=0;sys[s];s++) { /* for each system */
        ave=0.0;ns=0;
        for (i=0;i<n;i++) {
            sat=obs[i].sat;
            if (satsys(sat,NULL)!=sys[s]) continue;
            if (mcorr.t0[sat-1].time == 0.0) continue;
            tt = fabs(timediff(time,mcorr.t0[sat-1]));
            tstr = time_str(mcorr.t0[sat-1],0);
            
            satno2id(sat, satid);
            if (exc[i]) continue;
            if (tt > MIONO_MAX_AGE) continue;
            if (mcorr.std[sat-1] > rejstd) continue;
            
            f=ionmapf(pos,azel+i*2);
            j=II(sat,&rtk->opt);
            ave+=(mcorr.dly[sat-1]-f*x[j]);
            ns++;
        }
        if (ns>0) bsys[s]=ave/(double)ns;
    }

    /* constraint to external ionosphere correction */
    for (i=0;i<n;i++) {
        sat=obs[i].sat;
        if (mcorr.t0[sat-1].time == 0.0) continue;
        tt = fabs(timediff(time,mcorr.t0[sat-1]));
        tstr = time_str(mcorr.t0[sat-1],0);

        satno2id(sat, satid);

        switch (satsys(sat,NULL)) {
            case SYS_GPS: s=0; break;
            case SYS_GLO: s=1; break;
            case SYS_GAL: s=2; break;
            case SYS_QZS: s=3; break;
            default:      continue;
        }

        if (exc[i]) continue;
        if (tt > MIONO_MAX_AGE) continue;
        if (mcorr.std[sat-1] > rejstd) continue;

        std = mcorr.std[sat-1];
        f=ionmapf(pos,azel+i*2);
        j=II(sat,&rtk->opt);
        v[nv]=mcorr.dly[sat-1]-f*x[j]-bsys[s];

        trace(3,"const_miono_corr %s,%s,tt=%11d,az=%5.1f,el=%4.1f,sdly=%8.4f,std=%8.4f,v=%8.4f,f*x=%8.4f,bsys=%8.4f\n",
            satid,tstr,(int)tt,(azel+i*2)[0]*R2D,(azel+i*2)[1]*R2D,
            mcorr.dly[sat-1],mcorr.std[sat-1],v[nv],f*x[j],bsys[s]);
        for (k=0;k<rtk->nx;k++) H[k+nv*rtk->nx]=k==j?f:0.0;
        var[nv++]=SQR(std)*covratio;
    }
    return nv;
}
/* doppler residuals ---------------------------------------------------------*/
static int dopp_res(int post, const obsd_t *obs, int n, const double *rs, 
                    const double *dts, const double *dr, const int *exc,
                    int *excd, const nav_t *nav, const double *x,
                    const double *azel, rtk_t *rtk, double *v, double *H, 
                    double *varr, int *stat)
{
    prcopt_t *opt=&rtk->opt;
    int i,j,k,sat,sys,maxobsd,rejd;
    int nv=0,nx=rtk->nx,ned=0,obsid[MAXOBS*NFREQ]={0};
    double freq,D[NFREQ],y,r,rr[3],pos[3],e[3],rate,vs[3],vr[3],cdtrd,thres;
    double ved[MAXOBS*2*NFREQ]={0},vmaxd=0,factor = 1.0;
    char str[32],id[8];

    time2str(obs[0].time,str,3);
    if (rtk->insd.clkstat==1) {
        factor = 100;
    }

    for (i=0;i<3;i++) rr[i]=rtk->insd.ecef[i]-x[i]+dr[i];
    ecef2pos(rr,pos);
    for (i=0;i<3;i++) vr[i] = rtk->insd.vel[i]-x[i+3];

    for (i=0;i<n&&i<MAXOBS;i++) {
        if (exc[i] || excd[i]) continue;
        sat=obs[i].sat;
        sys=satsys(sat,NULL);
        satno2id(sat,id);
        if ((r=geodist(rs+i*6,rr,e))<=0.0) {
            excd[i]=1;
            continue;
        }
        
        /* stack doppler residuals {D1,D2} */
        for (j=0;j<NF(opt);j++) {
            if ((freq=sat2freq(sat,obs[i].code[j],nav))==0.0) continue;
            D[j]=-obs[i].D[j]*CLIGHT/freq;
            y = D[j];
            if (D[j]==0.0) continue;
            
            /* satellite velocity relative to receiver in ECEF */
            for (k=0;k<3;k++) {
                vs[k] = rs[k+3+i*6]-vr[k];
            }
            /* range rate with earth rotation correction */
            rate = dot(vs,e,3)+OMGE/CLIGHT*(rs[4+i*6]*rr[0]+rs[1+i*6]*vr[0]-
                            rs[3+i*6]*rr[1]-rs[  i*6]*vr[1]);

            for (k=0;k<nx;k++) H[k+nx*nv]=(3<=k && k<6)?e[k-3]:0.0;

            H[NP(opt)-1+nx*nv] = 1.0;
            cdtrd = x[NP(opt)-1];
            v[nv] = y-(rate+cdtrd-CLIGHT*dts[1+i*2]);

            /* variance */
            varr[nv] = varerr_rate(sys,azel[1+i*2],freq,opt,factor);
            trace(4,"%s [%d] sat=%s %s%d res=%9.4f std=%9.4f el=%4.1f clkd=%7.4f\n",
                  str,post,id,"D",j+1,v[nv],sqrt(varr[nv]),azel[1+i*2]*R2D,cdtrd);
            /* reject satellite by pre-fit residuals */
            if (!post&&opt->maxinno>0.0&&fabs(v[nv])>opt->maxinno) {
                trace(2,"outlier (%d) rejected %s sat=%s %s%d res=%9.4f\n",
                        post,str,id,"D",j+1,v[nv]);
                excd[i]=1;
                continue;
            }
            /* record large post-fit residuals */
            if (rtk->tt>rtk->opt.tint) {
                thres=sqrt(varr[nv])*THRES_REJECT*fabs(rtk->tt/rtk->opt.tint);
            }
            else{
                thres=sqrt(varr[nv])*THRES_REJECT;
            }
            if (post&&fabs(v[nv])>thres) {
                obsid[ned]=i; ved[ned]=v[nv]; ned++;
            }
            nv++;
        }
    }
    if (post&&ned>0) {
        vmaxd=ved[0]; maxobsd=obsid[0]; rejd=0;
        for (j=1;j<ned;j++) {
            if (fabs(vmaxd)>=fabs(ved[j])) continue;
            vmaxd=ved[j]; maxobsd=obsid[j]; rejd=j;
        }
        sat=obs[maxobsd].sat;
        satno2id(sat,id);
        trace(2,"outlier (%d) rejected %s sat=%s %s res=%9.4f\n",
                post,str,id,"D",vmaxd);
        excd[maxobsd]=1; *stat=0;
        ved[rejd]=0;
    }
    return nv;
}
/* quality control -----------------------------------------------------------*/
static int qc_ppp_res(const obsd_t *obs, int n)
{
    int i;
    int nv_cnt = 0;

    /* reject measurements with qc in case of less than 6 satellite */
    for (i=0;i<n&&i<MAXOBS;i++) {
        if (obs[i].L[0]!=0.0 && obs[i].L[1]!=0.0) nv_cnt++;
    }
    if (nv_cnt<=6) {
        trace(2,"qc_ppp_res : reject qc n=%d nv_cnt=%d\n",n,nv_cnt);
        return 0;
    }
    return 1;
}
/* phase and code residuals --------------------------------------------------*/
static int ppp_res(int post, const obsd_t *obs, int n, const double *rs,
                   const double *dts, const double *var_rs, const int *svh,
                   const double *dr, int *exc, int *excd, const nav_t *nav,
                   const double *x, rtk_t *rtk, double *v, double *H, 
                   double *R, double *azel)
{
    prcopt_t *opt=&rtk->opt;
    double y,r,cdtr,bias,isb,ifb,Lc,Pc,freq,thres;
    double rr[3],pos[3],e[3],dtdx[3],L[NFREQ],P[NFREQ],var[MAXOBS*2*NFREQ];
    double C=0.0,dtrp=0.0,dion=0.0,vart=0.0,vari=0.0,snrvar_c=1.0;
    double dantr[NFREQPCV]={0},dants[NFREQPCV]={0},ve[MAXOBS*2*NFREQ]={0};
    double vare[MAXOBS*2*NFREQ],vmax=0;
    char str[32],id[8];
    int i,j,k,sat,sys,maxobs,maxfrq,rej,frqi[MAXOBS*2*NFREQ];
    int ne=0,nv=0,nx=rtk->nx,stat=1,cmp=0;
    int obsi[MAXOBS*2*NFREQ]={0};
    
    time2str(obs[0].time,str,3);
    
    for (i=0;i<MAXSAT;i++) {
        for (j=0;j<opt->nf&&j<NFREQ;j++) {
            rtk->ssat[i].vsat[j]=0;
        }
    } 
    if (opt->dynamics==DY_INS && rtk->pppflg) {
        for (i=0;i<3;i++) rr[i]=rtk->insd.ecef[i]-x[i]+dr[i];
    }
    else{
        for (i=0;i<3;i++) rr[i]=x[i]+dr[i];
    }
    ecef2pos(rr,pos);
    
    if (qc_ppp_res(obs, n) == 0) return 0;

    for (i=0;i<n&&i<MAXOBS;i++) {
        sat=obs[i].sat;
        satno2id(sat,id);

        if ((r=geodist(rs+i*6,rr,e))<=0.0||
            satazel(pos,e,azel+i*2)<opt->elmin||
            testelmask(azel+i*2,opt->elmaskopt)||
            azelmask(rr,azel+i*2,obs[i].time,sat)) {
            exc[i]=1;
            continue;
        }
        if (!(sys=satsys_bd2(sat,NULL,nav->pcvs[sat-1].type))||!rtk->ssat[sat-1].vs||
            satexclude(obs[i].sat,var_rs[i],svh[i],opt)||
            exclsats_ep(obs[i].time,sat)||exc[i]) {
            exc[i]=1;
            continue;
        }
        /* tropospheric and ionospheric model */
        if (!model_trop(obs[i].time,pos,azel+i*2,opt,x,dtdx,nav,&dtrp,&vart)||
            !model_iono(obs[i].time,pos,azel+i*2,opt,sat,x,nav,&dion,&vari)) {
            continue;
        }
        /* satellite and receiver antenna model */
        if (opt->posopt[0]) satantpcv(rs+i*6,rr,nav->pcvs+sat-1,dants);
        antmodel(opt->pcvr,opt->antdel[0],azel+i*2,opt->posopt[1],dantr);
        
        /* phase windup model */
        if (!model_phw(rtk->sol.time,sat,nav->pcvs[sat-1].type,
                       opt->posopt[2]?2:0,rs+i*6,rr,&rtk->ssat[sat-1].phw)) {
            continue;
        }
        /* corrected phase and code measurements */
        corr_meas(obs+i,nav,azel+i*2,&rtk->opt,dantr,dants,
                  rtk->ssat[sat-1].phw,L,P,&Lc,&Pc);
        
        /* stack phase and code residuals {L1,P1,L2,P2,...} */
        for (j=0;j<2*NF(opt);j++) {

            isb=bias=ifb=0.0;
            
            if (opt->ionoopt==IONOOPT_IFLC) {
                if ((y=j%2==0?Lc:Pc)==0.0) continue;
            }
            else {
                if ((y=j%2==0?L[j/2]:P[j/2])==0.0) continue;
                
                if ((freq=sat2freq(sat,obs[i].code[j/2],nav))==0.0) continue;
                C=SQR(FREQ1/freq)*ionmapf(pos,azel+i*2)*(j%2==0?-1.0:1.0);
            }
            if (opt->dynamics==DY_INS && rtk->pppflg) {
                for (k=0;k<nx;k++) H[k+nx*nv]=k<3?e[k]:0.0;
            }
            else{
                for (k=0;k<nx;k++) H[k+nx*nv]=k<3?-e[k]:0.0;
            }
            
            /* receiver clock */
            k=sys2pppsys(sys);
            cdtr=x[IC(k,opt)];
            H[IC(k,opt)+nx*nv]=1.0;
            
            if (j/2==2&&j%2==1) { /* 3rd-frequency-receiver-bias */
                ifb=x[I3(k,opt)];
                H[I3(k,opt)+nx*nv]=1.0;
            }
            if (j/2==3&&j%2==1) { /* 4th-frequency-receiver-bias */
                ifb=x[I4(k,opt)];
                H[I4(k,opt)+nx*nv]=1.0;
            }
            
            if (opt->tropopt==TROPOPT_EST||opt->tropopt==TROPOPT_ESTG) {
                for (k=0;k<(opt->tropopt>=TROPOPT_ESTG?3:1);k++) {
                    H[IT(opt)+k+nx*nv]=dtdx[k];
                }
            }
            if (opt->ionoopt==IONOOPT_EST) {
                if (x[II(sat,opt)]==0.0) continue;
                H[II(sat,opt)+nx*nv]=C;
            }
            if ((rtk->opt.navsys&SYS_CMP) && (rtk->opt.bds2bias == 1)) {
                for (k=0; k<ND(opt);k++) {
                    cmp=strncmp(nav->pcvs[sat-1].type, bds2_sat_type[k], 
                                strlen(bds2_sat_type[k]));
                    if (cmp==0 && j%2==1) { /* BeiDou2-isb */
                        isb+=x[ID(k,opt)];
                        H[ID(k,opt)+nx*nv]=1.0;
                    }
                }
            }
            if (j%2==0) { /* phase bias */
                if ((bias=x[IB(sat,j/2,opt)])==0.0) continue;
                H[IB(sat,j/2,opt)+nx*nv]=1.0;
            }
            /* SNR weight */
            if (azel[1+i*2]>0.0&&obs[i].SNR[j/2]>0) {   
                if (j%2==1) {
                    snrvar_c=snrvarweight(obs[i].time,sat,obs[i].code[j/2],
                                          azel[1+i*2],obs[i].SNR[j/2]*SNR_UNIT,
                                          opt);
                    if (snrvar_c==0.0) {
                        trace(2,"outlier (%d) rejected SNR %s sat=%2d %s%d res=%9.4f el=%4.1f SNR=%6.3f\n",
                              post,str,sat,j%2?"P":"L",j/2+1,v[nv],
                              azel[1+i*2]*R2D,obs[i].SNR[j/2]*SNR_UNIT);
                            continue;
                    }
                }else{
                    snrvar_c=1.0;
                }
            }

            /* residual */
            v[nv]=y-(r+cdtr-CLIGHT*dts[i*2]+dtrp+C*dion+isb+bias+ifb);

            if (j%2==0) rtk->ssat[sat-1].resc[j/2]=v[nv];
            else        rtk->ssat[sat-1].resp[j/2]=v[nv];
            
            /* variance */
            var[nv]=(varerr(obs[i].sat,sys,azel[1+i*2],j/2,j%2,opt)+
                    vart+SQR(C)*vari+opt->uraratio*var_rs[i])*snrvar_c;
            if (sys==SYS_GLO&&j%2==1) var[nv]+=VAR_GLO_IFB;
            
            trace(4,"%s [%d] sat=%s %s%d res=%9.4f std=%9.4f el=%4.1f C=%7.4f dion=%7.4f dtrp=%7.4f bias=%9.4f\n",
                  str,post,id,j%2?"P":"L",j/2+1,v[nv],sqrt(var[nv]),
                  azel[1+i*2]*R2D,C,dion,dtrp,bias);
            
            /* reject satellite by pre-fit residuals */
            if (!post&&opt->maxinno>0.0&&fabs(v[nv])>opt->maxinno) {
                trace(2,"outlier (%d) rejected %s sat=%s %s%d res=%9.4f az=%4.1f el=%4.1f\n",
                      post,str,id,j%2?"P":"L",j/2+1,v[nv],azel[i*2]*R2D,
                      azel[1+i*2]*R2D);
                exc[i]=1; rtk->ssat[sat-1].rejc[j%2]++;
                continue;
            }
            /* record large post-fit residuals */
            if (rtk->tt>rtk->opt.tint) {
                thres=sqrt(var[nv])*THRES_REJECT*fabs(rtk->tt/rtk->opt.tint);
            }else{
                thres=sqrt(var[nv])*THRES_REJECT;
            }
            if (post&&fabs(v[nv]) > thres) {
                obsi[ne]=i; frqi[ne]=j; ve[ne]=v[nv]; vare[ne]=var[nv]; ne++;
            }
            if (j%2==0) rtk->ssat[sat-1].vsat[j/2]=1;
            nv++;
        }
    }
    /* reject satellite with large and max post-fit residual */
    if (post&&ne>0) {
        vmax=ve[0]; maxobs=obsi[0]; maxfrq=frqi[0]; rej=0;
        for (j=1;j<ne;j++) {
            if (fabs(vmax)>=fabs(ve[j])) continue;
            vmax=ve[j]; maxobs=obsi[j]; maxfrq=frqi[j]; rej=j;
        }
        sat=obs[maxobs].sat;
        satno2id(sat,id);
        trace(2,"outlier (%d) rejected %s sat=%s %s%d res=%9.4f std=%9.4f az=%4.1f el=%4.1f\n",
              post,str,id,maxfrq%2?"P":"L",maxfrq/2+1,vmax,sqrt(vare[rej]),
              azel[maxobs*2]*R2D,azel[1+maxobs*2]*R2D);
        exc[maxobs]=1; rtk->ssat[sat-1].rejc[maxfrq%2]++; stat=0;
        ve[rej]=0;
    }
    /* constraint to local correction */
    if (rtk->pppflg) nv+=const_corr(obs,n,exc,nav,x,pos,azel,rtk,v+nv,H+nv*rtk->nx,var+nv);

    /* constraint to ionospheric correction */
    if (!post && opt->ionocorr) {
        nv+=const_miono_corr(rtk,obs,nav,n,azel,exc,rr,x,v+nv,H+nv*rtk->nx,var+nv);
    }

    /* doppler residuals */
    if (opt->dynamics==DY_INS) {
        nv+=dopp_res(post,obs,n,rs,dts,dr,exc,excd,nav,x,azel,rtk,v+nv,
            H+nv*rtk->nx,var+nv,&stat);
    }
    for (i=0;i<nv;i++) for (j=0;j<nv;j++) {
        R[i+j*nv]=i==j?var[i]:0.0;
    }
    return post?stat:nv;
}
/* number of estimated states ------------------------------------------------*/
extern int pppnx(const prcopt_t *opt)
{
    return NX(opt);
}
/* update solution status ----------------------------------------------------*/
static void update_stat(rtk_t *rtk, const obsd_t *obs, int n, int stat,
                        const nav_t *nav, int dr)
{
    const prcopt_t *opt=&rtk->opt;
    int i,j,nx,ssrcode,sys;
    double *x,*P;
    double att[3],atts[9],attcorr[9],earthav[3], earthavs[9];
    double armvec[3],measgyro[3],omgl[3],comgl[3],comgiel[3];
    double *E=eye(3);

    /* test # of valid satellites */
    rtk->sol.ns=0;
    if (dr==0) {
        for (i=0;i<n&&i<MAXOBS;i++) {
            for (j=0;j<opt->nf&&j<NFREQ;j++) {
                if (!rtk->ssat[obs[i].sat-1].vsat[j]) continue;
                rtk->ssat[obs[i].sat-1].lock[j]++;
                rtk->ssat[obs[i].sat-1].outc[j]=0;
                if (j==0) rtk->sol.ns++;
            }
        }
    }

    /* select state vector and covariance matrix */
    if (stat==SOLQ_FIX||stat==SOLQ_FIX_WL) {
        nx=rtk->na;
        x =rtk->xa;
        P =rtk->Pa;
    }
    else{
        nx=rtk->nx;
        x =rtk->x;
        P =rtk->P;
    }
    if (stat==SOLQ_FIX_WL) stat=SOLQ_PPP;
    if (rtk->sol.ns<MIN_NSAT_SOL) {
        rtk->sol.stat=SOLQ_NONE;
        trace(2,"update_stat sol status none:ns=%d\n",rtk->sol.ns);
    }
    else{
        rtk->sol.stat=stat;
    }

    /* update solution based on INS */
    if (opt->dynamics==DY_INS) {
        for (i=0;i<3;i++) {
            rtk->insd.ecef[i] =rtk->insd.ecef[i]-x[i];
            rtk->insd.vel[i]  =rtk->insd.vel[i] -x[i+3];
            rtk->sol.rr[i]    =rtk->insd.ecef[i];
            rtk->sol.rr[i+3]  =rtk->insd.vel[i];
            rtk->sol.qr[i]    =(float)P[i+i*nx];
        }
        rtk->sol.qr[3]=(float)P[1];
        rtk->sol.qr[4]=(float)P[1+2*nx];
        rtk->sol.qr[5]=(float)P[2];
        
        /* correct attitude errors */
        for (i=0;i<3;i++) {
            att[i]=x[6+i];
        }
        skews(att, atts);
        for (i=0;i<9;i++) {
            attcorr[i]=E[i]-atts[i];
        }
        matmul("NN",3,3,3,1.0,attcorr,rtk->insd.curb2e,0.0,rtk->insd.curb2e);
        
        /* update IMU biases */
        for (i=0;i<6;i++) {
            rtk->insd.imubias[i]+=x[i+9];
        }
        
        /* correct lever arm */
        matmul("NN",3,1,3,1.0,rtk->insd.curb2e,rtk->insd.antimu,0.0,armvec);
        
        /* tightly coupling */
        earthav[0]=earthav[1]=0.0;
        earthav[2]=OMGIE;
        skews(earthav,earthavs);
        for (i=0;i<3;i++) {
            measgyro[i]=rtk->insd.measgyro[i]-x[i+9];
        }
        cross3(measgyro,rtk->insd.antimu,omgl);
        matmul("NN",3,1,3,1.0,rtk->insd.curb2e,omgl  ,0.0,comgl  );
        matmul("NN",3,1,3,1.0,earthavs        ,armvec,0.0,comgiel);
        for (i=0;i<3;i++) {
            rtk->sol.rr[i]  =rtk->sol.rr[i]  +armvec[i];
            rtk->sol.rr[i+3]=rtk->sol.rr[i+3]+comgl[i] +comgiel[i];
        }
    }
    /* update solution without INS */
    else{
        for (i=0;i<3;i++) {
            rtk->sol.rr[i]=x[i];
            rtk->sol.qr[i]=(float)P[i+i*nx];
        }
        rtk->sol.qr[3]=(float)P[1];
        rtk->sol.qr[4]=(float)P[1+2*nx];
        rtk->sol.qr[5]=(float)P[2];
    }

    for (i=0;i<CLKNSYS;i++) {
        if (P[IC(i,opt)+IC(i,opt)*nx]<VAR_CLK) {
            rtk->sol.dtr[i]=i==0?x[IC(0,opt)]:x[IC(i,opt)]-x[IC(0,opt)];
        }
        else {
            rtk->sol.dtr[i]=0.0;
        }
        rtk->sol.dtr[i]/=CLIGHT;
    }

    /* store previous solution */
    for (i=0;i<6;i++) rtk->sol.rrp[i]=rtk->sol.rr[i];

    if (dr==0) {
        for (i=0;i<n&&i<MAXOBS;i++) {
            for (j=0;j<opt->nf&&j<NFREQ;j++) {
                rtk->ssat[obs[i].sat-1].snr[j]=obs[i].SNR[j];

                sys=satsys(obs[i].sat, NULL);
                
                ssrcode=obs[i].code[j];
                if (nav->ssr[rtk->selssr][obs[i].sat-1].vendor
                     ==SSR_VENDOR_L6) {
                    ssrcode=mcssr_sel_biascode(sys,obs[i].code[j]);
                }
                else if (nav->ssr[rtk->selssr][obs[i].sat-1].vendor
                          ==SSR_VENDOR_RTCM) {
                    ssrcode=rtcm_sel_biascode(sys,obs[i].code[j]);
                }                
                rtk->ssat[obs[i].sat-1].predisc[j] =
                    nav->ssr[rtk->selssr][obs[i].sat-1].disc[ssrcode-1];
                rtk->ssat[obs[i].sat-1].prevpb[j] =
                    nav->osb.vspb[obs[i].sat-1][ssrcode-1];
                rtk->ssat[obs[i].sat-1].prepb[j] =
                    nav->osb.spb[obs[i].sat-1][ssrcode-1];
                rtk->ssat[obs[i].sat-1].prevcb[j] =
                    nav->osb.vscb[obs[i].sat-1][ssrcode-1];
                rtk->ssat[obs[i].sat-1].precb[j] =
                    nav->osb.scb[obs[i].sat-1][ssrcode-1];
            }
        }
        for (i=0;i<MAXSAT;i++) {
            for (j=0;j<opt->nf&&j<NFREQ;j++) {
                if (rtk->ssat[i].slip[j]&3) rtk->ssat[i].slipc[j]++;
                if (rtk->ssat[i].fix[j]==2&&stat!=SOLQ_FIX) rtk->ssat[i].fix[j]=1;
            }
        }
    }
    free(E);
}
/* test hold ambiguity -------------------------------------------------------*/
static int test_hold_amb(rtk_t *rtk)
{
    int i,j,stat=0;
    
    /* no fix-and-hold mode */
    if (rtk->opt.modear!=ARMODE_FIXHOLD) return 0;
    
    /* reset # of continuous fixed if new ambiguity introduced */
    for (i=0;i<MAXSAT;i++) {
        if (rtk->ssat[i].fix[0]!=2&&rtk->ssat[i].fix[1]!=2) continue;
        for (j=0;j<MAXSAT;j++) {
            if (rtk->ssat[j].fix[0]!=2&&rtk->ssat[j].fix[1]!=2) continue;
            if (!rtk->ambc[j].flags[i]||!rtk->ambc[i].flags[j]) stat=1;
            rtk->ambc[j].flags[i]=rtk->ambc[i].flags[j]=1;
        }
    }
    if (stat) {
        rtk->nfix=0;
        return 0;
    }
    /* test # of continuous fixed */
    return ++rtk->nfix>=ROUND(rtk->opt.minfix/fabs(rtk->tt));
}
/* copy satellite code bias --------------------------------------------------*/
static int cpsatcb(gtime_t gt, int vp, const ssr_t *ssr, osb_t *osb, int valid)
{
    int i,j;
    int sys;
    int udcnt=0;
    int ssrcode=CODE_NONE;

    for (i=0;i<MAXSAT;i++) {
        sys=satsys(i+1,NULL);
        if (timediff(gt,ssr[i].t0[4])>vp) continue;
        for (j=0;j<MAXCODE;j++) {
            ssrcode=j+1;
            if (ssr[i].vendor==SSR_VENDOR_L6) {
                ssrcode=mcssr_sel_biascode(sys,j+1);
            }
            else if (ssr[i].vendor==SSR_VENDOR_RTCM) {
                ssrcode=rtcm_sel_biascode(sys,j+1);
            }
            if (ssrcode==CODE_NONE) continue;
            if (sys==SYS_GPS && ssrcode==CODE_L5Q) {
                if (ssr[i].vcbias[ssrcode-1]==BIAS_INVALID&&
                    ssr[i].vcbias[CODE_L5X-1]!=BIAS_INVALID) ssrcode=CODE_L5X;
            }
            if (ssr[i].vcbias[ssrcode-1]==BIAS_INVALID) continue;
            osb->vscb[i][j] = valid;
            osb->scb[i][j]  = ssr[i].cbias[ssrcode-1];
            udcnt++;
        }
        if (0 < udcnt) {
            osb->gt[0]=ssr[i].t0[4];
        }
    }
    return udcnt;
}
/* update satellite code bias ------------------------------------------------*/
static void udsatcb(gtime_t gt,nav_t *nav, osb_t *biaosb, prcopt_t *popt,
                    int selssr, int sateph, int vendor)
{
    int i,j;
    int udcnt=0;
    double vp=MAXAGESSRL6;
    char *p;

    /* auto or l6e */
    if ((popt->pppsatcb==0 || popt->pppsatcb==1)
        && (vendor == SSR_VENDOR_L6)
        && (sateph==EPHOPT_SSRAPC || sateph==EPHOPT_SSRCOM)) {
        udcnt = cpsatcb(gt, vp, nav->ssr[selssr], &nav->osb,
                        BIAS_VALID_L6);
    }
     if (0 < udcnt || popt->pppsatcb ==1) {
        trace(3,"%s l6e update satellite code bias cnt=%d vp=%.f\n", 
            time_str(gt,0), udcnt, vp);
        if ( popt->unbias==0 ) popt->unbias=10; /* don't use uncorrected satellite code/phase bias */
        return ;
    }
    if ( popt->unbias==10 ) popt->unbias=0; /* use uncorrected satellite code/phase bias */
    
    /* auto or bia */
    if ((popt->pppsatcb==0 || popt->pppsatcb==2) && 
        timediff(gt,biaosb->gt[0]) <= popt->maxbiasdt) {
        for (i=0;i<MAXSAT;i++) {
            for (j=0;j<MAXCODE;j++) {
                if (biaosb->vscb[i][j]==BIAS_INVALID) continue;
                nav->osb.vscb[i][j]=BIAS_VALID_BIA;
                nav->osb.scb[i][j]=-1*biaosb->scb[i][j];
                udcnt++;
            }
        }
        if (0 < udcnt) {
            nav->osb.gt[0]=biaosb->gt[0];
        }
    }
    if (0 < udcnt || popt->pppsatcb ==2) {
        trace(3,"%s bia update satellite code bias cnt=%d dt=%.f\n", 
            time_str(gt,0), udcnt, popt->maxbiasdt);
        return ;
    }
    
    /* auto or dcb */
    if (popt->pppsatcb==0 || popt->pppsatcb==3) {
        for (i=0;i<MAXSAT;i++) {
            /* P1->P2 is not used, so it is excluded from processing */
            /* C1->P1 */
            if (nav->cbias[i][1]!=0.0) {
                nav->osb.vscb[i][CODE_L1C-1] = BIAS_VALID_DCB;
                nav->osb.scb[i][CODE_L1C-1] = nav->cbias[i][1];
                udcnt++;
            }
            /* C2->P2 */
            if (nav->cbias[i][2]!=0.0) {
                nav->osb.vscb[i][CODE_L2C-1] = BIAS_VALID_DCB;
                nav->osb.scb[i][CODE_L2C-1] = nav->cbias[i][2];
                udcnt++;
            }
        }
    }
    if (0 < udcnt || popt->pppsatcb ==3) {
        trace(3,"%s dcb update satellite code bias cnt=%d\n", 
            time_str(gt,0), udcnt);
        return ;
    }

    /* auto or rtcm */
    if ((popt->pppsatcb==0 || popt->pppsatcb==4)
        && (vendor == SSR_VENDOR_RTCM)
        && (sateph==EPHOPT_SSRAPC || sateph==EPHOPT_SSRCOM)) {
        if ((p=strstr(popt->rtcmopt,"-RTCM_CB_VALID="))&&
            sscanf(p,"-RTCM_CB_VALID=%lf",&vp)==1) {
        }
        udcnt = cpsatcb(gt, vp, nav->ssr[selssr], &nav->osb,
                        BIAS_VALID_RTCM);
    }
    if (0 < udcnt || popt->pppsatcb ==4) {
        trace(3,"%s rtcm update satellite code bias cnt=%d vp=%.f\n", 
            time_str(gt,0), udcnt, vp);
        return ;
    }
    trace(3,"%s no update satellite code bias \n", time_str(gt,0));
    return ;
}
/* copy satellite phase bias -------------------------------------------------*/
static int cpsatpb(gtime_t gt, int vp, const ssr_t *ssr, osb_t *osb, int valid)
{
    int i,j;
    int sys;
    int udcnt=0;
    int ssrcode=CODE_NONE;

    for (i=0;i<MAXSAT;i++) {
        sys=satsys(i+1,NULL);
        if (timediff(gt,ssr[i].t0[5])>vp) continue;
        for (j=0;j<MAXCODE;j++) {
            ssrcode=j+1;
            if (ssr[i].vendor==SSR_VENDOR_L6) {
                ssrcode=mcssr_sel_biascode(sys,j+1);
            }
            else if (ssr[i].vendor==SSR_VENDOR_RTCM) {
                ssrcode=rtcm_sel_biascode(sys,j+1);
            }
            if (ssrcode==CODE_NONE) continue;
            if (sys==SYS_GPS && ssrcode==CODE_L5Q) {
                if (ssr[i].vpbias[ssrcode-1]==BIAS_INVALID&&
                    ssr[i].vpbias[CODE_L5X-1]!=BIAS_INVALID) ssrcode=CODE_L5X;
            }
            if (ssr[i].vpbias[ssrcode-1]==BIAS_INVALID) continue;
            osb->vspb[i][j] = valid;
            osb->spb[i][j]  = ssr[i].pbias[ssrcode-1];
            if (valid == BIAS_VALID_RTCM) {
                osb->spb[i][j]*=-1;
            }
            udcnt++;
        }
        if (0 < udcnt) {
            osb->gt[0]=ssr[i].t0[5];
        }
    }
    return udcnt;
}
/* copy osb satellite phase bias ---------------------------------------------*/
static int cpsatpb_osb(nav_t *nav,osb_t *biaosb,int target_sys,int idx,int src_code,int dst_code)
{
    if (satsys(idx+1,NULL)==target_sys &&
         biaosb->vspb[idx][dst_code-1]==BIAS_INVALID &&
        biaosb->vspb[idx][src_code-1]!=BIAS_INVALID) {
        /* copy phase bias from src_code to dst_code */
        nav->osb.vspb[idx][dst_code-1]=BIAS_VALID_BIA;
        nav->osb.spb[idx][dst_code-1]=-1*biaosb->spb[idx][src_code-1];
        return 1;
    }
    return 0;
}
static int missing_satpb(nav_t *nav,osb_t *biaosb)
{
    int i,cnt=0;
    for (i=0;i<MAXSAT;i++) {
        if (satsys(i+1,NULL)==SYS_GPS) {
            cnt+=cpsatpb_osb(nav,biaosb,SYS_GPS,i,CODE_L1W,CODE_L1C);
        }else if (satsys(i+1,NULL)==SYS_GAL) {
            cnt+=cpsatpb_osb(nav,biaosb,SYS_GAL,i,CODE_L1C,CODE_L1X);
            cnt+=cpsatpb_osb(nav,biaosb,SYS_GAL,i,CODE_L5Q,CODE_L5X);
        }else if (satsys(i+1,NULL)==SYS_QZS) {
            cnt+=cpsatpb_osb(nav,biaosb,SYS_QZS,i,CODE_L1L,CODE_L1X);
            cnt+=cpsatpb_osb(nav,biaosb,SYS_QZS,i,CODE_L5Q,CODE_L5X);
        }else if (satsys(i+1,NULL)==SYS_CMP) {
            cnt+=cpsatpb_osb(nav,biaosb,SYS_CMP,i,CODE_L1P,CODE_L1X);
            cnt+=cpsatpb_osb(nav,biaosb,SYS_CMP,i,CODE_L5P,CODE_L5X);
        }
    }
    return cnt;
}
/* update satellite phase bias -----------------------------------------------*/
static void udsatpb(gtime_t gt,nav_t *nav,osb_t *biaosb, osb_t *fcbosb,
                    prcopt_t *popt,int selssr, int sateph, int vendor)
{
    int i,j;
    int udcnt=0;
    double vp=MAXAGESSRL6;
    char *p;
    
    /* auto or l6e */
    if ((popt->pppsatpb==0 || popt->pppsatpb==1)
        && (vendor == SSR_VENDOR_L6)
        && (sateph==EPHOPT_SSRAPC || sateph==EPHOPT_SSRCOM)) {
        if ((p=strstr(popt->rtcmopt,"-SSR_MAXAGE="))&&
            sscanf(p,"-SSR_MAXAGE=%lf",&vp)==1) {
        }
        udcnt = cpsatpb(gt, vp, nav->ssr[selssr], &nav->osb, 
                        BIAS_VALID_L6);
    }
    if (0 < udcnt || popt->pppsatpb ==1) {
        trace(4,"%s ssr update satellite phase bias cnt=%d vp=%.f\n", 
            time_str(gt,0), udcnt, vp);
        if ( popt->unbias==0 ) popt->unbias=10; /* don't use uncorrected satellite code/phase bias */
        return ;
    }
    if ( popt->unbias==10 ) popt->unbias=0; /* use uncorrected satellite code/phase bias */

    /* auto or bia */
    if((popt->pppsatpb==0 || popt->pppsatpb==2) &&
        (timediff(gt,biaosb->gt[0]) <= popt->maxbiasdt || popt->maxbiasdt < 0 )) {
        for(i=0;i<MAXSAT;i++){
            for(j=0;j<MAXCODE;j++){
                if(biaosb->vspb[i][j]==BIAS_INVALID) continue;
                nav->osb.vspb[i][j]=BIAS_VALID_BIA;
                nav->osb.spb[i][j]=-1*biaosb->spb[i][j];
                udcnt++;
            }
        }
        udcnt+=missing_satpb(nav,biaosb);
        if(0 < udcnt){
            nav->osb.gt[0]=biaosb->gt[0];
        }
    }
    if(0 < udcnt || popt->pppsatpb ==2){
        trace(4,"%s bia update satellite phase bias cnt=%d dt=%.f\n", 
            time_str(gt,0), udcnt, popt->maxbiasdt);
        return ;
    }

    /* auto or fcb */
    if ((popt->pppsatpb==0 || popt->pppsatpb==3) && 
        timediff(gt,fcbosb->gt[0])<=popt->maxbiasdt) {
        for (i=0;i<MAXSAT;i++) {
            for (j=0;j<MAXCODE;j++) {
                if (fcbosb->vspb[i][j]==BIAS_INVALID) continue;
                nav->osb.vspb[i][j]=BIAS_VALID_FCB;
                nav->osb.spb[i][j]=-1*fcbosb->spb[i][j];
                udcnt++;
            }
        }
        if (0 < udcnt) {
            nav->osb.gt[0]=fcbosb->gt[0];
        }
    }
    if (0 < udcnt || popt->pppsatpb ==3) {
        trace(4,"%s fcb update satellite phase bias cnt=%d\n", 
            time_str(gt,0), udcnt);
        return ;
    }

    /* auto or rtcm */
    if ((popt->pppsatpb==0 || popt->pppsatpb==4)
        && (vendor == SSR_VENDOR_RTCM)
        && (sateph==EPHOPT_SSRAPC || sateph==EPHOPT_SSRCOM)) {
        if ((p=strstr(popt->rtcmopt,"-RTCM_PB_VALID="))&&
            sscanf(p,"-RTCM_PB_VALID=%lf",&vp)==1) {
        }
        udcnt = cpsatpb(gt, vp, nav->ssr[selssr], &nav->osb, 
                        BIAS_VALID_RTCM);
    }
    if (0 < udcnt || popt->pppsatpb ==4) {
        trace(4,"%s rtcm update satellite phase bias cnt=%d vp=%.f\n", 
            time_str(gt,0), udcnt, vp);
        return ;
    }
    trace(3,"%s no update satellite phase bias \n", time_str(gt,0));
    return ;
}
/* update station code bias --------------------------------------------------*/
static void udstacb(gtime_t gt,nav_t *nav,osb_t *biaosb, const prcopt_t *popt)
{
    int i,j;
    int udcnt=0;
    
    /* auto or bia */
    if (timediff(gt,biaosb->gt[1]) <= popt->maxbiasdt) {
        for (i=0;i<MAXBSNXSYS;i++) {
            for (j=0;j<MAXCODE;j++) {
                if (biaosb->vrsyscb[i][j]!=0) {
                    nav->osb.vrsyscb[i][j]=BIAS_VALID_BIA;
                    nav->osb.rsyscb[i][j]=-1*biaosb->rsyscb[i][j];
                    udcnt++;
                }
            }
        }
        if (0 < udcnt) {
            nav->osb.gt[1]=biaosb->gt[1];
        }
        for (i=0;i<MAXSAT;i++) {
            for (j=0;j<MAXCODE;j++) {
                if (biaosb->vrsatcb[i][j]!=0) {
                    nav->osb.vrsatcb[i][j]=BIAS_VALID_BIA;
                    nav->osb.rsatcb[i][j]=-1*biaosb->rsatcb[i][j];
                    udcnt++;
                }
            }
        }
        if (0 < udcnt) {
            nav->osb.gt[1]=biaosb->gt[1];
        }
    }
    
    if (0 < udcnt) {
        trace(4,"%s bia update station code bias cnt=%d dt=%.f\n", 
            time_str(gt,0), udcnt, popt->maxbiasdt);
        return ;
    }
    
    trace(3,"%s no update station code bias \n", time_str(gt,0));
    return ;
}
/* update biass --------------------------------------------------------------*/
static void udbiass(gtime_t gt, prcopt_t *popt, nav_t *nav, int selssr,
                    int sateph)
{
    char path[1024];
    osb_t biaosb;
    osb_t fcbosb;
    int i;
    int ret;
    int vendor=SSR_VENDOR_L6;

    /* check the vendor */
    for (i=0;i<MAXSAT;i++) {
        if (!nav->ssr[selssr][i].vendor) continue;
        if (nav->ssr[selssr][i].vendor == SSR_VENDOR_RTCM) {
            vendor=SSR_VENDOR_RTCM;
            break;
        }
        else if (nav->ssr[selssr][i].vendor == SSR_VENDOR_L6) {
            vendor=SSR_VENDOR_L6;
            break;
        }
    }
    /* update bia file data */
    if (strlen(nav->biapath)>0) {
        reppath(nav->biapath,path,gt,"","");
        if (strcmp(nav->pr_biapath,path)) {
            if (readbsnx(path) != 0) {
                trace(3,"update bias sinex file data %s -> %s\n",
                      nav->pr_biapath, path);
                strncpy(nav->pr_biapath,path,sizeof(nav->pr_biapath));
            }
        }
        /* update satellite osb */
        if ((ret=udosb_sat(&biaosb, gt, 0)) == 0) {
            if (popt->maxbiasdt>0) {
                if ((ret=udosb_sat(&biaosb, gt, 1)) != 0) {
                    if (timediff(gt,biaosb.gt[0])>popt->maxbiasdt) ret=0;
                }
            }
        }
        if (ret == 0) {
            memset(biaosb.scb,0x00,sizeof(biaosb.scb));
            memset(biaosb.spb,0x00,sizeof(biaosb.scb));
            memset(biaosb.vscb,0x00,sizeof(biaosb.vscb));
            memset(biaosb.vspb,0x00,sizeof(biaosb.vscb));
        }
        trace(4, "update satellite osb: %s num=%d\n", time_str(gt,3), ret);
            
        /* update station osb */
        if ((ret=udosb_station(&biaosb, gt, 0, popt->staname)) == 0) { 
            if (popt->maxbiasdt>0) {
                if ((ret=udosb_station(&biaosb, gt, 1, popt->staname)) != 0) {
                    if (timediff(gt,biaosb.gt[1])>popt->maxbiasdt) ret=0;
                }
            }
        }
        if (ret==0) {
            memset(biaosb.rsyscb,0x00,sizeof(biaosb.rsyscb));
            memset(biaosb.rsatcb,0x00,sizeof(biaosb.rsatcb));
            memset(biaosb.vrsyscb,0x00,sizeof(biaosb.vrsyscb));
            memset(biaosb.vrsatcb,0x00,sizeof(biaosb.vrsatcb));
        }
        trace(4, "update station osb: %s num=%d\n", time_str(gt,3),  ret);
    }
    /* update fcb file data  */
    if (strlen(nav->fcbpath)>0) {
        reppath(nav->fcbpath,path, gt,"","");
        if (strcmp(nav->pr_fcbpath,path)) {
            if (readfcb(path)!=0) {
                trace(3,"update fcb product file data %s -> %s\n",
                      nav->pr_fcbpath, path);
                strncpy(nav->pr_fcbpath,path,sizeof(nav->pr_fcbpath));
            }
        }
        /* update satellite fcb */
        if ((ret=udfcb_sat(&fcbosb, gt, 0)) == 0) { 
            if (popt->maxbiasdt>0) {
                if ((ret=udfcb_sat(&fcbosb, gt, 1)) != 0) {
                    if (timediff(gt,fcbosb.gt[0])>popt->maxbiasdt) ret=0;
                }
            }
        }
        if (ret == 0) {
            memset(fcbosb.spb,0x00,sizeof(fcbosb.scb));
            memset(fcbosb.vspb,0x00,sizeof(fcbosb.vscb));
        }
        trace(4, "update satellite fcb: %s num=%d\n", time_str(gt,3),  ret);
    }
    
    if (popt->subopt!=SUBPPP_OFF) {
            memset(nav->osb.vscb,0x00,sizeof(nav->osb.vscb));
            memset(nav->osb.scb,0x00,sizeof(nav->osb.scb));
            memset(nav->osb.vspb,0x00,sizeof(nav->osb.vspb));
            memset(nav->osb.spb,0x00,sizeof(nav->osb.spb));
    }

    /* update satellite code bias and station codebias correction */
    udsatcb(gt, nav, &biaosb, popt, selssr, sateph, vendor);
    udsatpb(gt, nav, &biaosb, &fcbosb, popt, selssr, sateph, vendor);
    udstacb(gt, nav, &biaosb, popt);
}
/* output PPP kalman filter state/covariance ---------------------------------*/
static void outkf(rtk_t *rtk, int stat, char *str)
{
    int i;
    int f=0;
    int stattmp;
    char buff[4096];
    char *p=buff;

    if (rtk->opt.pppkf<1) return;
    if (stat==SOLQ_FIX_WL) {
        stattmp=SOLQ_PPP;
    }
    else stattmp=stat;
    p+=sprintf(p,"\n%s,%d",str,stattmp);
    if (rtk->opt.dynamics==DY_OFF) {
        for (i = 0; i < 3; i++) {
            p+=sprintf(p,",%15.3f",rtk->x[i]);
        }
    }
    else if (rtk->opt.dynamics==DY_DYNA) {
        for (i = 0; i < 3; i++) {
            p+=sprintf(p,",%15.3f",rtk->x[i]);
        }
        for (i = 3; i < 6; i++) {
            p+=sprintf(p,",%10.3f",rtk->x[i]);
        }
        for (i = 6; i < 9; i++) {
            p+=sprintf(p,",%7.3f",rtk->x[i]);
        }
    }
    else if (rtk->opt.dynamics==DY_INS) {
        for (i = 0; i < 6; i++) {
            p+=sprintf(p,",%7.3f",rtk->x[i]);
        }
        for (i = 6; i < 9; i++) {
            p+=sprintf(p,",%7.3f",rtk->x[i]*R2D);
        }
        for (i = 9; i < 16; i++) {
            p+=sprintf(p,",%7.3f",rtk->x[i]);
        }
    }
    for (i = IC(0,&rtk->opt); i < IC(0,&rtk->opt)+NC(&rtk->opt); i++) {
        p+=sprintf(p,",%7.3f",rtk->x[i]*M2NS);
    }
    if (rtk->opt.tropopt==TROPOPT_EST||rtk->opt.tropopt==TROPOPT_ESTG) {
        i=IT(&rtk->opt);
        p+=sprintf(p,",%6.3f",rtk->x[i+1]);
    }
    if (rtk->opt.tropopt==TROPOPT_ESTG) {
        i=IT(&rtk->opt);
        p+=sprintf(p,",%6.3f,%6.3f",rtk->x[i+1],rtk->x[i+2]);
    }
    if (rtk->opt.ionoopt==IONOOPT_EST) {
        for (i=II(1,&rtk->opt);i<=II(MAXSAT,&rtk->opt);i++) {
            p+=sprintf(p,",%7.3f",rtk->x[i]);
        }
    }
    if (rtk->opt.navsys&SYS_CMP) {
        i=ID(0,&rtk->opt);
        p+=sprintf(p,",%7.3f,%7.3f,%7.3f",
                    rtk->x[i]*M2NS,rtk->x[i+1]*M2NS,rtk->x[i+2]*M2NS);
    }
    if (0<N3(&rtk->opt)) {
        for (i=I3(0,&rtk->opt); i<I3(0,&rtk->opt)+N3(&rtk->opt); i++) {
            p+=sprintf(p,",%7.3f",rtk->x[i]);
        }
    }
    if (0<N4(&rtk->opt)) {
        for (i=I4(0,&rtk->opt); i<I4(0,&rtk->opt)+N4(&rtk->opt); i++) {
            p+=sprintf(p,",%7.3f",rtk->x[i]);
        }
    }
    fputs(buff, rtk->fp_kf);
    memset(buff, 0, sizeof(buff));
    p=buff;
    for (f=0; f<NF(&rtk->opt); f++) {
        for (i=IB(1,f,&rtk->opt);i<=IB(MAXSAT,f,&rtk->opt);i++) {
            p+=sprintf(p,",%10.3f",rtk->x[i]);
        }
        fputs(buff, rtk->fp_kf);
        memset(buff, 0, sizeof(buff));
        p=buff;
    }
    if (rtk->opt.pppkf<2) return;
    for (i = 0; i < rtk->nx; i++) {
        p+=sprintf(p,",%14.6e",rtk->P[i+i*rtk->nx]);
        fputs(buff, rtk->fp_kf);
        memset(buff, 0, sizeof(buff));
        p=buff;
    }
}
/* output kalman filter header -----------------------------------------------*/
static void outkfheader(rtk_t *rtk,int ocflag)
{
    char buff[4096];
    char *p=buff;
    char id[32];
    int i,j;

    if (ocflag==KF_OPEN) {
        p+=sprintf(p,"%-15s%8s,%s"
                  ,(rtk->opt.pppkf==1?"#(states)":"#(states+dcov)"),"GPST","Q");
        if (rtk->opt.dynamics==DY_OFF) {
            p+=sprintf(p,",%15s,%15s,%15s","POS-X","POS-Y","POS-Z");
        }
        else if (rtk->opt.dynamics==DY_DYNA) {
            p+=sprintf(p,",%15s,%15s,%15s,%10s,%10s,%10s,%7s,%7s,%7s",
                   "POS-X","POS-Y","POS-Z","VEL-X","VEL-Y","VEL-Z",
                   "ACC-X","ACC-Y","ACC-Z");
        }
        else if (rtk->opt.dynamics==DY_INS) {
            p+=sprintf(p,",%7s,%7s,%7s,%7s,%7s,%7s,%7s,%7s,%7s,%7s,%7s,%7s,"
                       "%7s,%7s,%7s,%7s",
                       "dPOS-X","dPOS-Y","dPOS-Z","dVEL-X","dVEL-Y","dVEL-Z",
                       "dATT-R","dATT-P","dATT-H","dACCB-X","dACCB-Y","dACCB-Z",
                       "dGYRB-R","dGYRB-P","dGYRB-H","CLKD");
        }
        p+=sprintf(p,",%7s,%7s,%7s,%7s,%7s,%7s,%7s",
                   "CLKGPS","CLKGLO","CLKGAL","CLKQZS","CLKBDS","CLKIRN","CLKBDS2");
        if (rtk->opt.tropopt==TROPOPT_EST) {
            p+=sprintf(p,",%6s","TRP");
        }
        if (rtk->opt.tropopt==TROPOPT_ESTG) {
            p+=sprintf(p,",%6s,%6s,%6s","TRP","GRAD-N","GRAD-E");
        }
        if (rtk->opt.ionoopt==IONOOPT_EST) {
            for (i=1; i<=MAXSAT; i++) {
                satno2id(i,id);
                p+=sprintf(p,",ION-%s",id);
            }
        }
        fputs(buff, rtk->fp_kf);
        memset(buff, 0, sizeof(buff));
        p=buff;
        if (rtk->opt.navsys&SYS_CMP) {
            for (i=1; i<4; i++) {
                p+=sprintf(p,", BDSB-%d",i);
            }
        }
        if (0<N3(&rtk->opt)) {
            for (i=0; i<N3(&rtk->opt); i++) {
                p+=sprintf(p,", IFB3-%d",i+1);
            }
        }
        if (0<N4(&rtk->opt)) {
            for (i=0; i<N4(&rtk->opt); i++) {
                p+=sprintf(p,", IFB4-%d",i+1);
            }
        }
        for (i=1;i<=NF(&rtk->opt);i++) {
            for (j=1; j<=MAXSAT; j++) {
                satno2id(j,id);
                p+=sprintf(p,",  AMB%d-%s",i,id);
            }
            fputs(buff, rtk->fp_kf);
            memset(buff, 0, sizeof(buff));
            p=buff;
        }
        if (rtk->opt.pppkf<2) return;
        fprintf(rtk->fp_kf,",       ");
    }
    if (ocflag==KF_CLOSE)fprintf(rtk->fp_kf,"\n#(ccov)");
    if (rtk->opt.dynamics==DY_OFF) {
        p+=sprintf(p,"%7s,%14s,%14s","cPOS-X","cPOS-Y","cPOS-Z");
    }
    else if (rtk->opt.dynamics==DY_DYNA) {
        p+=sprintf(p,"%7s,%14s,%14s,%14s,%14s,%14s,%14s,%14s,%14s",
                   "cPOS-X","cPOS-Y","cPOS-Z","cVEL-X","cVEL-Y","cVEL-Z",
                   "cACC-X","cACC-Y","cACC-Z");
    }
    else if (rtk->opt.dynamics==DY_INS) {
        p+=sprintf(p,"%7s,%14s,%14s,%14s,%14s,%14s,%14s,%14s,%14s"
                   ",%14s,%14s,%14s,%14s,%14s,%14s,%14s",
                   "cdPOS-X","cdPOS-Y","cdPOS-Z","cdVEL-X","cdVEL-Y","cdVEL-Z",
                   "cdATT-R","cdATT-P","cdATT-H","cdACCB-X","cdACCB-Y",
                   "cdACCB-Z","cdGYRB-R","cdGYRB-P","cdGYRB-H","cCLKD");
    }
    p+=sprintf(p,",%14s,%14s,%14s,%14s,%14s,%14s,%14s",
        "cCLKGPS","cCLKGLO","cCLKGAL","cCLKQZS","cCLKBDS","cCLKIRN","cCLKBDS2");
    if (rtk->opt.tropopt==TROPOPT_EST) {
        p+=sprintf(p,",%14s","cTRP");
    }
    if (rtk->opt.tropopt==TROPOPT_ESTG) {
        p+=sprintf(p,",%14s,%14s,%14s","cTRP","cGRAD-N","cGRAD-E");
    }
    if (rtk->opt.ionoopt==IONOOPT_EST) {
        for (i=1; i<=MAXSAT; i++) {
            satno2id(i,id);
            p+=sprintf(p,",      cION-%s",id);
        }
    }
    fputs(buff, rtk->fp_kf);
    memset(buff, 0, sizeof(buff));
    p=buff;
    if (rtk->opt.navsys==SYS_CMP) {
        for (i=1; i<4; i++) {
            p+=sprintf(p,",       cBDSB-%d",i);
        }
    }
    if (0<N3(&rtk->opt)) {
        for (i=0; i<N3(&rtk->opt); i++) {
            p+=sprintf(p,",       cIFB3-%d",i+1);
        }
    }
    if (0<N4(&rtk->opt)) {
        for (i=0; i<N4(&rtk->opt); i++) {
            p+=sprintf(p,",       cIFB4-%d",i+1);
        }
    }
    for (i=1;i<=NF(&rtk->opt);i++) {
        for (j=1; j<=MAXSAT; j++) {
            satno2id(j,id);
            p+=sprintf(p,",     cAMB%d-%s",i,id);
        }
        fputs(buff, rtk->fp_kf);
        memset(buff, 0, sizeof(buff));
        p=buff;
    }
}
/* open kalman filter file -----------------------------------------------------
* open kalman filter file 
* args   : rtk_t    *rtk    IO   RTK control/result struct
*          filopt_t *fopt   I    file options (see rtklib.h)
* return : none
*-----------------------------------------------------------------------------*/
extern void openkffile(rtk_t *rtk, const filopt_t *fopt)
{

    rtk->fp_kf=NULL;
    if (rtk->opt.pppkf<1) return;
    if (!(rtk->fp_kf=fopen(fopt->kf,"w"))) {
        trace(2,"openkffile err\n");
        return;
    }
    outkfheader(rtk,KF_OPEN);
}
/* close kalman filter file ----------------------------------------------------
* close kalman filter file 
* args   : rtk_t    *rtk    IO   RTK control/result struct
* return : none
*-----------------------------------------------------------------------------*/
extern void closekffile(rtk_t *rtk)
{
    int i,j;

    if (rtk->opt.pppkf==3) {
        outkfheader(rtk,KF_CLOSE);
        fprintf(rtk->fp_kf,"\n");
        for (i=0; i<rtk->nx; i++) {
            for (j=0; j<rtk->nx; j++) {
                fprintf(rtk->fp_kf,"%14.6e,",rtk->P[i*rtk->nx+j]);
            }
            fprintf(rtk->fp_kf,"\n");
        }
    }
    if (rtk->fp_kf!=NULL) {fclose(rtk->fp_kf);}
}
/* precise point positioning -------------------------------------------------*/
static void pppos(rtk_t *rtk, const obsd_t *obs, int n, nav_t *nav, int sateph, 
                  int ephreset)
{
    const prcopt_t *opt=&rtk->opt;
    double *rs,*dts,*var,*v,*H,*R,*azel,*xp,*Pp,*mxp,*mPp,*ecef;
    double dclk,drr[3],std[3];
    double dr[3]={0};
    char str[32];
    int ret=0;
    int i,j,nv,info,svh[MAXOBS];
    int exc[MAXOBS]={0},excd[MAXOBS]={0};
    int vendor=SSR_VENDOR_NONE,stat=SOLQ_SINGLE;
    int nvaldata=0;
    static int old_vendor=SSR_VENDOR_NONE;
    double ion[MAXSAT]={0.0};
    
    time2str(obs[0].time,str,3);
    trace(3,"pppos   : time=%s nx=%d n=%d\n",str,rtk->nx,n);
    
    rs=mat(6,n); dts=mat(2,n); var=mat(1,n); azel=zeros(2,n);
    
    /* satellite positions and clocks */
    nvaldata=satposs(obs[0].time,obs,n,nav,sateph,rtk->selssr,rs,dts,var,svh);
    if (!rtk->pppflg && nvaldata>0) rtk->pppflg=1; 
    
    if (ephreset!=0) {
        trace(3,"pppos : change ephemeris %s eph=%d selssr=%d\n", 
              str, sateph, rtk->selssr);
    }
    /* exclude measurements of eclipsing satellite (block IIA) */
    if (rtk->opt.posopt[3]) {
        testeclipse(obs,n,nav,rs);
    }
    /* earth tides correction */
    if (opt->tidecorr) {
        if (opt->dynamics==DY_INS) {
            ecef=rtk->insd.ecef;
        }
        else{
            ecef=rtk->x;
        }
        tidedisp(gpst2utc(obs[0].time),ecef,opt->tidecorr==1?1:7,&nav->erp,
                 opt->odisp[0],dr);
    }
    nv=n*rtk->opt.nf*3+MAXSAT+3;
    mxp=mat(rtk->nx,1); mPp=zeros(rtk->nx,rtk->nx);
    xp=mat(rtk->nx,1); Pp=zeros(rtk->nx,rtk->nx);
    v=mat(nv,1); H=mat(rtk->nx,nv); R=mat(nv,nv);

    matcpy(mxp,rtk->x,rtk->nx,1);
    matcpy(mPp,rtk->P,rtk->nx,rtk->nx);
    
    /* update code/phase bias */
    udbiass(obs[0].time, &rtk->opt, nav, rtk->selssr, sateph);

    /* detect discontinuity in bias */
    detdisc_bias(rtk, obs, n, nav, rtk->selssr);

    /* temporal update of clock */
    if (old_vendor == vendor && sateph == rtk->opt.sateph) ephreset=0;
    udclk_ppp(rtk,ephreset,mxp,mPp);

    /* temporal update of BeiDou2-isb parameters */
    if ((rtk->opt.navsys&SYS_CMP) && (rtk->opt.bds2bias == 1)) {
        udbds2b_ppp(rtk,ephreset,mxp,mPp);
    }
    /* temporal update of 3rd/4th-freqency-receiver-bias parameters */
    if (NF(&rtk->opt)>=3) {
        udifb_ppp(rtk,ephreset,mxp,mPp);
    }

    /* temporal update of ionospheric parameters */
    if (rtk->opt.ionoopt==IONOOPT_EST) {
        udiono_ppp(rtk,obs,n,rs,nav,ephreset,mxp,mPp,ion);
    }

    /* temporal update of phase-bias */
    udbias_ppp(rtk,obs,n,nav,ephreset,mxp,mPp);
    
    if (0<n) {
        if (opt->dynamics==DY_INS) {
            /* distance between the pnt solution and in ins navigation */
            rtk->sol.resetins=0;
            for (i=0;i<3;i++) drr[i]=rtk->sol.rr[i]-(rtk->insd.ecef[i]-rtk->x[i]);
            dclk=0.0;
            if (opt->clkopt!=0 && rtk->x[IC(0,&rtk->opt)]!=0.0) {
                dclk=fabs(rtk->sol.dtr[0]*CLIGHT-rtk->x[IC(0,&rtk->opt)]);
            }
            if (!rtk->sol.chisq && rtk->opt.maxdist!=0.0 &&
               (norm(drr,3)>rtk->opt.maxdist||dclk>rtk->opt.maxdist)) {
                trace(2,"pppos:reset ins navigation posdiff=%.3f clkdiff=%.3f > %.3f %.3f %.3f\n",
                    norm(drr,3),dclk,rtk->opt.maxdist,rtk->sol.dtr[0]*CLIGHT,
                    rtk->x[IC(0,&rtk->opt)]);
                rtk->sol.resetins=1;
            }
        }
        for (i=0;i<MAX_ITER;i++) {
            
            matcpy(xp,mxp,rtk->nx,1);
            matcpy(Pp,mPp,rtk->nx,rtk->nx);
            
            /* prefit residuals */
            if (!(nv=ppp_res(0,obs,n,rs,dts,var,svh,dr,exc,excd,nav,xp,rtk,v,H,R,azel))) {
                trace(2,"%s ppp (%d) no valid obs data\n",str,i+1);
                break;
            }
            /* measurement update of ekf states */
            if ((info=filter(xp,Pp,H,v,R,rtk->nx,nv))) {
                trace(2,"%s ppp (%d) filter error info=%d\n",str,i+1,info);
                break;
            }
            /* postfit residuals */
            if (ppp_res(i+1,obs,n,rs,dts,var,svh,dr,exc,excd,nav,xp,rtk,v,H,R,azel)) {
                matcpy(rtk->x,xp,rtk->nx,1);
                matcpy(rtk->P,Pp,rtk->nx,rtk->nx);
                stat=SOLQ_PPP;
                break;
            }
        }
        if (i>=MAX_ITER) {
            trace(2,"%s ppp (%d) iteration overflows\n",str,i);
        }
        if (stat==SOLQ_PPP) {
            /* ambiguity resolution in ppp */
            ret=ppp_ar(rtk,obs,n,exc,nav,azel,xp,Pp);
            if (ret&&
                ppp_res(9,obs,n,rs,dts,var,svh,dr,exc,excd,nav,xp,rtk,v,H,R,azel)) {
                
                matcpy(rtk->xa,xp,rtk->nx,1);
                matcpy(rtk->Pa,Pp,rtk->nx,rtk->nx);
                
                for (i=0;i<3;i++) std[i]=sqrt(Pp[i+i*rtk->nx]);
                if (rtk->opt.car==0) {
                    if (norm(std,3)<MAX_STD_FIX) stat=SOLQ_FIX;
                }
                else {
                    if (ret>1) stat=SOLQ_FIX;    /* narrow-lane fixed */
                    else       stat=SOLQ_FIX_WL; /* only wide-lane fixed */
                }   
            }
            else {
                rtk->nfix=0;
            }
            /* update solution status */
            update_stat(rtk,obs,n,stat,nav,0);

            for (j=0;j<MAXSAT;j++) {
                rtk->ssat[j].ionc=ion[j];
            }
            
            /* hold fixed ambiguities */
            if (stat==SOLQ_FIX&&test_hold_amb(rtk)) {
                matcpy(rtk->x,xp,rtk->nx,1);
                matcpy(rtk->P,Pp,rtk->nx,rtk->nx);
                trace(2,"%s hold ambiguity\n",str);
                rtk->nfix=0;
            }
            if (opt->dynamics==DY_INS && rtk->insd.insstat==INS_INACT) {
                rtk->insd.insstat=INS_INIT;
            }
            old_vendor = vendor;
            outkf(rtk,stat,str);
        }
    }
    else{
        matcpy(rtk->x,mxp,rtk->nx,1);
        matcpy(rtk->P,mPp,rtk->nx,rtk->nx);
    }
    free(rs); free(dts); free(var); free(azel);
    free(mxp); free(mPp); free(xp); free(Pp); free(v); free(H); free(R);
}
/* precise point positioning ---------------------------------------------------
* input observation data and navigation message, compute receiver position by
* precise point positioning (PPP)
* args   : rtk_t *rtk       IO  RTK control/result struct
*          obsd_t *obs      I   observation data for an epoch
*                               obs[i].rcv=1:rover
*                               sorted by receiver and satellite
*          int    n         I   number of observation data
*          int    dr        I   dead reckoning mode(0:nominal,1:dr)
*          nav_t  *nav      I   navigation messages
* return : none
* notes  : before calling this function, initial position should be set in
*          rtk->sol.rr[] or rtk->x[] as appropriate
*-----------------------------------------------------------------------------*/
extern void ppp(rtk_t *rtk, const obsd_t *obs, int n, int dr, nav_t *nav)
{
    int i,j;
    int sateph=-1;
    char str[32];
    int ephreset=0;

    time2str(rtk->sol.time,str,2);
    trace(3,"ppp : time=%s nx=%d n=%d aop=%d\n",str,rtk->nx,n,dr);

    for (i=0;i<MAXSAT;i++) {
        for (j=0;j<rtk->opt.nf&&j<NFREQ;j++) {
            rtk->ssat[i].fix[j]=0;
        }
    }

    /* temporal update of ekf states */
    udstate_ppp(rtk,obs,n,nav,dr);
    if (dr) {
        pppos(rtk,obs,n,nav,rtk->opt.sateph,ephreset);
        if (rtk->insd.insstat==INS_ACT) {
            /* dead reackoning */
            update_stat(rtk,obs,n,SOLQ_DGPS,nav,1);
            rtk->sol.stat=SOLQ_DGPS;
            rtk->sol.sub=SUBPPP_DR;
        }
        else {
            rtk->sol.stat=SOLQ_NONE;
            rtk->sol.sub=SUBPPP_DR;
        }
        return;
    }

    /* primary PPP positioning using main antenna correction data */
    if (rtk->sol.sub!=SUBPPP_OFF) {
        trace(4,"ppp : eph reset main %s %d \n",time_str(obs[0].time,3),rtk->sol.sub);
        ephreset=1;
    }
    rtk->selssr=STRSSR_MAIN;
    pppos(rtk,obs,n,nav,rtk->opt.sateph,ephreset);
    if (rtk->sol.stat!=SOLQ_NONE) {
        rtk->sol.sub=SUBPPP_OFF;
        return;
    }
    
    /* substitute PPP positioning without SSR correction data */
    switch (rtk->opt.subopt) {
        case SUBPPP_BRDC: sateph=EPHOPT_BRDC; break;
        case SUBPPP_RT  : sateph=rtk->opt.sateph; rtk->selssr=STRSSR_SUB; break;
        case SUBPPP_OFF : break;
        default         : break;
    }
    if (sateph<0) {
        rtk->insd.clkstat=0;
        return;
    }
    
    /* substitute PPP positioning without SSR correction data */
    ephreset=0;
    if (rtk->sol.sub!=rtk->opt.subopt) {
        trace(4,"ppp : eph reset sub %s %d \n",time_str(obs[0].time,3),rtk->sol.sub);
        ephreset=1;
    }
    
    /* recalculate by precise ephemeris */
    pppos(rtk,obs,n,nav,sateph,ephreset);
    if (rtk->sol.stat!=SOLQ_NONE) {
        if (rtk->sol.stat==SOLQ_PPP) rtk->sol.stat=SOLQ_DGPS;
        rtk->sol.sub=rtk->opt.subopt;
    }
    else{
        rtk->insd.clkstat=0;
        return;
    }
}
