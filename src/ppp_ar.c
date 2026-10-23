/*------------------------------------------------------------------------------
* ppp_ar.c : ppp ambiguity resolution
*
* Copyright (C) 2024-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
*
* reference :
*    [1] H.Okumura, C-gengo niyoru saishin algorithm jiten (in Japanese),
*        Software Technology, 1991
*
*          Copyright (C) 2012-2015 by T.TAKASU, All rights reserved.
*
* history : 2013/03/11  1.0  new
*           2015/05/15  1.1  refine complete algorithms
*           2015/05/31  1.2  delete WL-ambiguity resolution by ILS
*                            add PAR (partial ambiguity resolution)
*           2015/11/26  1.3  support option opt->pppopt=-TRACE_AR
*           2024/02/01  1.4  port from previous version of MALIB
*                            support option opt->arsys in gen_sat_sd()
*           2024/12/20  1.5  delete L5-receiver-dcb estimation
*                            add BeiDou-2 ISB estimation
*           2025/03/24  1.6  support ins navigation
*           2026/02/27  1.7  fix bug keep sat1/sat2/frq aligned after shrink
*                            support triple/quad-frequency PPP-AR
*                            support BeiDou-2 and BeiDou-3
*           2026/03/26  1.8  fix bug derive wavelengths from observed signal 
*                             codes in get_wavelength()
*-----------------------------------------------------------------------------*/
#include "rtklib.h"

#define MIN_AMB_RES 4         /* min number of ambiguities for ILS-AR */
#define MIN_ARC_GAP 300.0     /* min arc gap (s) */
#define CONST_AMB   0.003     /* constraint to fixed ambiguity */

#define LOG_PI      1.14472988584940017 /* log(pi) */
#define SQRT2       1.41421356237309510 /* sqrt(2) */

#define CONST_AMB_CAR     0.01     /* constraint to fixed ambiguity for CAR*/
#define CONST_AMB_BDS     0.05     /* constraint to fixed ambiguity for BDS2,BDS3 */
#define CONST_AMB_BDS_B2a 0.10     /* constraint to fixed ambiguity for BDS3 B2a */
#define CONST_AMB_GAL_E6  0.10     /* constraint to fixed ambiguity for GAL E6 */
#define CONST_AMB_EWL     0.10     /* constraint to fixed ewl ambiguity for GPS,BDS */
#define MAX_STD_FIX       0.15     /* max std-dev (3d-pos) to fix solution */
#define MIN_MATCH_RATE    0.10     /* min match rate between the first and second candidates to fix solution */
#define REL_MATCH_RATE    0.90     /* match rate between the first and second candidates to relax threshold */
#define MAX_FRAC_WL_FIX   0.20     /* max fraction to fix wide-lane ambiguity (cycle) */
#define MAX_STD_WL_FIX    1.00     /* max std-dev to fix wide-lane ambiguity (cycle) */

#define STEP_EWL          3
#define STEP_WL           2
#define STEP_NL           1

#define MIN(x,y)    ((x)<(y)?(x):(y))
#define MAX(x,y)    ((x)>(y)?(x):(y))
#define SQR(x)      ((x)*(x))
#define ROUND(x)    (int)floor((x)+0.5)
#define SWAP_I(x,y) do {int    _tmp=x; x=y; y=_tmp;} while (0)
#define SWAP_D(x,y) do {double _tmp=x; x=y; y=_tmp;} while (0)

/* number and index of ekf states */
#define NF(opt)     ((opt)->ionoopt==IONOOPT_IFLC?1:(opt)->nf)
#define NP(opt)     ((opt)->dynamics==DY_DYNA?9:((opt)->dynamics==DY_INS?16:3))
#define NC(opt)     (CLKNSYS)
#define NT(opt)     ((opt)->tropopt<TROPOPT_EST?0:((opt)->tropopt==TROPOPT_EST?1:3))
#define NI(opt)     ((opt)->ionoopt==IONOOPT_EST?MAXSAT:0)
#define ND(opt)     ((opt)->navsys&SYS_CMP?3:0)
#define N3(opt)     (NF(opt)>=3?NC(opt):0)   /* 3rd-frequency receivre bias */
#define N4(opt)     (NF(opt)>=4?NC(opt):0)   /* 4th-frequency receivre bias */
#define NR(opt)     (NP(opt)+NC(opt)+NT(opt)+NI(opt)+ND(opt)+N3(opt)+N4(opt))
#define IB(s,f,opt) (NR(opt)+MAXSAT*(f)+(s)-1)

/* global variables ----------------------------------------------------------*/
static const nav_t *_nav;

