/*------------------------------------------------------------------------------
* satqual.c : satellite signal quality functions
*
* Copyright (C) 2025-2026 Japan Aerospace Exploration Agency. All Rights Reserved.
*
* history : 2025/12/26  1.0  new by TETC
*           2026/02/27  1.1  modified the supported signal types
*-----------------------------------------------------------------------------*/
#include "rtklib.h"

/* constants/macros ----------------------------------------------------------*/
#define OBJ_STRUCTURE   0                          /* S: Structure */
#define OBJ_TERRAIN     1                          /* T: Terrain */
#define OBJ_VEGETATION  2                          /* V: Vegetation */
#define OBJ_USERDEF     3                          /* X: Other */
#define MAXMASK_OBJ     4

#define MASK_HORIZONTAL 0                          /* H: Horizontal */
#define MASK_ZENITH     1                          /* Z: Zenith */ 
#define MAXMASK_TYPE    2

#define MAXMASK_WP      500                        /* maximum number of waypoints for masking */
#define MAXMASK_DWP     (MAXMASK_OBJ*MAXMASK_TYPE) /* maximum number of mask type-object combinations per waypoint */

#define MAXEXCLSATS     100                        /* max exclusion satellites data */

#define SNREL_I 5.0                                /* snr threshold interval(deg) */
#define SQR(x)      ((x)*(x))

/* type definition -----------------------------------------------------------*/
typedef struct {                /* mask parameter type */
    double pos[3];              /* receiver position (ecef) (m) */
    int intv;                   /* azimuth interval (deg) */
    char obj;                   /* object type (0:structure,1:terrain,2:vegetation,3:other) */
    char type;                  /* mask type (0:horizontal,1:zenith) */
    double mask[360];           /* elevation mask values (deg) */
    int nmask;                  /* number of elevation mask values */
} maskp_t;

typedef struct {                /* mask data type */
    double pos[3];              /* receiver position (ecef) (m) */
    maskp_t maskp[MAXMASK_DWP]; /* mask parameters for the same waypoint (0:TH, 1:TZ, 2:VH, 3:VZ) */
} maskd_t;

typedef struct {                /* mask type */
    int n;                      /* number of unique receiver positions */
    maskd_t data[MAXMASK_WP];   /* mask data for each receiver position */
} mask_t;

typedef struct {                /* exclusion satellites data */
    gtime_t ts, te;             /* start and end time */
    int nsat;                   /* number of exclusion satellites */
    unsigned char flag[MAXSAT]; /* flags for excluded satellites (0:included,1:excluded) */
} exclsatsd_t;

typedef struct {                /* exclusion satellites type */
    int n;                      /* number of exclusion satellites data */
    exclsatsd_t data[MAXEXCLSATS]; /* exclusion satellites data */
} exclsats_t;

/* global variables ----------------------------------------------------------*/
static unsigned char maskobj[MAXMASK_OBJ]={0}; /* observation exclusion flag (0:excluded,1:included) */
static mask_t mask={0};                        /* elevation mask file data */
static exclsats_t exclsats={0};                /* exclusion satellites file data */  

/* parse numeric values from string ------------------------------------------*/
static int parse_str2num(const char *str, double arr[], int max)
{
    int i;
    int n=0;
    char *end;
    double val;

    for (i=0;i<max;i++) arr[i]=0;
    while (*str&&n<max) {
        val=strtod(str,&end);
        if (end!=str) arr[n++]=val,str=end;
        else str++;
    }
    return n;
}

