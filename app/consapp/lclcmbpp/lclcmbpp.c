/*------------------------------------------------------------------------------
* lclcmbpp.c post-processing local correction data combination
*
* Copyright (C) 2025 Japan Aerospace Exploration Agency. All Rights Reserved.
*
* history : 2025/02/06  1.0  new, for MALIB from madoca ver.2.0.2 lcmcmb, 
*                            stat2stec, stat2trop
*           2025/03/24  1.1  remove unused macros
*                            fix bug lopt initialize
*-----------------------------------------------------------------------------*/
#include <stdarg.h>
#include <stdio.h>
#include "rtklib.h"

#define TRACEFILE   "lclcmbpp_%Y%m%d%h%M.trace" /* debug trace file */

static int  statn=0;                    /* number of stat data files */
static char stat_file[MAXSTA][1024];    /* stat data files */
static char cstat_file[MAXSTA][1024];   /* current stat data  files */
static FILE *fp_stat[MAXSTA]={NULL};    /* current stat data file pointer */
static rtcm_t rtcm={0};                 /* rtcm control struct */
static char rejs[MAX_REJ_SITES][8]={0}; /* reject site list */
static int rejc=0;                      /* number of rejected sites */
static sstat_t sstat[MAXSITES]={0};     /* stat data control struct */