#if 0
/* complementaty error function (ref [1] p.227-229) --------------------------*/
static double q_gamma(double a, double x, double log_gamma_a);
static double p_gamma(double a, double x, double log_gamma_a)
{
    double y,w;
    int i;
    
    if (x==0.0) return 0.0;
    if (x>=a+1.0) return 1.0-q_gamma(a,x,log_gamma_a);
    y=w=exp(a*log(x)-x-log_gamma_a)/a;
    for (i=1;i<100;i++) {
        w*=x/(a+i);
        y+=w;
        if (fabs(w)<1E-15) break;
    }
    return y;
}
static double q_gamma(double a, double x, double log_gamma_a)
{
    double y,w,la=1.0,lb=x+1.0-a,lc;
    int i;
    
    if (x<a+1.0) return 1.0-p_gamma(a,x,log_gamma_a);
    w=exp(-x+a*log(x)-log_gamma_a);
    y=w/lb;
    for (i=2;i<100;i++) {
        lc=((i-1-a)*(lb-la)+(i+x)*lb)/i;
        la=lb; lb=lc;
        w*=(i-1-a)/i;
        y+=w/la/lb;
        if (fabs(w/la/lb)<1E-15) break;
    }
    return y;
}
static double f_erfc(double x)
{
    return x>=0.0?q_gamma(0.5,x*x,LOG_PI/2.0):1.0+p_gamma(0.5,x*x,LOG_PI/2.0);
}
/* confidence function of integer ambiguity ----------------------------------*/
static double conf_func(int N, double B, double var)
{
    double x,p=1.0,sig=sqrt(var);
    int i;
    
    x=fabs(B-N);
    for (i=1;i<6;i++) {
        p-=f_erfc((i-x)/(SQRT2*sig))-f_erfc((i+x)/(SQRT2*sig));
    }
    return p;
}
#endif
/* generate satellite SD (single-difference) ---------------------------------*/
static int gen_sat_sd(rtk_t *rtk, const obsd_t *obs, int n, const int *exc,
                      const double *azel, int f, int *sat1, int *sat2, int *frq)
{
    int sys[4],s=0;
    double elmask,el[MAXOBS];
    int i,j,k,m,ns=0,sat[MAXOBS];
    
    if (rtk->opt.arsys & SYS_GPS) sys[s++]=SYS_GPS;
    if (rtk->opt.arsys & SYS_GAL) sys[s++]=SYS_GAL;
    if (rtk->opt.arsys & SYS_QZS) sys[s++]=SYS_QZS;
    sys[s]=0;

    elmask=MAX(rtk->opt.elmaskar,rtk->opt.elmin);
    
    for (i=0;sys[i];i++) { /* for each system */
        
        /* sort by elevation angle */
        for (j=m=0;j<n;j++) {
            if (exc[j]||!(satsys(obs[j].sat,NULL)&sys[i])||
                !rtk->ssat[obs[j].sat-1].vsat[f]||azel[1+j*2]<elmask) continue;
            if (obs[j].L[f]==0.0) continue;
            sat[m]=obs[j].sat;
            el[m++]=azel[1+j*2];
        }
        for (j=0;j<m-1;j++) for (k=j+1;k<m;k++) {
            if (el[j]>=el[k]) continue;
            SWAP_I(sat[j],sat[k]); SWAP_D(el[j],el[k]);
        }
        /* generate SD referenced to max elevation angle */
        for (j=1;j<m;j++) {
            sat1[ns]=sat[j];
            sat2[ns]=sat[0];
            frq[ns++]=f;
        }
    }
    return ns; /* # of SD */
}
#if 0
/* filter EWL-ambiguity ------------------------------------------------------*/
static void filter_EWL(rtk_t *rtk, const obsd_t *obs, int n, const int *exc,
                       const nav_t *nav, const double *azel)
{
    const double *lam;
    ambc_t *amb;
    double lamE,lamN,EW,var,K;
    int i,sat,sys;
    
    for (i=0;i<n;i++) {
        sat=obs[i].sat;
        sys=satsys(sat,NULL);
        if (sys!=SYS_GPS&&sys!=SYS_QZS) continue;
        amb=rtk->ambc+sat-1;
        lam=nav->lam[sat-1];
        
        if (rtk->ssat[sat-1].slip[1]||rtk->ssat[sat-1].slip[2]||
            fabs(timediff(amb->epoch[0],obs[i].time))>MIN_ARC_GAP) {
            amb->n[1]=0;
        }
        if (exc[i]||azel[1+2*i]<rtk->opt.elmin||lam[1]==0.0||lam[2]==0.0||
            obs[i].L[1]==0.0||obs[i].P[2]==0.0||
            obs[i].L[1]==0.0||obs[i].P[2]==0.0) continue;
        
        /* EMW-LC and variance */
        lamE=lam[1]*lam[2]/(lam[2]-lam[1]);
        lamN=lam[1]*lam[2]/(lam[2]+lam[1]);
        EW=lamE*(obs[i].L[1]-obs[i].L[2])-
           lamN*(obs[i].P[1]/lam[1]+obs[i].P[2]/lam[2]);
        var=SQR(0.7*rtk->opt.eratio[0])*
            (SQR(rtk->opt.err[1])+SQR(rtk->opt.err[2]/sin(azel[1+2*i])));
        
        /* filter EWL-ambiguity */
        if (amb->n[1]<=0) {
            amb->LC[1]=EW;
            amb->LCv[1]=SQR(10.0);
            amb->n[1]=1;
            amb->epoch[1]=obs[i].time;
        }
        else if (SQR(EW-amb->LC[1])<=amb->LCv[1]*25.0) {
            K=amb->LCv[1]/(amb->LCv[1]+var);
            amb->LC[1]+=K*(EW-amb->LC[1]);
            amb->LCv[1]-=K*amb->LCv[1];
            amb->n[1]++;
            amb->epoch[1]=obs[i].time;
        }
    }
}
/* filter WL-ambiguity -------------------------------------------------------*/
static void filter_WL(rtk_t *rtk, const obsd_t *obs, int n, const int *exc,
                      const nav_t *nav, const double *azel)
{
    const double *lam;
    ambc_t *amb;
    double lamW,lamN,MW,var,K;
    int i,l,sat;
    
    for (i=0;i<n;i++) {
        sat=obs[i].sat;
        amb=rtk->ambc+sat-1;
        lam=nav->lam[sat-1];
        l=satsys(sat,NULL)==SYS_GAL?2:1; /* L1/L2 or L1/L5 */
        
        if (rtk->ssat[sat-1].slip[0]||rtk->ssat[sat-1].slip[l]||
            fabs(timediff(amb->epoch[0],obs[i].time))>MIN_ARC_GAP) {
            amb->n[0]=0;
        }
        if (exc[i]||azel[1+2*i]<rtk->opt.elmin||lam[0]==0.0||lam[l]==0.0||
            obs[i].L[0]==0.0||obs[i].P[0]==0.0||
            obs[i].L[l]==0.0||obs[i].P[l]==0.0) continue;
        
        /* MW-LC and variance */
        lamW=lam[0]*lam[l]/(lam[l]-lam[0]);
        lamN=lam[0]*lam[l]/(lam[l]+lam[0]);
        MW=lamW*(obs[i].L[0]-obs[i].L[l])-
           lamN*(obs[i].P[0]/lam[0]+obs[i].P[l]/lam[l]);
        var=SQR(0.7*rtk->opt.eratio[0])*
            (SQR(rtk->opt.err[1])+SQR(rtk->opt.err[2]/sin(azel[1+2*i])));
        
        /* filter WL-ambiguity */
        if (amb->n[0]<=0) {
            amb->LC[0]=MW;
            amb->LCv[0]=SQR(10.0);
            amb->n[0]=1;
            amb->epoch[0]=obs[i].time;
        }
        else if (SQR(MW-amb->LC[0])<=amb->LCv[0]*25.0) {
            K=amb->LCv[0]/(amb->LCv[0]+var);
            amb->LC[0]+=K*(MW-amb->LC[0]);
            amb->LCv[0]-=K*amb->LCv[0];
            amb->n[0]++;
            amb->epoch[0]=obs[i].time;
        }
    }
}
/* ambiguity resolution by WL/NL for iono-free LC ----------------------------*/
static int ppp_amb_IFLC(rtk_t *rtk, const obsd_t *obs, int n, int *exc,
                        const nav_t *nav, const double *azel, double *x,
                        double *P)
{
    const double *lam;
    ambc_t *amb1,*amb2;
    double lamN,lamW,lamE,C1,C2,Be,Bw,B1,varw,var1;
    double v[MAXOBS],R[MAXOBS*MAXOBS]={0},*H,*thres=rtk->opt.thresar;
    int i,j,k,l,info,ns,nw=0,na=0,Ne,Nw,N1,sat1[MAXOBS],sat2[MAXOBS],frq[MAXOBS];
    
    /* filter EWL-ambiguity */
    filter_EWL(rtk,obs,n,exc,nav,azel);
    
    /* filter WL-ambiguity */
    filter_WL(rtk,obs,n,exc,nav,azel);
    
    /* generate satellite SD */
    if (!(ns=gen_sat_sd(rtk,obs,n,exc,azel,0,sat1,sat2,frq))) return 0;
    
    H=zeros(rtk->nx,ns);
    
    for (i=0;i<ns;i++) {
        lam=nav->lam[sat1[i]-1];
        l=satsys(sat1[i],NULL)==SYS_GAL?2:1; /* L1/L2 or L1/L5 */
        lamE=lam[1]*lam[2]/(lam[2]-lam[1]);
        lamW=lam[0]*lam[l]/(lam[l]-lam[0]);
        lamN=lam[0]*lam[l]/(lam[l]+lam[0]);
        C1= SQR(lam[l])/(SQR(lam[l])-SQR(lam[0]));
        C2=-SQR(lam[0])/(SQR(lam[l])-SQR(lam[0]));
        
        j=IB(sat1[i],0,&rtk->opt);
        k=IB(sat2[i],0,&rtk->opt);
        amb1=rtk->ambc+sat1[i]-1;
        amb2=rtk->ambc+sat2[i]-1;
        if (!amb1->n[0]||!amb2->n[0]) continue;
        
        if (amb1->n[1]&&amb2->n[1]) {
            Be=(amb1->LC[1]-amb2->LC[1])/lamE;
            Ne=ROUND(Be);
#if 0
            trace(2,"%s sat=%2d-%2d:Be=%13.4f Fe=%7.4f\n",time_str(obs[0].time,0),
                  sat1[i],sat2[i],Be,Be-Ne);
#endif
        }
        /* round WL- and L1-ambiguity */
        Bw=(amb1->LC[0]-amb2->LC[0])/lamW;
        Bw+=nav->wlbias[sat1[i]-1]-nav->wlbias[sat2[i]-1]; /* correct WL-bias */
        Nw=ROUND(Bw);
        B1=(x[j]-x[k]+C2*lam[l]*Nw)/lamN;
        N1=ROUND(B1);
        varw=(amb1->LCv[0]+amb2->LCv[0])/SQR(lamW);
        var1=(P[j+j*rtk->nx]+P[k+k*rtk->nx]-2.0*P[j+k*rtk->nx])/SQR(lamN);
        
        if (conf_func(Nw,Bw,varw)<thres[1]||fabs(Nw-Bw)>thres[2]) continue;
        nw++;
        if (conf_func(N1,B1,var1)<thres[1]||fabs(N1-B1)>thres[3]) continue;
        
        /* constraint to fixed LC-ambiguty */
        H[j+na*rtk->nx]= 1.0;
        H[k+na*rtk->nx]=-1.0;
        v[na++]=C1*lam[0]*N1+C2*lam[l]*(N1-Nw)-(rtk->x[j]-rtk->x[k]);
        
        /* update fix flags */
        rtk->ssat[sat1[i]-1].fix[0]=rtk->ssat[sat2[i]-1].fix[0]=2;
    }
    rtk->sol.age  =(float)nw; /* # of WL-fixed */
    rtk->sol.ratio=(float)na; /* # of NL-fixed */
    
    if (na<=0) {
        free(H);
        return 0;
    }
    for (i=0;i<na;i++) R[i+i*na]=SQR(CONST_AMB);
    
    /* update states with fixed LC-ambiguity constraints */
    if ((info=filter(x,P,H,v,R,rtk->nx,na))) {
        trace(1,"filter error (info=%d)\n",info);
        free(H);
        return 0;
    }
    free(H);
    return 1;
}
#endif
/* write debug trace for PAR -------------------------------------------------*/
static void write_trace1(rtk_t *rtk, const double *Z, const double *a,
                         const double *Q, int na, const int *sat1,
                         const int *sat2, const int *frq)
{
    const char freq[]="125678";
    char buff[1024],s[32],*p=buff;
    int i,j;
    
    trace(2,"EPOCH=%s NFIX=%d\n",time_str(rtk->sol.time,0),rtk->nfix);
    
    for (i=0,p=buff;i<na;i++) {
        satno2id(sat1[i],s); p+=sprintf(p,"%s ",s);
    }
    trace(2,"     %s          Z*a     STD\n",buff);
    for (i=0,p=buff;i<na;i++) {
        satno2id(sat2[i],s); p+=sprintf(p,"%s ",s);
    }
    trace(2,"     %s         (cyc)   (cyc)\n",buff);
    for (i=0,p=buff;i<na;i++) {
        p+=sprintf(p,"L%c  ",freq[frq[i]]);
    }
    trace(2,"      %s\n",buff);
    for (i=na-1;i>=0;i--) {
        p=buff;
        p+=sprintf(p,"%3d: ",na-i);
        for (j=0;j<na;j++) p+=sprintf(p,"%3.0f ",Z[j+i*na]);
        p+=sprintf(p,"%14.3f %7.3f",a[i],sqrt(Q[i+i*na]));
        trace(2,"%s\n",buff);
    }
    trace(2,"%3s: %7s %9s (%9s/%9s) [ N1 N2 ... NN ]\n","FIX","STD-POS","RATIO",
          "S1","S2");
}
static void write_trace2(rtk_t *rtk, const double *x, const double *P,
                         const double *a, const double *N, const double *D,
                         int na, const double *s)
{
    double *xp,*Pp,b[256],R[256*256]={0},std[3];
    char buff[1024],*p=buff;
    int i;
    
    xp=mat(rtk->nx,1); Pp=mat(rtk->nx,rtk->nx);
    matcpy(xp,x,rtk->nx,1);
    matcpy(Pp,P,rtk->nx,rtk->nx);
    for (i=0;i<na;i++) {
        b[i]=N[i]-a[i];
        R[i+i*na]=SQR(CONST_AMB);
    }
    for (i=na-1;i>=0;i--) {
        p+=sprintf(p,"%s%d",i==na-1?"":" ",(int)N[i]);
    }
    if (!filter(xp,Pp,D,b,R,rtk->nx,na)) {
        for (i=0;i<3;i++) std[i]=sqrt(Pp[i+i*rtk->nx]);
        trace(2,"%3d: %7.3f %9.3f (%9.3f/%9.3f) [%s]\n",na,norm(std,3),
              MIN(99999.999,s[1]/s[0]),s[0],s[1],buff);
    }
    free(xp); free(Pp);
}
/* decorrelate search space --------------------------------------------------*/
static int decorr_space(rtk_t *rtk, double *a, double *Q, double *D, int na,
                        const int *sat1, const int *sat2, const int *frq)
{
    double *W=mat(na,rtk->nx),*Z=eye(na);
    
    /* lambda reduction */
    if (lambda_reduction(na,Q,Z)) {
        free(W); free(Z);
        return 0;
    }
    /* a=Z'*a, Q=Z'*Q*Z, D'=Z'*D' */
    matmul("TN",na,1,na,1.0,Z,a,0.0,W);
    matcpy(a,W,na,1);
    matmul("TN",na,na,na,1.0,Z,Q,0.0,W);
    matmul("NN",na,na,na,1.0,W,Z,0.0,Q);
    matmul("NN",rtk->nx,na,na,1.0,D,Z,0.0,W);
    matcpy(D,W,rtk->nx,na);
    
    if (strstr(rtk->opt.pppopt,"-TRACE_AR")) {
        write_trace1(rtk,Z,a,Q,na,sat1,sat2,frq);
    }
    free(W); free(Z);
    return 1;
}
/* shrink search space -------------------------------------------------------*/
static int shrink_space(double *a, double *Q, double *H, int *sat1, int *sat2, 
                        int *frq, int is, int na, int nx)
{
    int i,j,n=0,*index=imat(na,1);
    
    for (i=is;i<na;i++) {
        index[n++]=i;
    }
    for (i=0;i<n;i++) {
        a[i]=a[index[i]];
        for (j=0;j<n;j++) Q[j+i*n]=Q[index[j]+index[i]*na];
        matcpy(H+i*nx,H+index[i]*nx,nx,1);

        sat1[i]=sat1[index[i]];
        sat2[i]=sat2[index[i]];
        frq[i]=frq[index[i]];
    }
    free(index);
    return n;
}
/* freq-ambiguity resolution by ILS ------------------------------------------*/
static int ppp_amb_ILS_FRQ(rtk_t *rtk, const obsd_t *obs, int n, const int *exc,
                           const nav_t *nav, const double *azel,
                           const double *x, const double *P, double *D,
                           double *a, double *N)
{
#if 0
    const double *lam,thres_fact[]={3.0,2.5,2.0,1.5,1.2};
#else
    const double thres_fact[]={3.0,2.5,2.0,1.5,1.2};
#endif
    double *W,*Q,s[2],thres=0.0;
    int i,j,k,l,na=0,sat1[MAXOBS*NFREQ],sat2[MAXOBS*NFREQ],frq[MAXOBS*NFREQ];
    int trace_AR=0;
    
    if (strstr(rtk->opt.pppopt,"-TRACE_AR")) trace_AR=1;
    
    /* generate satellite SD */
    for (i=0;i<rtk->opt.nf;i++) {
        if (rtk->opt.freqopt==1&&i==1) continue;
        na+=gen_sat_sd(rtk,obs,n,exc,azel,i,sat1+na,sat2+na,frq+na);
    }
    if (na<=0) return 0;
    
    /* freq-ambiguity SD-matrix */
    for (i=0;i<na;i++) {
#if 0
        lam=nav->lam[sat1[i]-1];
#else
        for (l=0;l<n;l++)if (sat1[i]==obs[l].sat)break;
#endif

        j=IB(sat1[i],frq[i],&rtk->opt);
        k=IB(sat2[i],frq[i],&rtk->opt);
#if 0
        D[j+i*rtk->nx]= 1.0/lam[frq[i]];
        D[k+i*rtk->nx]=-1.0/lam[frq[i]];
#else
        D[j+i*rtk->nx]= 1.0/(CLIGHT/sat2freq(obs[l].sat,obs[l].code[frq[i]],nav));
        D[k+i*rtk->nx]=-1.0/(CLIGHT/sat2freq(obs[l].sat,obs[l].code[frq[i]],nav));
#endif
    }
    /* a=D'*x, Q=D'*P*D */
    W=mat(rtk->nx,na); Q=mat(na,na);
    matmul("TN",na,1,rtk->nx,1.0,D,x,0.0,a);
    matmul("TN",na,rtk->nx,rtk->nx,1.0,D,P,0.0,W);
    matmul("NN",na,na,rtk->nx,1.0,W,D,0.0,Q);
    
    /* decorrelate search space */
    if (!decorr_space(rtk,a,Q,D,na,sat1,sat2,frq)) {
        free(W); free(Q);
        return 0;
    }
    for (i=0;i<rtk->opt.armaxiter&&na>=MIN_AMB_RES;i++) {
        
        /* integer least-square (a->N) */
        if (lambda_search(na,2,a,Q,N,s)||s[0]<=0.0) {
            free(W); free(Q);
            return 0;
        }
        if (trace_AR) {
            write_trace2(rtk,x,P,a,N,D,na,s);
        }
        thres=rtk->opt.thresar[0];
        if (na-MIN_AMB_RES<5) thres*=thres_fact[na-MIN_AMB_RES];
        
        /* validation by ratio-test */
        if (s[1]/s[0]>=thres) break;
        
        /* shrink search space */
        na=shrink_space(a,Q,D,sat1,sat2,frq,1,na,rtk->nx);
    }
    free(W); free(Q);
    
    rtk->sol.ratio=(float)MIN(s[1]/s[0],999.9);
    rtk->sol.thres=(float)thres;
    
    if (i>=rtk->opt.armaxiter||na<MIN_AMB_RES) return 0;
    
    /* update fix flags */
    for (i=0;i<na;i++) {
        rtk->ssat[sat1[i]-1].fix[frq[i]]=rtk->ssat[sat2[i]-1].fix[frq[i]]=2;
    }
    return na;
}
/* ambiguity resolution by ILS (integer-least-square) ------------------------*/
static int ppp_amb_ILS(rtk_t *rtk, const obsd_t *obs, int n, int *exc,
                       const nav_t *nav, const double *azel, double *x,
                       double *P)
{
    double *D,*R,a[MAXOBS*NFREQ],N[MAXOBS*NFREQ*2];
    int i,na,info;
    
    D=zeros(rtk->nx,n*NF(&rtk->opt));
    
    /* freq-ambiguity resolution */
    if (!(na=ppp_amb_ILS_FRQ(rtk,obs,n,exc,nav,azel,x,P,D,a,N))) {
        free(D);
        return 0;
    }
    R=zeros(na,na);
    
    /* update states with integer ambiguity constraints */
    for (i=0;i<na;i++) {
        a[i]=N[i]-a[i];
        R[i+i*na]=SQR(CONST_AMB);
    }
    if ((info=filter(x,P,D,a,R,rtk->nx,na))) {
        trace(1,"filter error (info=%d)\n",info);
    }
    free(D); free(R);
    return info?0:1;
}
/* get wave length -----------------------------------------------------------*/
static void get_wavelength(int sat, const obsd_t *obs, int n, double *lam)
{
    double freq;
    int i,s,sys;
    
    for (i=0;i<NFREQ;i++) {
        lam[i]=0.0;
    }
    sys=satsys_bd2(sat,NULL,_nav->pcvs[sat-1].type);
    for (s=0;s<n;s++) {
        if (sat==obs[s].sat) break;
    }
    if (s>=n) return;
    for (i=0;i<NFREQ;i++) {
        freq=code2freq(sys, obs[s].code[i], 0);
        if (freq>0.0) lam[i]=CLIGHT/freq;
    }
}
/* exclude satellite ---------------------------------------------------------*/
static int exc_sat_sd(rtk_t *rtk, const obsd_t *obs, int exc,
                        double el, int nf, int sys)
{
    double elmask;
    int arsys=rtk->opt.arsys;
    
    elmask=MAX(rtk->opt.elmaskar,rtk->opt.elmin);
    
    trace(4,"exc_sat_sd: nf=%d arsys=%d elmaskar=%2.0f\n",
        nf,arsys,rtk->opt.elmaskar);
    
    if (exc) return 0;
    if (!(satsys_bd2(obs->sat,NULL,_nav->pcvs[obs->sat-1].type)&sys)) return 0;
    if (!(satsys(obs->sat,NULL)&arsys)) return 0;
    if (!rtk->ssat[obs->sat-1].vsat[0]||el<elmask) {
        trace(4,"exc_sat_sd: sys=%2d sat=%3d excluded by el=%4.1f / %4.1f\n",
            satsys_bd2(obs->sat,NULL,_nav->pcvs[obs->sat-1].type),obs->sat,el*R2D,elmask*R2D);
        return 0;
    }
    trace(4,"exc_sat_sd: sat=%d\n",obs->sat);
    
    return 1;
}
/* generate satellite SD (single-difference) for CAR -------------------------*/
static int gen_sat_sd_CAR(rtk_t *rtk, const obsd_t *obs, int n, const int *exc,
                      const double *azel, int nf, int *sat1, int *sat2, 
                      double *el1, double *el2)
{
    const int sys[]={SYS_GPS,SYS_GAL,SYS_CMP,SYS_QZS,SYS_BD2,0};
    double el[MAXOBS],maxel;
    int i,j,k,m,f,ns=0,sat[MAXOBS];
    int stat=0,maxsat,excmask[MAXOBS];
    char s1[5],s2[5];
    char satid[8];
    
    trace(3,"gen_sat_sd_CAR: nf=%d arsys=%d\n",nf,rtk->opt.arsys);
    
    for (i=0;sys[i];i++) { /* for each system */
        
        for (j=0;j<n;j++) excmask[j]=0;
        
        /* search highest elevation angle */
        for (j=0,maxsat=0,maxel=0.0;j<n;j++) {
            
            if (!exc_sat_sd(rtk,obs+j,exc[j],azel[1+j*2],nf,sys[i])) {
                excmask[j]=1;
                continue;
            }
            for (f=0,stat=0;f<2;f++) {
                stat|=(obs[j].L[f]==0.0);
                trace(4,"gen_sat_sd_CAR: sys=%d sat=%3d f=%d obs[j].L[f]=%14.3f stat=%d\n",
                    sys[i],obs[j].sat,f,obs[j].L[f],stat);
            }
            if (stat) continue;
            
            /* exclude IGSO for reference satellite */
            if (strncmp(_nav->pcvs[obs[j].sat-1].type,"BEIDOU-3I",9)==0) {
                satno2id(obs[j].sat, satid);
                trace(4,"gen_sat_sd_CAR: excluded BEIDOU3-IGSO sat=%s\n",satid);
                continue;
            }
            
            if (azel[1+j*2]>maxel) {
                maxel =azel[1+j*2];
                maxsat=obs[j].sat;
            }
        }
        if (maxsat==0) {
            trace(4,"gen_sat_sd_CAR: no reference satellite sys=%d maxsat=%3d maxel=%4.1f\n",
                    sys[i],maxsat,maxel*R2D);
            continue;
        }
        trace(4,"gen_sat_sd_CAR: sys=%d maxsat=%3d maxel=%4.1f\n",
                    sys[i],maxsat,maxel*R2D);
        
        /* sort by elevation angle */
        for (j=m=0;j<n;j++) {
            if (excmask[j]) continue;
            if (obs[j].sat==maxsat) continue;
            
            sat[m]=obs[j].sat;
            el[m++]=azel[1+j*2];
        }
        for (j=0;j<m-1;j++) for (k=j+1;k<m;k++) {
            if (el[j]>=el[k]) continue;
            SWAP_I(sat[j],sat[k]); SWAP_D(el[j],el[k]);
        }
        /* generate SD referenced to max elevation angle */
        for (j=0;j<m;j++) {
            el1[ns]=el[j];
            el2[ns]=maxel;
            
            sat1[ns  ]=sat[j];
            sat2[ns++]=maxsat;
            
            satno2id(sat[j],s1);
            satno2id(maxsat,s2);
            trace(3,"gen_sat_sd_CAR: %s %s el=%4.1f %4.1f[deg]\n",
                s1,s2,el[j]*R2D,maxel*R2D);
        }
    }
    trace(4,"gen_sat_sd_CAR: ns=%d\n",ns);
    return ns; /* # of Satellite */
}
/* write debug trace for CAR -------------------------------------------------*/
static void write_trace_amb(rtk_t *rtk, const double *a, const double *Q, int na, 
                            const int *sat1s, const int *sat2s, const int *frqs, 
                            const char label[NFREQ][10], int step)
{
    char s1[32],s2[32];
    double ep[6]={0.0};
    gtime_t t0;
    int i;
    trace(2,"write_trace_amb: EPOCH=%s\n",time_str(rtk->sol.time,0));
    
    time2epoch(timeadd(rtk->sol.time,1.0),ep);
    ep[3]=0.0;ep[4]=0.0;ep[5]=0.0;
    t0=epoch2time(ep);
    
    for (i=0;i<na;i++) {
        satno2id(sat1s[i],s1);
        satno2id(sat2s[i],s2);
        trace(2,"write_trace_amb:, STEP_%d,  %5.0f, %s_%s, %s, %14.3f, %7.3f, %7.3f\n",
            step,timediff(rtk->sol.time,t0),s2,s1,label[frqs[i]],a[i],
            sqrt(Q[i+i*na]),ROUND(a[i])-a[i]);
    }
}
/* write debug trace for CAR -------------------------------------------------*/
static void write_trace_sd_amb(rtk_t *rtk, const obsd_t *obs, int obsn, 
                               const double *x, const double *P, 
                              const int *sat1, const int *sat2, int ns, int nf,
                              int *sat1s, int *sat2s, int *frqs,
                              double *a, int post, int step)
{
    int i,f,j,k,j1,j2,k1,k2,na=0;
    double *D,*Q,*W;
    double lam[NFREQ];
    const char label1[NFREQ][10]={"PRE-N1 ","PRE-WL ","PRE-EWL","PRE-EW2"};
    const char label2[NFREQ][10]={"POST-N1 ","POST-WL ","POST-EWL","POST-EW2"};
    
    if (!strstr(rtk->opt.pppopt,"-TRACE_AR")) {
        return;
    }
    trace(4,"write_trace_sd_amb: ns=%d nf=%d\n",ns,nf);
    
    D=zeros(rtk->nx,ns*nf);
    Q=mat(ns*NFREQ,ns*nf);
    
    /* freq-ambiguity SD-matrix */
    for (i=0;i<ns;i++) {
        if (sat1[i]<=0 || sat2[i]<=0) continue;
        get_wavelength(sat1[i],obs,obsn,lam);
        
        for (f=0;f<nf;f++) {
            
            sat1s[na]=sat1[i];
            sat2s[na]=sat2[i];
            frqs [na]=f;
            
            if (f==0) { /* N1 */
                j=IB(sat1[i],0,&rtk->opt);
                k=IB(sat2[i],0,&rtk->opt);
                
                if (lam[0]==0.0) continue;
                if (x[j]==0.0||x[k]==0.0) continue;
                
                D[j+na*rtk->nx]= 1.0/lam[0];
                D[k+na*rtk->nx]=-1.0/lam[0];
                na++;
            }
            else if (f==1) { /* WL (F1-F2) */
                j1=IB(sat1[i],0,&rtk->opt);
                j2=IB(sat1[i],1,&rtk->opt);
                k1=IB(sat2[i],0,&rtk->opt);
                k2=IB(sat2[i],1,&rtk->opt);
                
                if (lam[0]==0.0||lam[1]==0.0) continue;
                if (x[j1]==0.0||x[j2]==0.0) continue;
                if (x[k1]==0.0||x[k2]==0.0) continue;
                
                D[j1+na*rtk->nx]= 1.0/lam[0];
                D[j2+na*rtk->nx]=-1.0/lam[1];
                D[k1+na*rtk->nx]=-1.0/lam[0];
                D[k2+na*rtk->nx]= 1.0/lam[1];
                na++;
            }
            else if (f==2) { /* EWL (F2-F3) */
                j1=IB(sat1[i],1,&rtk->opt);
                j2=IB(sat1[i],2,&rtk->opt);
                k1=IB(sat2[i],1,&rtk->opt);
                k2=IB(sat2[i],2,&rtk->opt);
                
                if (lam[1]==0.0||lam[2]==0.0) continue;
                if (x[j1]==0.0||x[j2]==0.0) continue;
                if (x[k1]==0.0||x[k2]==0.0) continue;
                
                D[j1+na*rtk->nx]= 1.0/lam[1];
                D[j2+na*rtk->nx]=-1.0/lam[2];
                D[k1+na*rtk->nx]=-1.0/lam[1];
                D[k2+na*rtk->nx]= 1.0/lam[2];
                na++;
            }
            else if (f==3) { /* EWL (F2-F4) */
                j1=IB(sat1[i],1,&rtk->opt);
                j2=IB(sat1[i],3,&rtk->opt);
                k1=IB(sat2[i],1,&rtk->opt);
                k2=IB(sat2[i],3,&rtk->opt);
                
                if (lam[1]==0.0||lam[3]==0.0) continue;
                if (x[j1]==0.0||x[j2]==0.0) continue;
                if (x[k1]==0.0||x[k2]==0.0) continue;
                
                D[j1+na*rtk->nx]= 1.0/lam[1];
                D[j2+na*rtk->nx]=-1.0/lam[3];
                D[k1+na*rtk->nx]=-1.0/lam[1];
                D[k2+na*rtk->nx]= 1.0/lam[3];
                na++;
            }
        }
    }
    
    /* a=D'*x, Q_wl=D'*P*D */
    W=mat(rtk->nx,na);
    matmul("TN",na,1,rtk->nx,1.0,D,x,0.0,a);
    matmul("TN",na,rtk->nx,rtk->nx,1.0,D,P,0.0,W);
    matmul("NN",na,na,rtk->nx,1.0,W,D,0.0,Q);
    
    if (post) write_trace_amb(rtk,a,Q,na,sat1s,sat2s,frqs,label2,step);
    else      write_trace_amb(rtk,a,Q,na,sat1s,sat2s,frqs,label1,step);
    
    free(W); free(D); free(Q); W=D=Q=NULL;
}
/* search extra wide-lane integer ambiguity ----------------------------------*/
static int search_amb_ewl(rtk_t *rtk, const obsd_t *obs, int obsn,
                          const double *x, const double *P, const int *sat1, 
                          const int *sat2, int ns, int nf, int *sat1s, 
                          int *sat2s, int *frqs, double *a, double *N,
                          double *D)
{
    
    int i,j1,j2,k1,k2,na=0;
    int f,f1,f2;
    double lam[NFREQ];
    double b,n,var,frac;
    double th_frac=MAX_FRAC_WL_FIX,th_std=MAX_STD_WL_FIX;
    char s1[32],s2[32];
    
    if (nf<3) return 0;
    
    /* initialize D-matrix */
    for (i=0;i<rtk->nx*(ns*2);i++) D[i]=0.0;
    
    /* freq-ambiguity SD-matrix extra WL (F2-F3, F2-F4) */
    for (i=0;i<ns;i++) {
        if (sat1[i]<=0 || sat2[i]<=0) continue;
        get_wavelength(sat1[i],obs,obsn,lam);
        satno2id(sat1[i],s1);
        satno2id(sat2[i],s2);
        
        for (f=2;f<rtk->opt.nf;f++) {
            if (f==2) {
                f1=1; /* 2nd frequency */
                f2=2; /* 3rd frequency */
            }
            else if (f==3) {
                f1=1; /* 2nd frequency */
                f2=3; /* 4th frequency */
            }
            
            j1=IB(sat1[i],f1,&rtk->opt);
            j2=IB(sat1[i],f2,&rtk->opt);
            k1=IB(sat2[i],f1,&rtk->opt);
            k2=IB(sat2[i],f2,&rtk->opt);
            
            if (lam[f1]==0.0||lam[f2]==0.0||
                x[j1]==0.0||x[j2]==0.0||x[k1]==0.0||x[k2]==0.0) {
                trace(3,"search_amb_ewl: skipped %s %s x(j1,j2,k1,k2)=%8.3f %8.3f %8.3f %8.3f\n",
                    s1,s2,x[j1],x[j2],x[k1],x[k2]);
                continue;
            }
            
            b=(x[j1]/lam[f1]-x[j2]/lam[f2])-(x[k1]/lam[f1]-x[k2]/lam[f2]);
            n=ROUND(b);
            frac=fabs(n-b);
            var= P[j1+j1*rtk->nx]/SQR(lam[f1])+P[j2+j2*rtk->nx]/
                  SQR(lam[f2])-2.0*P[j1+j2*rtk->nx]/(lam[f1]*lam[f2]);
            var+=P[k1+k1*rtk->nx]/SQR(lam[f1])+P[k2+k2*rtk->nx]/
                  SQR(lam[f2])-2.0*P[k1+k2*rtk->nx]/(lam[f1]*lam[f2]);
            
            if (sqrt(var)>th_std || frac>th_frac) {
                trace(3,"search_amb_ewl: excluded %s %s N,a= %8.3f %8.3f "
                        "sig=%8.3f frac=%8.3f/%8.3f std=%8.3f/%8.3f\n",
                    s1,s2,n,b,sqrt(var),frac,th_frac,sqrt(var),th_std);
                continue;
            }
            
            sat1s[na]=sat1[i];
            sat2s[na]=sat2[i];
            frqs [na]=f;
            
            a[na]=b;
            N[na]=n;
            D[j1+na*rtk->nx]= 1.0/lam[f1];
            D[j2+na*rtk->nx]=-1.0/lam[f2];
            D[k1+na*rtk->nx]=-1.0/lam[f1];
            D[k2+na*rtk->nx]= 1.0/lam[f2];
            rtk->ssat[sat1s[na]-1].fix[f2]=rtk->ssat[sat2s[na]-1].fix[f2]=1;
            na++;
            
            if (strstr(rtk->opt.pppopt,"-TRACE_AR")) {
                trace(3,"search_amb_ewl: %s %s N,a= %8.3f %8.3f sig=%8.3f\n",
                    s1,s2,n,b,sqrt(var));
            }
        }
    }
    trace(4,"search_amb_ewl: number of fixed extra wide-lane ambiguity = %d / %d\n",
            na,ns);
    return na;
}
/* search wide-lane integer ambiguity ----------------------------------------*/
static int search_amb_wl(rtk_t *rtk, const obsd_t *obs, int obsn, 
                         const double *x, const double *P, int *sat1, int *sat2,
                         int ns, int nf, int *sat1s, int *sat2s, int *frqs, 
                         double *a, double *N, double *D)
{
    
    int i,j1,j2,k1,k2,na=0;
    double lam[NFREQ];
    double b,n,var,frac;
    double th_frac=MAX_FRAC_WL_FIX,th_std=MAX_STD_WL_FIX;
    char s1[32],s2[32];
    
    if (nf<2) return 0;
    
    /* initialize D-matrix */
    for (i=0;i<rtk->nx*ns;i++) D[i]=0.0;
    
    /* freq-ambiguity SD-matrix WL (L1-L2) */
    for (i=0;i<ns;i++) {
        if (sat1[i]<=0 || sat2[i]<=0) continue;
        get_wavelength(sat1[i],obs,obsn,lam);
        satno2id(sat1[i],s1);
        satno2id(sat2[i],s2);
        
        j1=IB(sat1[i],0,&rtk->opt);
        j2=IB(sat1[i],1,&rtk->opt);
        k1=IB(sat2[i],0,&rtk->opt);
        k2=IB(sat2[i],1,&rtk->opt);
        
        if (lam[0]==0.0||lam[1]==0.0||
            x[j1]==0.0||x[j2]==0.0||x[k1]==0.0||x[k2]==0.0) {
            trace(3,"search_amb_wl: excluded %s %s x(j1,j2,k1,k2)=%.3f %.3f %.3f %.3f\n",
                s1,s2,x[j1],x[j2],x[k1],x[k2]);
            
            sat1[i]=0;
            sat2[i]=0;
            continue;
        }
        
        b=(x[j1]/lam[0]-x[j2]/lam[1])-(x[k1]/lam[0]-x[k2]/lam[1]);
        n=ROUND(b);
        frac=fabs(n-b);
        var= P[j1+j1*rtk->nx]/SQR(lam[0])+P[j2+j2*rtk->nx]/
              SQR(lam[1])-2.0*P[j1+j2*rtk->nx]/(lam[0]*lam[1]);
        var+=P[k1+k1*rtk->nx]/SQR(lam[0])+P[k2+k2*rtk->nx]/
              SQR(lam[1])-2.0*P[k1+k2*rtk->nx]/(lam[0]*lam[1]);
        
        if (sqrt(var)>th_std || frac>th_frac) {
            trace(3,"search_amb_wl: excluded %s %s N,a= %8.3f %8.3f "
                    "sig=%8.3f frac=%8.3f/%8.3f std=%8.3f/%8.3f\n",
                s1,s2,n,b,sqrt(var),frac,th_frac,sqrt(var),th_std);
            
            sat1[i]=0;
            sat2[i]=0;
            
            continue;
        }
        
        sat1s[na]=sat1[i];
        sat2s[na]=sat2[i];
        frqs [na]=1;
        
        a[na]=b;
        N[na]=n;
        D[j1+na*rtk->nx]= 1.0/lam[0];
        D[j2+na*rtk->nx]=-1.0/lam[1];
        D[k1+na*rtk->nx]=-1.0/lam[0];
        D[k2+na*rtk->nx]= 1.0/lam[1];
        rtk->ssat[sat1s[na]-1].fix[1]=rtk->ssat[sat2s[na]-1].fix[1]=1;
        na++;
        
        if (strstr(rtk->opt.pppopt,"-TRACE_AR")) {
            trace(3,"search_amb_wl: %s %s N,a= %8.3f %8.3f sig=%8.3f\n",
                s1,s2,n,b,sqrt(var));
        }
    }
    trace(4,"search_amb_wl: number of fixed wide-lane ambiguity = %d / %d\n",na,ns);
    return na;
}
/* generate SD-matrix of narrow-lane ambiguity -------------------------------*/
static int gen_sd_matrix_n1(rtk_t *rtk, const obsd_t *obs, int obsn, 
                            const double *x, const double *P, const int *sat1, 
                            const int *sat2, int ns, int *sat1s, int *sat2s, 
                            int *frqs, double *a, double *Q, double *D)
{
    int i,j,k,na=0,ind[MAXOBS][2],i0,i1,j0,j1;
    double q,lam[NFREQ];
    char s1[32],s2[32];
    const char label[NFREQ][10]={"N1 ","WL ","EWL","EW2"};
    unsigned int tick=tickget();
    
    /* initialize D-matrix */
    for (i=0;i<rtk->nx*ns;i++) D[i]=0.0;
    
    /* freq-ambiguity SD-matrix N1 */
    for (i=0;i<ns;i++) {
        if (sat1[i]<=0 || sat2[i]<=0) continue;
        get_wavelength(sat1[i],obs,obsn,lam);
        satno2id(sat1[i],s1);
        satno2id(sat2[i],s2);
        
        sat1s[na]=sat1[i];
        sat2s[na]=sat2[i];
        frqs [na]=0;
        
        j=IB(sat1[i],0,&rtk->opt);
        k=IB(sat2[i],0,&rtk->opt);
        
        if (lam[0]==0.0) continue;
        if (x[j]==0.0||x[k]==0.0) continue;
        
        D[j+na*rtk->nx]= 1.0/lam[0];
        D[k+na*rtk->nx]=-1.0/lam[0];
        ind[na][0]=j;
        ind[na][1]=k;
        na++;
    }
    trace(3,"gen_sd_matrix_n1: ns=%d proc_time=%d [ms]\n",
        ns,(int)(tickget()-tick));
    
    /* a=D'*x, Q=D'*P*D */
    for (j=0;j<na;j++) {
        j0=ind[j][0]; j1=ind[j][1];
        a[j]=D[j0+j*rtk->nx]*x[j0]+D[j1+j*rtk->nx]*x[j1];
        
        for (i=j;i<na;i++) {
            i0=ind[i][0]; i1=ind[i][1];
            q= D[i0+i*rtk->nx]*P[i0+j0*rtk->nx]*D[j0+j*rtk->nx];
            q+=D[i0+i*rtk->nx]*P[i0+j1*rtk->nx]*D[j1+j*rtk->nx];
            q+=D[i1+i*rtk->nx]*P[i1+j0*rtk->nx]*D[j0+j*rtk->nx];
            q+=D[i1+i*rtk->nx]*P[i1+j1*rtk->nx]*D[j1+j*rtk->nx];
            Q[i+j*na]=Q[j+i*na]=q;
        }
    }
    if (strstr(rtk->opt.pppopt,"-TRACE_AR")) {
        write_trace_amb(rtk,a,Q,na,sat1s,sat2s,frqs,label,8);
    }
    trace(3,"gen_sd_matrix_n1: na=%d proc_time=%d [ms]\n",
        na,(int)(tickget()-tick));
    return na;
}
/* search integer ambiguity by LAMBDA ----------------------------------------*/
static int search_amb_lambda(rtk_t *rtk, const int *sat1s, const int *sat2s, 
                             const int *frq, const double *Q, const double *a, 
                             double *N, int na, int *exc_sat_flag)
{
    const double thres_fact[]={5.0,5.0,3.0,2.0,1.5};
    double s[2],thres=0.0;
    int i,trace_AR=0,nm=0;
    char s1[32],s2[32],tstr[30];
    unsigned int tick=tickget();
    trace(3,"search_amb_lambda: na=%d\n",na);
    
    if (strstr(rtk->opt.pppopt,"-TRACE_AR")) trace_AR=1;
    
    time2str(rtk->sol.time,tstr,0);
    
    /* integer least-square (a->N) */
    if (lambda(na,2,a,Q,N,s)||s[0]<=0.0) {
        trace(2,"search_amb_lambda: error lambda\n");
        return 0;
    }
    
    /* threshold for ratio test */
    thres=rtk->opt.thresar[5];
    if (na-MIN_AMB_RES<(sizeof(thres_fact)/sizeof(thres_fact[0]))) {
        thres*=thres_fact[na-MIN_AMB_RES];
        trace(3,"search_amb_lambda: inflated threshold na-MIN_AMB_RES=%d thres_fact=%.2f thres=%.2f\n",
            na-MIN_AMB_RES,thres_fact[na-MIN_AMB_RES],thres);
    }
    for (i=0;i<MAXSAT;i++) exc_sat_flag[i]=0;
    for (i=0;i<na;i++) {
        if (fabs(N[i]-N[i+na])<0.001) nm++;
        else exc_sat_flag[sat1s[i]-1]=(int)fabs(N[i]-N[i+na]);
    }
    trace(3,"search_amb_lambda: %s na=%2d nm= %2d match_rate=%7.2f\n",
        tstr,na,nm,(double)nm/(double)na);
    if ((double)nm/(double)na>REL_MATCH_RATE) thres*=0.8;
    if ((double)nm/(double)na<MIN_MATCH_RATE) thres=99.99;
    
    if (trace_AR) {
        trace(4,"search_amb_lambda: na= %2d ratio= %.3f (s[1](%.3f)/s[0](%.3f)) thres= %.3f\n",
            na,s[1]/s[0],s[1],s[0],thres);
        for (i=0;i<na;i++) {
            satno2id(sat1s[i],s1);
            satno2id(sat2s[i],s2);
            trace(4,"search_amb_lambda first candidate: i= %2d na= %2d %s %s N,a= %8.3f %8.3f frac=%8.3f\n",
                i,na,s1,s2,N[i],a[i],N[i]-a[i]);
        }
        for (i=0;i<na;i++) {
            satno2id(sat1s[i],s1);
            satno2id(sat2s[i],s2);
            trace(4,"search_amb_lambda second candidate: i= %2d na= %2d %s %s N,a= %8.3f %8.3f frac=%8.3f\n",
                i,na,s1,s2,N[i+na],a[i],N[i+na]-a[i]);
        }
        for (i=0;i<na;i++) {
            satno2id(sat1s[i],s1);
            satno2id(sat2s[i],s2);
            trace(4,"search_amb_lambda difference: i= %2d na= %2d %s %s N1,N2= %8.3f %8.3f diff=%8.3f\n",
                i,na,s1,s2,N[i],N[i+na],N[i]-N[i+na]);
        }
    }
    
    rtk->sol.ratio=(float)MIN(s[1]/s[0],999.9);
    if (s[1]/s[0]<thres) {
        trace(4,"search_amb_lambda: failed to fix. na=%d ratio=%.2f/%.2f proc_time=%d [ms]\n",
            na,s[1]/s[0],thres,(int)(tickget()-tick));
        return 0;
    }
    
    /* update fix flags */
    for (i=0;i<na;i++) {
        rtk->ssat[sat1s[i]-1].fix[frq[i]]=rtk->ssat[sat2s[i]-1].fix[frq[i]]=2;
    }
    
    trace(4,"search_amb_lambda: successed to fix. na=%d ratio=%.2f/%.2f proc_time=%d [ms]\n",
            na,s[1]/s[0],thres,(int)(tickget()-tick));
    return na;
}
/* update states with integer ambiguity constraints --------------------------*/
static int update_states(double *x, double *P, const double *D, int nx, 
                         const double *N, const double *a, int na, 
                         const int *sats, const int *frqs, int step)
{
    double *R,b[MAXOBS*NFREQ*2];
    double var;
    int i,info,sys;
    unsigned int tick=tickget();
    
    R=zeros(na,na);
    for (i=0;i<na;i++) {
        var=SQR(CONST_AMB_CAR);
        
        /* de-weight for a specific system or frequency */
        sys=satsys_bd2(sats[i],NULL,_nav->pcvs[sats[i]-1].type);
        if (sys&(SYS_BD2)) {
            var=SQR(CONST_AMB_BDS);
        }
        if (sys&(SYS_CMP)) {
            var=SQR(CONST_AMB_BDS);
            if (freq_idx2freq_num(SYS_CMP,frqs[i])==5) { /* B2a */
                var=SQR(CONST_AMB_BDS_B2a); 
            }
        }
        if (step==STEP_EWL) {
            switch(sys) {
                case SYS_GPS : var=SQR(CONST_AMB_EWL); break;
                case SYS_QZS : break;
                case SYS_CMP : var=SQR(CONST_AMB_EWL); break;
                case SYS_BD2 : var=SQR(CONST_AMB_EWL); break;
                case SYS_GAL : 
                    if (freq_idx2freq_num(SYS_GAL,frqs[i])==6) { /* E6 */
                        var=SQR(CONST_AMB_GAL_E6); 
                    }
                    break;
            }
        }
        b[i]=N[i]-a[i];
        R[i+i*na]=var;
        trace(4,"update_states: sat=%3d frqs=%d fn=%d i=%2d na=%2d N,a=%10.3f %10.3f frac=%8.3f sig=%6.3f\n",
            sats[i],frqs[i],freq_idx2freq_num(sys,frqs[i]),i,na,N[i],a[i],b[i],sqrt(var));
    }
    if ((info=filter(x,P,D,b,R,nx,na))) {
        trace(1,"update_states: filter error (info=%d)\n",info);
    }
    free(R); R=NULL;
    
    trace(3,"update_states: na=%d proc_time=%d [ms]\n",
        na,(int)(tickget()-tick));
    return info;
}
/* exclude satellite for partial AR ------------------------------------------*/
static int exc_sat_par(int *sat1, int *sat2, const double *el1, 
                       const double *el2, int ns, const int *exc_sat_flag)
{
    int i,minidx=0,sys;
    double minel=9999.9;
    double el,offset=PI/2.0;
    char s1[32],s2[32];
    char satid[8];
    
    for (i=0;i<ns;i++) {
        if (!sat1[i]) continue;
        if (!exc_sat_flag[sat1[i]-1]) continue;
        el=el1[i];
        
        /* exclude IGSO satellites first */
        sys=satsys_bd2(sat1[i],NULL,_nav->pcvs[sat1[i]-1].type);
        if (sys&SYS_BD2) {
            el-=offset;
        }
        else if (sys&SYS_CMP) {
            if (strncmp(_nav->pcvs[sat1[i]-1].type,"BEIDOU-3I",9)==0) {
                satno2id(sat1[i], satid);
                trace(4,"gen_sat_sd_CAR: excluded BEIDOU3-IGSO sat=%s\n",satid);
                el-=offset;
            }
        }
        else if (sys&SYS_QZS) {
            el-=offset;
        }
        
        if (el<minel) {
            minidx=i;
            minel=el;
        }
        trace(4,"exc_sat_par: i=%2d sat1=%3d sat2=%3d el=%4.1f %4.1f minidx=%2d minel=%.3f\n",
            i,sat1[i],sat2[i],el1[i]*R2D,el2[i]*R2D,minidx,minel*R2D);
    }
    satno2id(sat1[minidx],s1);
    satno2id(sat2[minidx],s2);
    trace(4,"exc_sat_par: excluded %s %s sat1=%3d sat2=%3d el=%4.1f %4.1f\n",
        s1,s2,sat1[minidx],sat2[minidx],el1[minidx]*R2D,el2[minidx]*R2D);
    sat1[minidx]=0;
    sat2[minidx]=0;
    
    for (i=0;i<ns;i++) {
        trace(4,"exc_sat_par: i=%2d sat1=%3d sat2=%3d el1=%4.1f\n",
            i,sat1[i],sat2[i],el1[i]*R2D);
    }
    
    return ns-1;
}
/* ambiguity resolution by ILS (integer-least-square) ------------------------*/
static int ppp_amb_CAR(rtk_t *rtk, const obsd_t *obs, int n, int *exc,
                       const double *azel, double *x, double *P)
{
    double *D,*Q,a[MAXOBS*NFREQ],N[MAXOBS*NFREQ*2],*xp,*Pp;
    double el1[MAXOBS],el2[MAXOBS],std[3];
    int sat1[MAXOBS],sat2[MAXOBS],exc_sat_flag[MAXSAT];
    int sat1s[MAXOBS*NFREQ],sat2s[MAXOBS*NFREQ],frqs[MAXOBS*NFREQ];
    int i,ns,na=0,info=0;
    int nf=rtk->opt.nf,sol_fix_wl=1;
    char tstr[30];
    unsigned int tick=tickget();
    
    if (strstr(rtk->opt.pppopt,"-AR_NOFIX_WL")) {
        sol_fix_wl=0;
    }
    
    time2str(rtk->sol.time,tstr,0);
    trace(3,"ppp_amb_CAR: nf=%d\n",nf);
    
    /* generate satellite SD */
    ns=gen_sat_sd_CAR(rtk,obs,n,exc,azel,nf,sat1,sat2,el1,el2);
    if (ns<=0) {
        trace(4,"ppp_amb_CAR: no generated satellite SD ns= %d\n",ns);
        return 0;
    }
    trace(3,"ppp_amb_CAR: %s generate satellite SD ns= %d\n",tstr,ns);
    
    Pp=mat(rtk->nx,rtk->nx);
    xp=mat(rtk->nx,1);
    D=zeros(rtk->nx,n*nf);
    Q=mat(ns*nf,ns*nf);
    
    write_trace_sd_amb(rtk,obs,n,x,P,sat1,sat2,ns,nf,sat1s,sat2s,frqs,a,0,STEP_EWL);
    
    /* extra wide-lane */
    na=search_amb_ewl(rtk,obs,n,x,P,sat1,sat2,ns,nf,sat1s,sat2s,frqs,a,N,D);
    info=update_states(x,P,D,rtk->nx,N,a,na,sat1s,frqs,STEP_EWL);
    if (info) {
        trace(1,"ppp_amb_CAR: %s update error\n",tstr);
        free(D); free(Q); free(xp); free(Pp); D=Q=xp=Pp=NULL;
        return 0;
    }
    trace(3,"ppp_amb_CAR: %s extra wide-lane ambiguity fixed. nfix=%2d nsat=%2d\n",
        tstr,na,ns);
    
    write_trace_sd_amb(rtk,obs,n,x,P,sat1,sat2,ns,nf,sat1s,sat2s,frqs,a,0,STEP_WL);
    
    /* wide-lane */
    na=search_amb_wl(rtk,obs,n,x,P,sat1,sat2,ns,nf,sat1s,sat2s,frqs,a,N,D);
    if (na==0) {
        trace(4,"ppp_amb_CAR: %s wide-lane ambiguity search error\n",tstr);
        rtk->sol.age=0;
        free(D); free(Q); free(xp); free(Pp); D=Q=xp=Pp=NULL;
        return 0;
    }
    info=update_states(x,P,D,rtk->nx,N,a,na,sat1s,frqs,STEP_WL);
    if (info) {
        trace(1,"ppp_amb_CAR: %s update error\n",tstr);
        free(D); free(Q); free(xp); free(Pp); D=Q=xp=Pp=NULL;
        return 0;
    }
    trace(3,"ppp_amb_CAR: %s wide-lane ambiguity fixed. nfix=%2d nsat=%2d proc_time=%d [ms]\n",
        tstr,na,ns,(int)(tickget()-tick));
    
    matcpy(Pp,P,rtk->nx,rtk->nx);
    matcpy(xp,x,rtk->nx,1);
    write_trace_sd_amb(rtk,obs,n,x,P,sat1,sat2,ns,nf,sat1s,sat2s,frqs,a,0,STEP_NL);
    
    /* narrow-lane */
    for (i=0;i<3;i++) std[i]=sqrt(P[i+i*rtk->nx]);
    if (norm(std,3)<rtk->opt.thresar[6]) {
        trace(3,"ppp_amb_CAR: %s narrow-lane search. nsat=%d std=%.2f\n",
            tstr,ns,norm(std,3));
        
        for (i=0;i<rtk->opt.armaxiter;i++) {
            na=gen_sd_matrix_n1(rtk,obs,n,x,P,sat1,sat2,ns,sat1s,sat2s,frqs,a,Q,D);
            if (na<MIN_AMB_RES) {
                trace(4,"ppp_amb_CAR: %s no integer ambiguity candidate. na=%d/%d\n",
                    tstr,na,MIN_AMB_RES);
                na=0;
                break;
            }
            
            na=search_amb_lambda(rtk,sat1s,sat2s,frqs,Q,a,N,na,exc_sat_flag);
            if (na>0) {
                break;
            }
            
            /* exclude satellite for partial AR */
            exc_sat_par(sat1,sat2,el1,el2,ns,exc_sat_flag);
        }
    }
    else {
        na=0; i=0;
        trace(4,"ppp_amb_CAR: %s std is greater than the threshold. std=%.2f / %.2f\n",
            tstr,norm(std,3),rtk->opt.thresar[6]);
    }
    if (na==0) {
        rtk->sol.age=(float)i;
        free(D); free(Q); free(xp); free(Pp); D=Q=xp=Pp=NULL;
        trace(4,"ppp_amb_CAR: %s narrow-lane search error. iter=%d proc_time=%d [ms]\n",
            tstr,(int)rtk->sol.age,(int)(tickget()-tick));
        return sol_fix_wl; /* wide-lane fixed */
    }
    info=update_states(x,P,D,rtk->nx,N,a,na,sat1s,frqs,STEP_NL);
    rtk->sol.age=(float)i;
    
    /* test by standard deviation of position */
    for (i=0;i<3;i++) std[i]=sqrt(P[i+i*rtk->nx]);
    if (norm(std,3)>MAX_STD_FIX) {
        /* reset fix flag */
        for (i=0;i<na;i++) {
            rtk->ssat[sat1s[i]-1].fix[0]=rtk->ssat[sat2s[i]-1].fix[0]=0;
        }
        matcpy(P,Pp,rtk->nx,rtk->nx);
        matcpy(x,xp,rtk->nx,1);
        free(D); free(Q); free(xp); free(Pp); D=Q=xp=Pp=NULL;
        trace(4,"ppp_amb_CAR: %s failed to std test. std=%.3f / %.3f iter=%d proc_time=%d [ms]\n",
            tstr,norm(std,3),MAX_STD_FIX,(int)rtk->sol.age,(int)(tickget()-tick));
        return sol_fix_wl; /* wide-lane fixed */
    }
    
    write_trace_sd_amb(rtk,obs,n,x,P,sat1,sat2,ns,nf,sat1s,sat2s,frqs,a,1,9);
    trace(3,"ppp_amb_CAR: %s ambiguity fixed. nfix=%2d nsat=%2d std=%.3f iter=%d proc_time=%d [ms]\n",
        tstr,na,ns,norm(std,3),(int)rtk->sol.age,(int)(tickget()-tick));
    
    free(D); free(Q); free(xp); free(Pp); D=Q=xp=Pp=NULL;
    return info?sol_fix_wl:2; /* narrow-lane fixed */
}
/* ambiguity resolution in ppp -----------------------------------------------*/
extern int ppp_ar(rtk_t *rtk, const obsd_t *obs, int n, int *exc,
                  const nav_t *nav, const double *azel, double *x, double *P)
{
    if (n<=0||rtk->opt.modear<ARMODE_CONT) return 0;
    
#if 0
    /* ambiguity resolution by WL/NL for iono-free LC */
    if (rtk->opt.ionoopt==IONOOPT_IFLC) {
        return ppp_amb_IFLC(rtk,obs,n,exc,nav,azel,x,P);
    }
    /* ambiguity resolution by ILS */
    else {
        return ppp_amb_ILS(rtk,obs,n,exc,nav,azel,x,P);
    }
#endif
    if (rtk->opt.ionoopt!=IONOOPT_EST) return 0;
    _nav=nav;

    if (rtk->opt.car==0) {
        return ppp_amb_ILS(rtk,obs,n,exc,nav,azel,x,P);
    }
    else {
        return ppp_amb_CAR(rtk,obs,n,exc,azel,x,P);
    }
}   