/* mask find nearest ---------------------------------------------------------*/
static maskd_t *mask_find_nearest(mask_t *mask, const double *rr, 
                                  const double tolerance)
{
    int i,j;
    int idx=-1;
    double diff[3],dist;
    double min_dist=tolerance;

    for (i=0;i<mask->n;i++) {
        for (j=0;j<3;j++) diff[j]=mask->data[i].pos[j]-rr[j];
        dist=norm(diff,3);
        if      (dist<1E-9) return &mask->data[i];
        else if (dist<min_dist) {
            min_dist=dist;
            idx=i;
        }
    }
    return (idx<0)?NULL:&mask->data[idx];
}
/* convert mask object code to object number ---------------------------------*/
static int maskobj2no(char code)
{
    switch (code) {
        case 'S': return OBJ_STRUCTURE;
        case 'T': return OBJ_TERRAIN;
        case 'V': return OBJ_VEGETATION;
        case 'X': return OBJ_USERDEF;
        default : return -1;
    }
}
/* convert mask type code to type number -------------------------------------*/
static int masktype2no(char code)
{
    switch (code) {
        case 'H': return MASK_HORIZONTAL;
        case 'Z': return MASK_ZENITH;
        default : return -1;
    }
}
/* set mask object flags from code string ------------------------------------*/
static int set_maskobj(const char *codes)
{
    int objno,i;
    int n=0;

    for (i=0;i<MAXMASK_OBJ;i++) maskobj[i]=0;
    for (i=0;codes[i]!='\0';i++) {
        if ((objno=maskobj2no(codes[i]))<0) continue;
        maskobj[objno]=1;
        n++;
    }
    return n;
}
/* add mask ------------------------------------------------------------------*/
static int addmask(const double *r, char obj, char type, int intv, int nmask, 
                   const double *maskel)
{
    maskd_t *data;
    int i;
    int idx=-1;

    data=mask_find_nearest(&mask,r,1E-9);
    if (data==NULL) {
        if (mask.n>=MAXMASK_WP) return 0;
        data=&mask.data[mask.n++];
        for (i=0;i<3;i++) data->pos[i]=r[i];
    }

    idx=maskobj2no(obj)*MAXMASK_TYPE+masktype2no(type);
    if (idx<0) return 0;
    data->maskp[idx].obj=obj;
    data->maskp[idx].type=type;
    data->maskp[idx].intv=intv;
    data->maskp[idx].nmask=nmask;
    for (i=0;i<nmask&&i<360;i++) data->maskp[idx].mask[i]=maskel[i];
    return 1;
}
/* read mask -------------------------------------------------------------------
* load azimuth/elevation mask data from a text file into a mask_t structure
* args   : char   *file  I azimuth/elevation angle mask file
* return : status (1:ok,0:file open error)
*-----------------------------------------------------------------------------*/
extern int readmask(const char *file, const prcopt_t *prcopt)
{
    FILE *fp;
    double pos[3],r[3];
    double maskel[360]={0};
    char buff[1024],*arr[5+360],*p, obj, type;
    int i,n,intv,nmask;

    trace(3,"readmask: file=%s\n",file);
    
    if (!(fp=fopen(file,"r"))) {
        trace(2,"azimuth/elevation angle mask file open error : %s\n",file);
        return 0;
    }
    
    set_maskobj(prcopt->maskobj);
    
    while (fgets(buff,sizeof(buff),fp)) {
        if ((p=strpbrk(buff,"#\r\n"))) *p='\0';
        if (strlen(buff)==0) continue;
        if (mask.n>=MAXMASK_WP) break;

        n=split(buff,",",arr,5+360);
        if (n<6) continue;
        for (i=0;i<n;i++) arr[i]=strip(arr[i]);
        for (i=0;i<3;i++) pos[i]=atof(arr[i])*(i<2?D2R:1.0);
        obj=arr[3][0];
        type=arr[4][0];
        intv=atoi(arr[5]);
        if (intv<1) continue;
        nmask=ceil(360.0/intv);
        for (i=0;i<360;i++) maskel[i]=0;
        for (i=0;i<nmask&&i+6<n;i++) maskel[i]=atof(arr[i+6]);
        pos2ecef(pos,r);
        addmask(r,obj,type,intv,nmask,maskel);
    }
    fclose(fp);
    return 1;
}
/* azelmask --------------------------------------------------------------------
* determines if the satellite is masked based on position and angle data
* args   : double  *r      I receiver position (ecef) (m)
*          double  *azel   I azimuth/elevation angle {az,el} (rad)
*          gtime_t t       I time
*          int     sat     I satellite number
* return : status (1:masked,0:unmasked)
*-----------------------------------------------------------------------------*/
extern int azelmask(const double *r, const double *azel, gtime_t t, int sat)
{
    maskd_t *data;
    maskp_t *prm;
    char satid[8];
    int i,idx,objno;
    double azp[2],elp[2],maskel;
    double az=azel[0]*R2D,el=azel[1]*R2D,tolerance=10,pos[3]={0},rpos[3]={0};
    
    az-=floor(az/360.0)*360.0; /* 0 <= az < 360.0 */
    
    if (mask.n==0) return 0; /* no mask data */
    if ((data=mask_find_nearest(&mask,r,tolerance))==NULL) {
        return 0; /* no nearest waypoint found */
    }

    for (i=0;i<MAXMASK_DWP;i++) {
        prm=&data->maskp[i];
        if (prm->nmask==0||prm->intv<=0) continue; /* mask parameter error */
        
        idx=(int)floor(az/prm->intv);
        azp[0]=idx*prm->intv;
        azp[1]=azp[0]+prm->intv;
        if (azp[1]>=360.0) azp[1]=360.0;

        elp[0]=prm->mask[idx];
        elp[1]=prm->mask[(idx+1)%prm->nmask];
        maskel=elp[0]+(az-azp[0])*(elp[1]-elp[0])/(azp[1]-azp[0]);
        
        objno=maskobj2no(prm->obj);
        if (objno<0||objno>=MAXMASK_OBJ) continue; /* object error */
        if (maskobj[objno]==0) continue; /* exclude object tyep */
        
        if ( ((prm->type=='H') && (maskel> 0&&el<=maskel)) ||
             ((prm->type=='Z') && (maskel<90&&el>=maskel)) ) {
            satno2id(sat,satid);
            ecef2pos(data->pos,pos);
            ecef2pos(r,rpos);
            trace(3,"azelmask: exclude %s %s azel=%5.1f %4.1f maskel=%4.1f "
                    "type=%c obj=%c waypoint=%12.9f %13.9f %8.3f "
                    "pos=%12.9f %13.9f %8.3f\n",
                    time_str(t,3),satid,azel[0]*R2D,azel[1]*R2D,maskel,
                    prm->type,prm->obj,pos[0]*R2D,pos[1]*R2D,pos[2],
                    rpos[0]*R2D,rpos[1]*R2D,rpos[2]);
            return 1; /* masked */
        }
    }
    return 0; /* unmasked */
}

