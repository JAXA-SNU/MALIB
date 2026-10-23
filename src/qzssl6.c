/*------------------------------------------------------------------------------
* qzssl6.c : QZSS L6 signal message decode common functions
*
* Copyright (C) 2025 TOSHIBA ELECTRONIC TECHNOLOGIES CORPORATION. All Rights Reserved.
*
* history : 2025/12/26 1.0  new, for MALIB from TETC original tools.
*-----------------------------------------------------------------------------*/
#include "rtklib.h"

/* constants -----------------------------------------------------------------*/
#define L6PREAMB          0x1ACFFC1Du   /* QZSS L6 message preamble */
#define L6BYTELEN                 218   /* QZSS L6 message byte length w/o R-S CODE */
#define L6DATA_BITLEN            1695   /* L6 message data part bit length */
#define L6DATA_BYTELEN            212   /* L6 message data part byte length */

/* decode QZSS L6 message -----------------------------------------------------
* decode QZSS L6 message
* args   : rtcm_t *rtcm IO   rtcm control struct
*          int    index I    input stream index
* return : QZSS L6 type (-1: error , 0: madoca-ppp cssr, 1: ionospheric)
* note   : before calling the function, init_mcssr()
*-----------------------------------------------------------------------------*/
extern int decode_qzss_l6msg(rtcm_t *rtcm, int index)
{
    unsigned int preamb;
    unsigned int type;
    unsigned int vid;
    int csid=-1;
    int anme=-1;
    int i=0;
    qzssl6_t *l6=rtcm->l6info;

    /* decode L6 message header (49 bits) */
    preamb   =getbitu(rtcm->buff,i,32); i+=32; /* Preamble */
    l6->prn  =getbitu(rtcm->buff,i, 8); i+= 8; /* PRN */
    type     =getbitu(rtcm->buff,i, 8);        /* L6 Msg. Type ID */
    vid      =getbitu(rtcm->buff,i, 3); i+= 3; /* Vendor ID */
    l6->mgfid=getbitu(rtcm->buff,i, 2); i+= 2; /* Msg. Gen. Facility ID */
    csid     =getbitu(rtcm->buff,i, 1); i+= 1; /* Correction Service ID */
    anme     =getbitu(rtcm->buff,i, 1); i+= 1; /* Applicable Nav. Msg. Ext. */
    l6->si   =getbitu(rtcm->buff,i, 1); i+= 1; /* Subframe Indicator(0:others,1:First Data) */
    l6->alert=getbitu(rtcm->buff,i, 1); i+= 1; /* Alert Flag(1:Alert) */
    
    trace(3,"check_qzss_l6msg : %s,preamb=0x%08X,prn=%d,type=0x%X(vid=%d, \
             mgfid=%d,csid=%d,anme=%d,si=%d),alert=%d\n",
        time_str(rtcm->time,3), preamb, l6->prn, type, vid, l6->mgfid, csid,
        anme, l6->si, l6->alert);

    if (preamb != L6PREAMB) {
        trace(2,"decode_qzss_l6emsg : %s,invalid preamb=0x%08X\n",
            time_str(rtcm->time,3),preamb);
        return -1;
    }

    if (vid==2 && csid==0) return decode_qzss_mdccssr(rtcm,index);
    if (vid==2 && csid==1) return decode_qzss_mdciono(rtcm);
    return -1;
}
/* stack QZSS L6 message -------------------------------------------------------
* stack QZSS L6 message and synchronize frame with L6 preamble
* args   : rtcm_t  *rtcm IO  rtcm control struct
*          uint8_t data  I   L6 1byte data
*          int     index I   input stream index
* return : status (-1: error message, 0: no message,0<: input messages)
* note   : before calling the function, init_l6()
*-----------------------------------------------------------------------------*/
extern int input_qzssl6(rtcm_t *rtcm, const uint8_t data, int index)
{
    trace(5,"input_qzssl6: data=%02x,%d\n",data,rtcm->nbyte);
    
    /* synchronize frame with L6 preamble */
    if      ((rtcm->nbyte == 0) && (data != 0x1A)) return 0;
    else if (((rtcm->nbyte == 1) && (data != 0xCF)) ||
            ((rtcm->nbyte == 2) && (data != 0xFC)) ||
            ((rtcm->nbyte == 3) && (data != 0x1D))) {
        rtcm->nbyte=0;
        if (data != 0x1A) {
            return 0;
        }
    }
    rtcm->buff[rtcm->nbyte++]=data;

    if (rtcm->nbyte<L6BYTELEN) return 0;

    rtcm->nbyte=0;
    return decode_qzss_l6msg(rtcm,index);
}
/* input QZSS L6 message from file ---------------------------------------------
* fetch next QZSS L6 message and input a messsage from file
* args   : rtcm_t *rtcm IO   rtcm control struct
*          FILE   *fp   I    file pointer
*          int    index I    input stream index
* return : status (-2: end of file, -1...10: same as input_qzssl6e())
* note   : same as input_qzssl6e()
*-----------------------------------------------------------------------------*/
extern int input_qzssl6f(rtcm_t *rtcm, FILE *fp, int index)
{
    int i;
    int data;
    int ret;
    
    trace(4,"input_qzssl6f\n");
    
    for (i=0;i<4096;i++) {
        if ((data=fgetc(fp))==EOF) return -2;
        if ((ret=input_qzssl6(rtcm,(uint8_t)data,index))) return ret;
    }
    return 0; /* return at every 4k bytes */
}
/* initialize L6 infomation ----------------------------------------------------
* initialize L6 infomation buffer in rtcm control struct
* args   : rtcm_t *rtcm IO   rtcm control struct
* return : status (1:ok,0:memory allocation error)
*-----------------------------------------------------------------------------*/
extern int init_l6info(rtcm_t *rtcm)
{
    trace(4,"init_l6info : rtcm=%p l6info=%p\n", (void*)rtcm, 
          rtcm ? (void*)rtcm->l6info : NULL);
    if (!rtcm) return 0;
    
    /* reset and allocate zero-initialized buffer */
    free_l6info(rtcm);
    rtcm->l6info=calloc(1,sizeof(*rtcm->l6info));
    if (!rtcm->l6info) {
        trace(2, "init_l6info: calloc failed\n");
        return 0;
    }
    return 1;
}
/* free L6 infomation ----------------------------------------------------------
* free L6 L6 infomation buffer in rtcm control struct
* args   : rtcm_t *rtcm IO   rtcm control struct
* return : none
*-----------------------------------------------------------------------------*/
extern void free_l6info(rtcm_t *rtcm)
{
    trace(4,"free_l6info : rtcm=%p l6info=%p\n", (void*)rtcm, 
          rtcm ? (void*)rtcm->l6info : NULL);
    
    if (!rtcm) return ;
    
    /* free memory for l6 ioformation buffer */
    if (rtcm->l6info!=NULL) {
        free(rtcm->l6info);
        rtcm->l6info=NULL;
    }
}
