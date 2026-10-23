/*------------------------------------------------------------------------------
* inscmn.c :  Inertial Navigation System(INS) common functions
*
* Copyright (C) 2025 Japan Aerospace Exploration Agency. All Rights Reserved.
*
* history : 2025/03/24  1.0  new by TETC
*           2025/12/26  1.1  change trace log level
*                            move split() to rtkcmn.c
*                            add update_ubxins() to integrating INS navigation 
*                            processing
*-----------------------------------------------------------------------------*/
#include "rtklib.h"

static char catt_file[1024]="";         /* current attitude data file */
static FILE *fp_att=NULL;               /* attitude data file pointer */
static char cimu_file[1024]="";         /* current imu data file */
static FILE *fp_imu=NULL;               /* imu data file pointer */
static char cubxins_file[1024]="";      /* current ubxins data file */
static FILE *fp_ubxins=NULL;            /* ubxins data file pointer */

/* rotate lever arm coordinates based on IMU attitude --------------------------
* rotate lever arm coordinates based on the IMU attitude.
* args   : double *rotimu I  IMU rotation angles
*          double *data         IO lever arm coordinates to be rotated
* return : none
*-----------------------------------------------------------------------------*/
extern void rotate_leverarm(const double *rotimu, double *data)
{
    int i;
    double R[9],R1[9],R2[9],temp[3];
    double z=PI/2.0;

    /* Septentrio IMU coordinate must be rotated. */
    if (rotimu[0]==90&&rotimu[2]==90) {
        z=rotimu[0]*PI/180;
        Rz(z,R1);
        Rx(z,R2);
        matmul("NN",3,3,3,1.0,R1,R2  ,0.0,R   );
        matmul("NN",3,1,3,1.0,R ,data,0.0,temp);
        for (i=0;i<3;i++) data[i]=temp[i];
    }
    else if (rotimu[0]==180||rotimu[0]==-180) {
        z=rotimu[0]*PI/180;
        Rx(z,R);
        matmul("NN",3,1,3,1.0,R ,data,0.0,temp);
        for (i=0;i<3;i++) data[i]=temp[i];
    }
}
/* convert body frame coordinates to ECEF coordinates --------------------------
* convert body frame coordinates to Earth-Centered Earth-Fixed (ECEF) coordinates.
* args   : double *rb  I  body frame rotation angles (roll, pitch, yaw)
*          double *pos I  position coordinates (latitude, longitude)
*          double *b2e O  transformation matrix from body frame to ECEF
* return : none
*-----------------------------------------------------------------------------*/
extern void body2ecef(const double *rb,const double *pos, double *b2e)
{
    double n2b[9],e2n[9],rbrx[9],rbry[9],rbrz[9],latry[9],lonrz[9],a[9],b[9];
    double *E = eye(3);

    Rx(rb[0]         ,rbrx );
    Ry(rb[1]         ,rbry );
    Rz(rb[2]         ,rbrz );
    Ry(-pos[0]-PI/2.0,latry);
    Rz(pos[1]        ,lonrz);
    matmul("NN",3,3,3,1.0,rbrx ,rbry ,0.0,a  );
    matmul("NN",3,3,3,1.0,a    ,rbrz ,0.0,n2b);
    matmul("NN",3,3,3,1.0,latry,lonrz,0.0,e2n);
    matmul("NN",3,3,3,1.0,n2b  ,e2n  ,0.0,b  );
    matmul("TN",3,3,3,1.0,b    ,E    ,0.0,b2e);
    free(E);
}
/* convert rotation matrix to Euler angles -------------------------------------
* convert a rotation matrix to Euler angles (roll, pitch, yaw).
* args   : double *R   I  rotation matrix (3x3)
*          double *att O  Euler angles (roll, pitch, yaw)
* return : none
*-----------------------------------------------------------------------------*/
void mat2euler(const double *R, double *att)
{
    att[1]=asin(-R[6]);
    if (fabs(R[6]) < 0.99999) {
        att[0]=atan2( R[7], R[8]);
        att[2]=atan2( R[3], R[0]);
    } else {
        att[0]=0.0;
        att[2]=atan2(-R[1], R[4]);
    }
}
/* convert ECEF coordinates to body frame coordinates --------------------------
* convert Earth-Centered Earth-Fixed (ECEF) coordinates to body frame coordinates.
* args   : double *pos    I  position coordinates (latitude, longitude)
*          double *b2e    I  transformation matrix from body frame to ECEF
*          double *att    O  Euler angles (roll, pitch, yaw)
* return : none
*-----------------------------------------------------------------------------*/
extern void ecef2body(const double *pos, const double *b2e, double *att)
{
    double e2n[9],latry[9],lonrz[9],n2b[9],b[9],Einv[9],e2ninv[9];
    double *E=eye(3);

    Ry(-pos[0]-PI/2.0,latry);
    Rz(pos[1]        ,lonrz);
    matmul("NN",3,3,3,1.0,latry,lonrz ,0.0,e2n);
    matcpy(Einv  ,E  ,3,3);
    matinv(Einv  ,3);
    matmul("NN",3,3,3,1.0,b2e  ,Einv  ,0.0,b  );
    matcpy(e2ninv,e2n,3,3);
    matinv(e2ninv,3);
    matmul("TN",3,3,3,1.0,b    ,e2ninv,0.0,n2b);
    free(E);
    mat2euler(n2b,att);
}
/* Skew symmetric matrix -------------------------------------------------------
* Skew symmetric matrix
* args   : double *a        I   vector a (3 x 1)
*        : double *b        O   matrix b (3 x 3)
* return : none
*-----------------------------------------------------------------------------*/
extern void skews(const double *a, double *b)
{
    b[0] =   0.0; b[3] = -a[2]; b[6] =  a[1];
    b[1] =  a[2]; b[4] =   0.0; b[7] = -a[0];
    b[2] = -a[1]; b[5] =  a[0]; b[8] =   0.0;
}
/* acceleration due to gravity gravity resolved about ECEF-frame ---------------
* comupute acceleration due to gravity gravity resolved about ECEF-frame
* args   : double *ecef     I   ecef position [x,y,z] (m)
*        : double *acc      O   acceleration {ax,ay,az} (m/s^2) (ecef)
* return : none
*-----------------------------------------------------------------------------*/
extern void gravity_ecef(const double *ecef, double *acc)
{
    double gamma[3];
    double mag=0.0,z_scale=0.0,mumag=0.0,j2r0mag=0.0;
    int i;

    /* calculate distance from center of the Earth */
    mag = norm(ecef,3);

    if (!mag) {
        acc[0] = acc[1] = acc[2] = 0.0;
    }
    else {
        z_scale  = 5 * pow(ecef[2]/mag,2);
        mumag    = -GM_LATEST / pow(mag,3);
        j2r0mag  = 1.5 * J2_EGM * pow(RE_WGS84/mag,2);
        gamma[0] = (1 - z_scale) * ecef[0];
        gamma[1] = (1 - z_scale) * ecef[1];
        gamma[2] = (3 - z_scale) * ecef[2];
        for (i=0;i<3;i++) gamma[i] = mumag * (ecef[i] + j2r0mag * gamma[i]);

        /* add centripetal acceleration */
        for (i=0;i<2;i++) acc[i] = gamma[i] + pow(OMGE,2) * ecef[i];
        acc[2]   = gamma[2];
    }
}
/* precision ECEF-frame inertial navigation ------------------------------------
* run precision ECEF-frame inertial navigation
* args   : double tor      I   time interval between epochs (s)
*        : double *ecef    IO  ecef position [x,y,z] (m)
*        : double *vel     IO  velocity {vx,vy,vz} (m/s) (ecef)
*        : double *b2e     IO  body-to-ECEF coord transformation matrix (3 x 3)
*        : double *fb      I   specific force of body frame (m/s^2)
*        : double *omgb    I   angular rate of body frame (rad/s)
* return : none
*-----------------------------------------------------------------------------*/
static void nav_equations_ecef(double tor, double *ecef, double *vel,
                               double *b2e, const double *fb, const double *omgb)
{
    int i;
    double prevb2e[9],aveb2e[9],intavgb2e[9],finavgb2e[9],attinc[3],attincs[9];
    double attincsq[9],atttrans[9],cearthm[9],tmpm[9];
    double earthvec[3],earthvecs[9],earthprod[9];
    double prevvel[3],earthav[3],earthavs[9],earthap[3],eceff[3],acc[3];
    double attmag=0.0,ca=0.0,cb=0.0,aveca=0.0,avecb=0.0;
    double earthrot=0.0,*E=eye(3);

    for (i=0;i<9;i++) prevb2e[i] = b2e[i];
    for (i=0;i<3;i++) prevvel[i] = vel[i];
    
    /* attitude update determine the Earth rotation over the update interval */
    earthrot = OMGE*tor;
    cearthm[0] =  cos(earthrot); cearthm[3] = sin(earthrot); cearthm[6] = 0.0;
    cearthm[1] = -sin(earthrot); cearthm[4] = cos(earthrot); cearthm[7] = 0.0;
    cearthm[2] =            0.0; cearthm[5] =           0.0; cearthm[8] = 1.0;

    /* calculate attitude increment, magnitude, and skew-symmetric matrix */
    for (i=0;i<3;i++) attinc[i] = omgb[i]*tor;
    attmag = norm(attinc,3);
    skews(attinc, attincs);

    /* obtain coordinate transformation matrix from the new attitude w.r.t. an
     * inertial frame to the old using Rodrigues' formula */
    matmul("NN",3,3,3,1.0,attincs,attincs,0.0,attincsq);
    if (attmag>1.E-8) {
        ca = sin(attmag)/attmag;
        cb = (1-cos(attmag))/pow(attmag,2);
        for (i=0;i<9;i++) atttrans[i] = E[i]+ca*attincs[i]+cb*attincsq[i];
    }
    else {
        for (i=0;i<9;i++) atttrans[i] = E[i]+attincs[i];
    }
    /* update attitude */
    matmul("NN",3,3,3,1.0,cearthm,prevb2e  ,0.0,tmpm);
    matmul("NN",3,3,3,1.0,tmpm   ,atttrans ,0.0,b2e );

    /* specific force frame transformation
     * calculate the average body-to-ECEF-frame coordinate transformation
     * matrix over the update interval */
    earthvec[0] = earthvec[1] = 0.0;earthvec[2] = earthrot;
    skews(earthvec, earthvecs);
    matmul("NN",3,3,3,1.0,earthvecs,prevb2e,0.0,earthprod);

    if (attmag>1.E-8) {
        aveca = (1-cos(attmag))/pow(attmag,2);
        avecb = (1-sin(attmag)/attmag)/pow(attmag,2);
        for (i=0;i<9;i++) {
            intavgb2e[i] = E[i]+aveca*attincs[i]+avecb*attincsq[i];
        }
        matmul("NN",3,3,3,1.0,prevb2e,intavgb2e,0.0,finavgb2e);
        for (i=0;i<9;i++) aveb2e[i] = finavgb2e[i]-0.5*earthprod[i];
    }
    else {
        for (i=0;i<9;i++) aveb2e[i] = prevb2e[i]-0.5*earthprod[i];
    }

    /* transform specific force to ECEF-frame resolving axes */
    matmul("NN",3,1,3,1.0,aveb2e,fb,0.0,eceff);

    earthav[0] = earthav[1] = 0.0;
    earthav[2] = OMGIE;
    skews(earthav,earthavs);
    gravity_ecef(ecef,acc);
    matmul("NN",3,1,3,1.0,earthavs,prevvel,0.0,earthap);
    
    /* update velocity and cartesian position */
    for (i=0;i<3;i++) {
        vel[i]  = prevvel[i]+tor*(eceff[i]+acc[i]-2*earthap[i]);
        ecef[i] = ecef[i]+tor*(vel[i]+prevvel[i])*0.5;
    }

    free(E);
}
/* update INS navigation -------------------------------------------------------
* update the Inertial Navigation System (INS) navigation state.
* args   : double *rotimu       I  IMU rotation angles
*          imuatt_s_t *imuatt   IO single IMU attitude structure
*          rtk_t *rtk           IO RTK structure
* return : none
*-----------------------------------------------------------------------------*/
extern void update_insnav(const double *rotimu, imuatt_s_t *imuatt, rtk_t *rtk)
{
    int i;
    double tor_ins=rtk->opt.insint,llh[3]={0.0},val[2]={0.0};
    imu_t *imup=&imuatt->imu;
    att_t *attp=&imuatt->att;

    rotate_leverarm(rotimu,imup->accel.accel);
    rotate_leverarm(rotimu,imup->gyro.gyro  );
    if (fabs(attp->att[0])<1E-3) {
        attp->att[0] = 
            atan(imup->accel.accel[1]/imup->accel.accel[2]);
    }
    if (fabs(attp->att[1])<1E-3) {
        for (i=0;i<2;i++) val[i] = imup->accel.accel[1+i];
        attp->att[1] = atan2(imup->accel.accel[0],norm(val,2));
    }

    ecef2pos(rtk->insd.ecef,llh);
    if (rtk->insd.insstat == INS_INIT) { 
        /* initialize previous position and velocity */
        for (i=0;i<3;i++) {
            rtk->insd.ecef[i]=rtk->sol.rrp[i];
            rtk->insd.vel[i]=rtk->sol.rrp[i+3];
        }
        for (i=0;i<6;i++) rtk->insd.imubias[i] = 0.0;

        /* previous body to ecef transfomation matrix */
        body2ecef(attp->att,llh,rtk->insd.curb2e);
    }

    /* correct IMU errors */
    for (i=0;i<3;i++) {
        rtk->insd.measacc[i]  = imup->accel.accel[i] - rtk->insd.imubias[i];
        rtk->insd.measgyro[i] = imup->gyro.gyro[i] - rtk->insd.imubias[i+3];
    }
    
    if (rtk->insd.gt.time!=0) tor_ins = timediff(imup->gt, rtk->insd.gt);

    /* update INS navigation */
    nav_equations_ecef(tor_ins,rtk->insd.ecef, rtk->insd.vel, 
                        rtk->insd.curb2e, rtk->insd.measacc,
                        rtk->insd.measgyro);
}
/* read sbfimu file ----------------------------------------------------------*/
static int read_sbfimu(char *buff, imu_t *imu)
{
    int i;
    int week=0,ret=0;
    double tow=0.0;
    char *array[24];

    for (i=0;i<24;i++) array[i]=NULL;
    ret=split(buff,",",array,24);
    if (ret<17) return 0;
    if (array[0]!=NULL) tow =atof(array[0]);
    if (array[1]!=NULL) week=atoi(array[1]);
    if (week==0) return 0;
    imu->gt = gpst2time(week,tow);

    if (strstr(array[5], "ACC") != NULL) {
        imu->accel.gt = imu->gt;
        for (i=0;i<3;i++) {
            if (array[i+11]!=NULL) imu->accel.accel[i] = atof(array[i+11]);
        }        
    }
    else if (strstr(array[5], "RATE") != NULL) {
        imu->gyro.gt = imu->gt;
        for (i=0;i<3;i++) {
            if (array[i+14]!=NULL) {
                imu->gyro.gyro[i] = (float)(atof(array[i+14])*D2R);
            }
        }  
    }
    else{
        return 0;
    }
    return 1;
}
/* update IMU data -------------------------------------------------------------
* update the IMU data from the specified file.
* args   : gtime_t gt           I  current time
*          char *imu_file       I  IMU file path
*          imuatt_s_t *imuatt   IO single IMU attitude structure
* return : status (1:ok,0:error)
*-----------------------------------------------------------------------------*/
extern int update_imu(gtime_t gt, const char *imu_file, imuatt_s_t *imuatt)
{
    char path[1024],tstr[32],buff[2048],sbuff[2048];
    int ret=0;
    static imu_t simu={0};

    /* open or swap stat file */
    reppath(imu_file, path,gt,"", "");

    if (strcmp(path, cimu_file)!=0) {
        if (fp_imu) fclose(fp_imu);
        fp_imu=fopen(path, "rb");
        if (fp_imu==NULL) {
            trace(2, "update_imu: imu file open error: %s\n", path); 
        } else {
            strcpy(cimu_file, path);
        }
    }
    if (!fp_imu) return ret;

    while (timediff(simu.gt,gt)<1E-3) {
        strcpy(tstr,time_str(simu.gt,3));
        trace(4, "update_imu: %s %s\n", time_str(gt, 3), tstr);
        
        if (simu.gt.time != 0) {
            memcpy(&imuatt->imu,&simu,sizeof(imu_t));
            trace(4, "update_imu: update %s accel=%.3f,%.3f,%.3f\n", 
                time_str(simu.accel.gt, 3), simu.accel.accel[0], 
                simu.accel.accel[1], simu.accel.accel[2]);
            trace(4, "update_imu: update %s gyro=%.3f,%.3f,%.3f\n", 
                time_str(simu.gyro.gt, 3), simu.gyro.gyro[0]*R2D, 
                simu.gyro.gyro[1]*R2D, simu.gyro.gyro[2]*R2D);
            ret = 1;
        }

        if ((fgets(buff,sizeof(buff),fp_imu))==NULL) {
            if (feof(fp_imu)) {
                ret = -1;
            }
            break;
        }
        memcpy(sbuff, buff, sizeof(sbuff));
        read_sbfimu(sbuff, &simu);
    }
    if (ret<0) {
        trace(3, "update_imu: file close %s %s\n", time_str(gt, 3), catt_file);
        fclose(fp_imu);
        fp_imu=NULL;
    }
    return ret;
}
/* read sbfatt file ----------------------------------------------------------*/
static int read_sbfatt(char *buff, att_t *att)
{
    int i,ret;
    int week=0;
    double tow=0.0;
    char *array[13];

    for (i=0;i<13;i++) array[i]=NULL;
    ret=split(buff,",",array,13);
    if (ret<13) return 0;
    if (array[0]!=NULL) tow =atof(array[0]);
    if (array[1]!=NULL) week=atoi(array[1]);
    if (week==0) return 0;
    att->gt = gpst2time(week,tow);

    if (array[2]!=NULL) att->nsat = (unsigned char)atoi(array[2]);
    att->err[0] = (strcmp(array[3], "No Error") == 0) ? 0 : 1;
    att->err[1] = (strcmp(array[4], "No Error") == 0) ? 0 : 1;
    att->err[2] = (unsigned char)atoi(array[5]);
    memset(att->att,0x00,sizeof(att->att));
    if (array[9]!=NULL) att->att[0] = (float)(atof(array[9]) * D2R);
    if (array[8]!=NULL) att->att[1] = (float)(atof(array[8]) * D2R);
    if (array[7]!=NULL) att->att[2] = (float)(atof(array[7]) * D2R);
    memset(att->attdot,0x00,sizeof(att->attdot));
    if (array[10]!=NULL) att->attdot[0] = atof(array[10]) * D2R;
    if (array[11]!=NULL) att->attdot[1] = atof(array[11]) * D2R;
    if (array[12]!=NULL) att->attdot[2] = atof(array[12]) * D2R;
    
    return 1;
}
/* update attitude data --------------------------------------------------------
* update the attitude data from the specified file.
* args   : gtime_t gt           I  current time
*          char *att_file       I  attitude file path
*          imuatt_s_t *imuatt   IO single IMU attitude structure
* return : status (1:ok,0:error)
*-----------------------------------------------------------------------------*/
extern int update_att(gtime_t gt, const char *att_file, imuatt_s_t *imuatt)
{
    char path[1024],tstr[32],buff[2048],sbuff[2048];
    int ret=0;
    static att_t satt={0};

    /* open or swap stat file */
    reppath(att_file,path,gt,"", "");

    if (strcmp(path, catt_file)!=0) {
        if (fp_att) fclose(fp_att);
        fp_att=fopen(path, "rb");
        if (fp_att==NULL) {
            trace(2, "update_att: attitude file open error: %s\n", path);
        } else {
            strcpy(catt_file, path);
        }
    }
    if (!fp_att) return ret;

    while (timediff(satt.gt,gt)<1E-3) {
        strcpy(tstr,time_str(satt.gt,3));
        trace(4, "update_att: update %s %s\n", time_str(gt, 3), tstr);
        
        if (satt.gt.time != 0) {
            memcpy(&imuatt->att,&satt,sizeof(att_t));
            trace(4, "update_att: update %s att=%.3f,%.3f,%.3f\n", 
                    time_str(satt.gt, 3), satt.att[0]*R2D, satt.att[1]*R2D,
                    satt.att[2]*R2D);
            ret = 1;
        }

        if ((fgets(buff,sizeof(buff),fp_att))==NULL) {
            if (feof(fp_att)) {
                ret = -1;
            }
            break;
        } 
        memcpy(sbuff, buff, sizeof(sbuff));
        read_sbfatt(sbuff, &satt);
    }
    if (ret<0) {
        trace(3, "update_att: file close %s %s\n", time_str(gt, 3), catt_file);
        fclose(fp_att);
        fp_att=NULL;
    }
    return ret;
}
/* delete IMU data -------------------------------------------------------------
* delete IMU data entries starting from the specified index.
* args   : imuatts_t *imuattm IO  IMU attitude structure
*          int dindex         I   index from which to start deletion
* return : none
*-----------------------------------------------------------------------------*/
extern void del_imu(imuatt_m_t *imuattm, int dindex)
{
    int i;

    if (dindex>MAXIMUATT) {
        trace(2,"del_imu : index error %d\n",dindex);
        return;
    }
    for (i=dindex;i<imuattm->imun;i++) {
        memcpy(&imuattm->imu[i-dindex],&imuattm->imu[i],sizeof(imu_t));
    }
    imuattm->imun-=dindex;
    if (imuattm->imun<0) imuattm->imun=0;
    imuattm->imuidx-=dindex;
    if (imuattm->imuidx<0) imuattm->imuidx=0;
}
/* add IMU data ----------------------------------------------------------------
* add a new IMU data entry to the IMU attitude structure.
* args   : imuatt_m_t *imuattm IO  multiple IMU attitude structure
*          imuatt_s_t *imuatt  I   single IMU attitude structure
* return : none
*-----------------------------------------------------------------------------*/
extern void add_imu(imuatt_m_t *imuattm, imuatt_s_t *imuatt)
{
    if (imuattm->imun+1 > MAXIMUATT) del_imu(imuattm,1);
    memcpy(&imuattm->imu[imuattm->imun],&imuatt->imu,sizeof(imu_t));
    imuattm->imun++;
}
/* get IMU data ----------------------------------------------------------------
* retrieve the IMU data entry closest to the specified time.
* args   : gtime_t gt          I  current time
*          imuatt_m_t *imuattm IO multiple IMU attitude structure
*          imuatt_s_t *imuatt  O  single IMU attitude structure
* return : status (1:ok,0:error)
*-----------------------------------------------------------------------------*/
extern int get_imu(gtime_t gt, imuatt_m_t *imuattm, imuatt_s_t *imuatt)
{
    int i;

    if (imuattm->imun==0) {
        trace(3,"get_att : no imu data \n");
        return 0;
    }
    for (i=imuattm->imuidx;i<imuattm->imun;i++) {
        if (timediff(imuattm->imu[i].gt,gt)>0) break;
        memcpy(&imuatt->imu,&imuattm->imu[i],sizeof(imu_t));
    }
    if (i==0) return 0;
    imuattm->imuidx=i-1;
    return 1;
}
/* delete attitude data --------------------------------------------------------
* delete attitude data entries starting from the specified index.
* args   : imuatt_m_t *imuattm IO  multiple IMU attitude structure
*          int dindex          I   index from which to start deletion
* return : none
*-----------------------------------------------------------------------------*/
extern void del_att(imuatt_m_t *imuattm, int dindex)
{
    int i;

    if (dindex>MAXIMUATT) {
        trace(2,"del_att : index error %d\n",dindex);
        return;
    }
    for (i=dindex;i<imuattm->attn;i++) {
        memcpy(&imuattm->att[i-dindex],&imuattm->att[i],sizeof(att_t));
    }
    imuattm->attn-=dindex;
    if (imuattm->attn<0) imuattm->attn=0;
    imuattm->attidx-=dindex;
    if (imuattm->attidx<0) imuattm->attidx=0;
}
/* add attitude data -----------------------------------------------------------
* add a new attitude data entry to the IMU attitude structure.
* args   : imuatt_m_t *imuattm IO multiple IMU attitude structure
*          imuatt_s_t *imuatt  I  single IMU attitude structure
* return : none
*-----------------------------------------------------------------------------*/
extern void add_att(imuatt_m_t *imuattm, imuatt_s_t *imuatt)
{
    if (imuattm->attn+1 > MAXIMUATT) del_att(imuattm,1);
    memcpy(&imuattm->att[imuattm->attn],&imuatt->att,sizeof(att_t));
    imuattm->attn++;
}
/* get attitude data -----------------------------------------------------------
* retrieve the attitude data entry closest to the specified time.
* args   : gtime_t gt          I  current time
*          imuatt_m_t *imuattm IO multiple IMU attitude structure
*          imuatt_s_t *imuatt  O  single IMU attitude structure
* return : int                 O  status (1: success, 0: failure)
*-----------------------------------------------------------------------------*/
extern int get_att(gtime_t gt, imuatt_m_t *imuattm, imuatt_s_t *imuatt)
{
    int i;
    
    if (imuattm->attn==0) {
        trace(3,"get_att : no attitude data \n");
        return 0;
    }
    for (i=imuattm->attidx;i<imuattm->attn;i++) {
        if (timediff(imuattm->att[i].gt,gt)>0) break;
        memcpy(&imuatt->att,&imuattm->att[i],sizeof(att_t));
    }
    if (i==0) return 0;
    imuattm->attidx=i-1;
    return 1;
}
/* update ubxins data ----------------------------------------------------------
* update the ubxins data from the specified file.
* args   : gtime_t gt          I  current time
*          char *ubxins_file   I  ubxins file path
*          imuatt_s_t *imuatt  IO single IMU attitude structure
* return : status (1:ok,0:error)
*-----------------------------------------------------------------------------*/
extern int update_ubxins(gtime_t gt, const char *ubxins_file, imuatt_s_t *imuatt)
{
    char path[1024];
    int ret=0,index=0;
    static int start=1,retubx=0;
    static raw_t raw_instance={0};
    raw_t *raw=&raw_instance;
    static rtcm_t rtcm_instance={0};
    rtcm_t *rtcm=&rtcm_instance;

    if (start==1) {
        init_raw(raw, STRFMT_UBX);
        init_rtcm(rtcm);
        start=0;
    }

    /* open or swap ubxins file */
    reppath(ubxins_file, path, gt, "", "");

    if (strcmp(path, cubxins_file)!=0) {
        if (fp_ubxins) fclose(fp_ubxins);
        fp_ubxins=fopen(path, "rb");
        if (fp_ubxins==NULL) {
            trace(2, "update_ubxins: ubxins file open error: %s\n", path);
            return ret;
        } else {
            strcpy(cubxins_file, path);
        }
    }

    while (timediff(raw->imuatt.imu.gyro.gt,gt)<1E-3 &&
           timediff(raw->imuatt.imu.accel.gt,gt)<1E-3 &&
           timediff(raw->imuatt.att.gt,gt)<1E-3) {
        /* set the data */
        if (retubx==16 && raw->imuatt.imu.gyro.gt.time!=0) {
            memcpy(&imuatt->imu.gt, &raw->imuatt.imu.gyro.gt, sizeof(gtime_t));
            memcpy(&imuatt->imu.gyro, &raw->imuatt.imu.gyro, sizeof(gyro_t));
            trace(4, "update_ubxins: update %s gyro=%.6f,%.6f,%.6f\n", 
                  time_str(raw->imuatt.imu.gyro.gt, 3), raw->imuatt.imu.gyro.gyro[0], 
                  raw->imuatt.imu.gyro.gyro[1], raw->imuatt.imu.gyro.gyro[2]);
            ret = 1;
        } else if (retubx==17 && raw->imuatt.imu.accel.gt.time!=0) {
            memcpy(&imuatt->imu.gt, &raw->imuatt.imu.accel.gt, sizeof(gtime_t));
            memcpy(&imuatt->imu.accel, &raw->imuatt.imu.accel, sizeof(accel_t));
            trace(4, "update_ubxins: update %s accel=%.6f,%.6f,%.6f\n", 
                  time_str(raw->imuatt.imu.accel.gt, 3), raw->imuatt.imu.accel.accel[0], 
                  raw->imuatt.imu.accel.accel[1], raw->imuatt.imu.accel.accel[2]);
            ret = 2;
        } else if (retubx==14 && raw->imuatt.att.gt.time!=0) {
            memcpy(&imuatt->att.gt, &raw->imuatt.att.gt, sizeof(gtime_t));
            memcpy(&imuatt->att.att, &raw->imuatt.att.att, sizeof(double[3]));
            memcpy(&imuatt->att.attdot, &raw->imuatt.att.attdot, sizeof(double[3]));
            trace(4, "update_ubxins: update %s att=%.6f,%.6f,%.6f\n", 
                  time_str(raw->imuatt.att.gt, 3), raw->imuatt.att.att[0], 
                  raw->imuatt.att.att[1], raw->imuatt.att.att[2]);
            trace(4, "update_ubxins: update %s attdot=%.6f,%.6f,%.6f\n", 
                  time_str(raw->imuatt.att.gt, 3), raw->imuatt.att.attdot[0], 
                  raw->imuatt.att.attdot[1], raw->imuatt.att.attdot[2]);
            ret = 3;
        }
        
        retubx=input_ubxf(raw, rtcm, index, fp_ubxins);
        if (retubx==-2) {
            break;
        }
    }

    if (retubx==-2) {
        trace(3, "update_ubxins: file close %s %s\n", time_str(gt, 3), cubxins_file);
        fclose(fp_ubxins);
        fp_ubxins=NULL;
    }
    return ret;
}