/* read exclusion satellites file ----------------------------------------------
* args   : char    *file  I exclusion satellites file
* return : status (1:ok,0:file open error)
*-----------------------------------------------------------------------------*/
extern int read_exclsats(const char *file)
{
    FILE *fp;
    gtime_t ts,te;
    gtime_t t0={0};
    char buff[1024],*arr[2+MAXSAT],*p;
    int i,n,sat;
    double ep[6];
    
    trace(3,"read_exclsats: file=%s\n",file);
    
    if (!(fp=fopen(file,"r"))) {
        trace(2,"exclsats file open error : %s\n",file);
        return 0;
    }
    
    while (fgets(buff,sizeof(buff),fp)) {
        if ((p=strpbrk(buff,"#\r\n"))) *p='\0';
        if (strlen(buff)==0) continue;
        if (exclsats.n>=MAXEXCLSATS) break;
        
        n=split(buff,",",arr,2+MAXSAT);
        for (i=0;i<n;i++) arr[i]=strip(arr[i]);

        ts=(parse_str2num(arr[0],ep,6)<3)?t0:epoch2time(ep);
        te=(parse_str2num(arr[1],ep,6)<3)?t0:epoch2time(ep);
        if (ts.time==0&&te.time==0) continue;

        exclsats.data[exclsats.n].ts=ts;
        exclsats.data[exclsats.n].te=te;
        exclsats.data[exclsats.n].nsat=0;
        for (i=2;i<n;i++) {
            if (arr[i][0]=='\0') continue;
            sat=satid2no(arr[i]);
            if (sat<=0||sat>=MAXSAT) continue;
            exclsats.data[exclsats.n].flag[sat-1]=1;
            exclsats.data[exclsats.n].nsat++;
        }
        exclsats.n++;
    }
    fclose(fp);
    return 1;
}
/* exclusion satellites at epoch -----------------------------------------------
* checks whether a given satellite is excluded at a specific time
* args   : gtime_t t    I time
*          int     sat  I satellite number
* return : exclision status (0:included,1:excluded)
*-----------------------------------------------------------------------------*/
extern int exclsats_ep(gtime_t t, int sat)
{
    int i;
    exclsatsd_t *data={0};
    char satid[8];
    
    if (exclsats.n==0||t.time==0||sat<=0||sat>=MAXSAT) {
        return 0; /* parameter error */
    }

    for (i=0;i<exclsats.n;i++) {
        data=&exclsats.data[i];
        if (!(data->ts.time==0||timediff(t,data->ts)>=0)) continue;
        if (!(data->te.time==0||timediff(t,data->te)<=0)) continue;
        if (data->flag[sat-1]!=0) {
            satno2id(sat,satid);
            trace(3,"exclusion satellite at epoch: %s sat=%s\n",
                  time_str(t,3), satid);
            return 1; /* included */
        }
    }
    return 0; /* excluded */
}