/* help text -----------------------------------------------------------------*/
static const char *help[]={
"",
"NAME:",
"  lclcmbpp - post-processing local correction data combination",
"",
"SYNOPSIS:",
"  lclcmbpp [options] file",
"",
"DESCRIPTION:",
"   Generate local combination data as a post-processing step.",
"",
"OPTIONS: (* indicates required options)",
" -ts     y/m/d hh:mm:ss start time ",
"                        (y=year,m=month,d=date,hh=hour,mm=minite,",
"                         ss=seconds in GPST) *",
" -te     y/m/d hh:mm:ss end time [ts+86400s]",
" -grid   file           generate grid info file. *",
" -sta    file           generate station info file. *",
" -interp mode           interpolation mode.[0]",
"                        (0:planer fitting,1:weighted average of distance)",
" -extp                  enable extrapolation check on planer fitting mode.",
"                        (only planer fitting)[off]",
" -idist  dist           max ionospheric delay distance (km) from target to",
"                        station.[50]",
" -tdist  dist           max tropospheric delay distance (km) from target to",
"                        station.[100]",
" -ni     ni maxth       fail station detection parameter.[ni=1,maxth=1.0]",
"                        (ni=itaraion number(1-5), maxth=max threshold)",
"                        up to 5 iterations, each with its own threshold.",
" -eratio ratio          residual error ratio.[0.1]",
" -out    file           output file(.rtcm3)",
" -t      level          debug trace level (0:off) [0]",
" file                   input STAT file *",
""
};
/* print help ----------------------------------------------------------------*/
static void print_help(void)
{
    int i;
    for (i = 0; i < sizeof(help) / sizeof(*help); i++) {
        fprintf(stderr, "%s\n", help[i]);
    }
    exit(0);
}
/* update stat corrections ---------------------------------------------------*/
static void update_stat(gtime_t gt,stat_t *cstat)
{
    char path[1024],tstr[32],*p;
    char st[64]="";
    int i,j,siteno=0;
    static char buff[MAXSITES][4096];
    static int nbyte[MAXSITES]={0};
    
    trace(3,"update_stat :%s %d\n", time_str(gt,3), statn);

    for (i=0;i<statn;i++) {
        /* open or swap stat file */
        reppath(stat_file[i],path,gt,"", "");

        if (strcmp(path, cstat_file[i])!=0) {
            if (fp_stat[i]!=NULL) fclose(fp_stat[i]);
            fp_stat[i]=fopen(path, "rb");
            if (fp_stat[i]==NULL) {
                trace(2, "stat file open error: %s\n", path);
            } else {
                strcpy(cstat_file[i], path);
            }
        }
        if (fp_stat[i]==NULL) continue;
        
        /* site name as first 4 character of file */
        if ((p=strrchr(cstat_file[i],FILEPATHSEP))) strncpy(st,p+1,4);
        else strncpy(st,cstat_file[i],4);
        for (j=0; j<4&&st[j]; j++) st[j]=toupper(st[j]);
        
        /* check of reject site*/
        for (j=0; j<rejc; j++) {
            if (!strcmp(rejs[j],st)) break;
        }
        if (j<rejc) continue;

        siteno = getstano(cstat,st);
        strcpy(sstat[siteno].strp.site.name,st);
        strcpy(sstat[siteno].sion.site.name,st);

        while (timediff(sstat[siteno].time,gt)<1E-3) {
            strcpy(tstr,time_str(sstat[siteno].time, 3));
            trace(4, "update_stat: targettime=%s readtime=%s\n", time_str(gt, 3), tstr);

            memcpy(&cstat->time[siteno], &sstat[siteno].time,
                sizeof(cstat->time[siteno]));
            memcpy(&cstat->strp[siteno], &sstat[siteno].strp,
                sizeof(cstat->strp[siteno]));
            memcpy(&cstat->sion[siteno], &sstat[siteno].sion,
                sizeof(cstat->sion[siteno]));

            if (input_statf(&sstat[siteno], fp_stat[i], buff[i], &nbyte[i])<-1) {
                break;
            } 
        }
        strcpy(tstr,time_str(cstat->time[siteno], 3));
        trace(3, "update_stat: %s %s %s\n", st, time_str(gt, 3), tstr);
    }
}
/* post-processing local combination  ------------------------------------------
* post-processing local combination
* args   : char   **infile  I   input files (see below)
*          int    n         I   number of input files
*          lclopt_t *lopt   I   processing options
*          int btype        I   block type(0:grid,1:stations)
* return : status (1:ok,0:error)
*-----------------------------------------------------------------------------*/
static int postlclcmb(const int ni, char **infile, int n, const lclopt_t *lopt,
                      int btype) 
{
    int i;
    int data=0, nrej[MAXSITES]={0}, ncnt[MAXSITES]={0};
    char *ext;
    stat_t stat={0};
    gtime_t proctime={0};
    stream_t str;
    
    trace(3,"postlclcmb: ni=%d n=%d\n",ni, n);   
    
    for (i=0;i<MAXSTA;i++) {
        stat_file[i][0] ='\0';
        cstat_file[i][0]='\0';
        fp_stat[i]=NULL;
    }

    for (i=0;i<n;i++) {
        if ((ext=strrchr(infile[i],'.'))&&
            (!strcmp(ext,".stat")||!strcmp(ext,".STAT"))) {
            trace(3,"postlclcmb: stat file[%d] %s\n", statn, infile[i]);
            strcpy(stat_file[statn++],infile[i]);
        }
    }
    
    if (stropen(&str, STR_FILE, STR_MODE_W, lopt->outpath)==0) {
        return 0;
    }

    memcpy(&proctime,&lopt->ts,sizeof(time_t));

    while (1) {
        /* update stat corrections from files */
        update_stat(proctime,&stat);

        /* iono&trop correction interpolated value */
        if (btype==BTYPE_GRID) {
            grid_intp_trop(proctime, &stat, &rtcm.lclblk);
            grid_intp_iono(proctime, &stat, &rtcm.lclblk, lopt->maxres[ni],
                nrej, ncnt);
        }
        else if (btype==BTYPE_STA) {
            sta_sel_trop(proctime, &stat, &rtcm.lclblk);
            sta_sel_iono(proctime, &stat, &rtcm.lclblk);
        }

        /* output local combination data */
        if (ni == lopt->ni) {
            output_lclcmb(&rtcm, btype, &str);
        }

        /* set the next processing time */
        proctime=timeadd(proctime,TOW_LSB);
        if (timediff(proctime,lopt->te)>0) break;
    }
    if (ni<lopt->ni) {
        for (i=0;i<stat.nsi;i++) {
            if ((double)nrej[i]/ncnt[i]>lopt->eratio) {
                strcpy(rejs[rejc],stat.sion[i].site.name);
                trace(2,"postlclcmb[%d] : reject site %s ecnt=%d cnt=%d eratio=%.1f\n", 
                    ni, rejs[rejc],nrej[i],ncnt[i],lopt->eratio);
                rejc++;
            }
        }
    }
    
    for (i=0;i<statn;i++) {
        if (fp_stat[i]!=NULL) fclose(fp_stat[i]);
        fp_stat[i]=NULL;
    }
    statn=0;
    memset(sstat,0x00,sizeof(sstat));
    strclose(&str);
    return data;
}
/* lclcmbpp main -------------------------------------------------------------*/
int main(int argc, char **argv)
{
    double ep[6];
    int i, j, n;
    int tlevel = 0, btype = -1, ret = -1;
    char tracefile[MAXSTRPATH] = TRACEFILE, *infile[MAXSTA];

    lclopt_t lopt = {{0},{0},
                    0, {1.0,0.5,0.3,0.1,0.05}, 0.1,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    rtcm.lclblk.idist = MAX_DIST_ION;
    rtcm.lclblk.tdist = MAX_DIST_TRP;

    /* set options parameters*/
    for (i=1,n=0;i<argc;i++) {
        if (!strcmp(argv[i],"-ts")&&i+1<argc) {
            sscanf(argv[++i],"%lf/%lf/%lf",ep,ep+1,ep+2);
            sscanf(argv[++i],"%lf:%lf:%lf",ep+3,ep+4,ep+5);
            lopt.ts=epoch2time(ep);
        }
        else if (!strcmp(argv[i],"-te")&&i+1<argc) {
            sscanf(argv[++i],"%lf/%lf/%lf",ep,ep+1,ep+2);
            sscanf(argv[++i],"%lf:%lf:%lf",ep+3,ep+4,ep+5);
            lopt.te=epoch2time(ep);
        }
        else if (!strcmp(argv[i],"-idist")) {
            rtcm.lclblk.idist = atof(argv[++i]);
        }
        else if (!strcmp(argv[i],"-tdist")) {
            rtcm.lclblk.tdist = atof(argv[++i]);
        }
        else if (!strcmp(argv[i],"-extp")) {
            rtcm.lclblk.extp = 1;
        }
        else if (!strcmp(argv[i],"-interp")) {
            rtcm.lclblk.interp = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i],"-ni")) {
            lopt.ni=atoi(argv[++i]);
            for (j=0;j<lopt.ni&&j<5;j++) lopt.maxres[j]=atof(argv[++i]);
        }
        else if (!strcmp(argv[i],"-eratio")) lopt.eratio=atof(argv[++i]);
        else if (!strcmp(argv[i],"-grid")) {
            lopt.gridfile=argv[++i];
        }
        else if (!strcmp(argv[i],"-sta")) {
            lopt.stafile=argv[++i];
        }
        else if (!strcmp(argv[i],"-out")&&i+1<argc) {
            lopt.outpath=argv[++i];
        }
        else if (!strcmp(argv[i],"-t")&&i+1<argc) {
            tlevel=atoi(argv[++i]);
        }
        else if (*argv[i] == '-') {
            print_help();
            return -1;
        }
        else if (n<MAXSTA) infile[n++]=argv[i];
    }
    if (tlevel>0) {
        traceclose();
        traceopen(tracefile);
        tracelevel(tlevel);
    }

    trace(3,"Parameters : %s\n",argv[1]);
    for (i=2;i<argc;i++) trace(3," %s\n",argv[i]);
    trace(3,"\n");

    /* parameter check */
    if (n==0)  return -1;
    if (lopt.outpath==NULL) return -1;
    if (lopt.ts.time==0) return -1;
    if (lopt.te.time==0) {
        lopt.te = timeadd(lopt.ts,86400);
    }

    /* generate grid/station setting */
    if (lopt.gridfile!=NULL) {
        btype=BTYPE_GRID;
        if (initgridsta(lopt.gridfile,&rtcm.lclblk,btype)==0) {
            return -1;
        }
    }
    else if (lopt.stafile!=NULL) {
        btype=BTYPE_STA;
        if (initgridsta(lopt.stafile,&rtcm.lclblk,btype)==0) {
            return -1;
        }
    }
    else{
        trace(2,"select output type -grid [grid file] or -sta [station file]\n");
        return -1;
    }

    /* create local combination */
    /* iteration by residual threashold */
    for (i=0;i<lopt.ni+1;i++) {
        ret = postlclcmb(i,infile,n,&lopt,btype);
    }
    
    traceclose();
    return ret == 1 ? 0 : -1;
}