/* SNR weighting function ------------------------------------------------------
* SNR-based variance multiplier using elevation and SNR values
* args   : gtime_t  time   I   time
*          int      sat    I   satellite number
*          uint8_t  code   I   code indicator (CODE_???)
*          double   el     I   elevation angle (rad)
*          double   snr    I   signal-noise ratio values from RINEX OBS
*          prcopt_t opt    I   processing options
* return : double          O   variance multiplier
*-----------------------------------------------------------------------------*/
extern double snrvarweight(gtime_t time, int sat, uint8_t code, double el, 
                           double snr, const prcopt_t *opt)
{
    double thsnr,a,minsnr;
    int i,sys,midx;
    int sysno=0;
    char satid[4];
    char *obs;
    
    sys=satsys(sat,NULL);
    sysno=sys2pppsys(sys);
    satno2id(sat,satid);

    obs=code2obs(code);
    midx=obs2midx(sys,obs);

    if (midx<0 || MAXFREQ<=midx) return 1.0;
    
    if (opt->snrwmargin[sysno] == 0 && opt->snrweratio[sysno] == 0 ) return 1.0;
    
    a=(el*R2D)/SNREL_I;
    i=(int)floor(a); a-=i;

    if (i<1) {
        thsnr=opt->snrwthres[sysno][midx][0];
    } else if (i>=SNRWTHRESNUM) {
        thsnr=opt->snrwthres[sysno][midx][SNRWTHRESNUM-1];
    } else {
        thsnr=(1.0-a)*opt->snrwthres[sysno][midx][i-1]
              +a*opt->snrwthres[sysno][midx][i];
    }
    minsnr=thsnr-opt->snrwmargin[sysno]; /* calc minSNR */
    
    if (minsnr < 0) minsnr=0.0;
    if (snr<minsnr) {
        /* lower than minSNR */
        trace(3,"snrvarweight: %s %s(%d) SNR(%5.2f,el=%5.2f) lower than minSNR(%5.2f)\n",
            time_str(time,3),satid,midx,snr,el*R2D,minsnr);
        return 0.0;
    } else if (snr<thsnr) {
        /* add wait SNR lower than threshold SNR */
        trace(3,"snrvarweight: %s %s(%d) SNR(%5.2f,el=%5.2f) lower than threshold SNR(%5.2f) varwait:%9.6f\n",
            time_str(time,3),satid,midx,snr,el*R2D,thsnr,
            (thsnr-snr)*opt->snrweratio[sysno]+1.0);
        /* return value (var) and reliability are inversely proportional */
        return SQR((thsnr-snr)*opt->snrweratio[sysno]+1.0);
    } else {
        /* higher than threshold SNR */
        return 1.0;
    }
}
