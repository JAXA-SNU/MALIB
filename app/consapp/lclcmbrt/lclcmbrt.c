/*------------------------------------------------------------------------------
* lclcmbrt.c real-time local correction data combination
*
* Copyright (C) 2025 Japan Aerospace Exploration Agency. All Rights Reserved.
*
* history : 2025/02/06  1.0  new, for MALIB from madoca ver.2.0.2 lclrtc
*           2025/03/24  1.1  remove unused macros
*           2025/12/26  1.2  fix bug to process time in GPST instead of UTC
*-----------------------------------------------------------------------------*/
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include "rtklib.h"

/* constants -----------------------------------------------------------------*/
#define LOGIN_MAXTRY      3             /* max number of tries to login */
#define LOGIN_TIMEOUT     30            /* login timeout (s) */
#define SOCKET_TIMEOUT    1             /* socket timeout (ms) */
#define REP_CYCLE         250           /* cycle of update console (ms) */
#define CMD_PROMPT_A      "# "          /* command prompt: administrator */
#define CMD_PROMPT_U      "$ "          /* command prompt: user */
#define C_DEL       (char)0x7F          /* code del */
#define C_ESC       (char)0x1B          /* code escape */
#define C_CTRC      (char)0x03          /* code ctrl-C */
#define C_CTRD      (char)0x04          /* code ctrl-D */
#define C_ECHO      (char)1             /* code telnet echo */
#define C_SUPPGA    (char)3             /* code telnet suppress go ahead */
#define C_NOP       (char)241           /* code telnet nop */
#define C_BRK       (char)243           /* code telnet break */
#define C_IP        (char)244           /* code telnet interrupt */
#define C_EC        (char)247           /* code telnet erase character */
#define C_EL        (char)248           /* code telnet erase line */
#define C_WILL      (char)251           /* code telnet option negotiation */
#define C_WONT      (char)252           /* code telnet option negotiation */
#define C_DO        (char)253           /* code telnet option negotiation */
#define C_DONT      (char)254           /* code telnet option negotiation */
#define C_IAC       (char)255           /* code telnet interpret as command */
#define MAX_CMDLEN        1024          /* max length of command */
#define MAX_CMDHIST       256           /* max number of command history */
#define MAX_NCMD          64            /* max number of control and montor commands */
#define MAX_LOGIN         16            /* max number of login users */
#define MAX_LOGINLEN      32            /* max length of login users */
#define MAX_CMDARG        32            /* max number of command arguments */
#define TRACEFILE "lclcmbrt_%Y%m%d%h%M%S.trace" /* trace file */

#define MAX(x,y)          ((x)>(y)?(x):(y))

typedef struct {                        /* monitor console type */
    int state;                          /* state (0:stop,1:connect,2:login) */
    int id;                             /* console id */
    int sock;                           /* client socket descriptor */
    int n,p;                            /* length and cursor of line buffer */
    int ep,hp;                          /* escape buffer and history pointer */
    int blind;                          /* blind mode (0:off,1:on) */
    int level;                          /* command level (1:admin,0:user) */
    int brk;                            /* break state */
    int timeout;                        /* timeout time (ms) */
    int rep_cyc;                        /* repeat cycle (ms) */
    char user[MAX_LOGINLEN];            /* login user */
    char saddr[128];                    /* client address */
    unsigned int tstart,tactive;        /* start/data active tick */
    struct monitor_tag *monitor;        /* control and monitor */
    void *ap;                           /* argument pointer for console command */
    char buff[MAX_CMDLEN];              /* line buffer */
    char esc[16];                       /* escape buffer */
    char *hist[MAX_CMDHIST];            /* command history */
    FILE *fp_log;                       /* log file pointer */
    pthread_t thread;                   /* monitor console thread */
} console_t;

typedef int cmdfunc_t(char **, int, console_t *); /* command function type */

typedef struct {                        /* command table type */
    char name[32];                      /* command name */
    int level;                          /* command level (1:admin,0:user) */
    cmdfunc_t *func;                    /* command function */
} command_t;

typedef struct monitor_tag {            /* control and monitor type */
    int state;                          /* state (0:stop,1:running) */
    int sock;                           /* server socket descriptor */
    int port;                           /* port number */
    int ncmd;                           /* number of commands */
    char prompt[1024];                  /* login prompt */
    char login_a[1024];                 /* login users admin */
    char login_u[1024];                 /* login users user */
    char passwd_a[128];                 /* password admin */
    char passwd_u[128];                 /* password user */
    int timeout;                        /* inactive timeout (s) */
    void *ap;                           /* argument point for console command */
    console_t *console[MAX_LOGIN];      /* monitor consoles */
    command_t commands[MAX_NCMD];       /* console commands */
    pthread_t thread;                   /* control and monitor thread */
} monitor_t;

static lclcmbsvr_t svr={0};             /* local combination server */
static volatile int intr=0;             /* interrupt flag */
static int rep=0;                       /* replay flag */

/* help text -----------------------------------------------------------------*/
static const char *help[]={
"",
"NAME:",
"  lclcmbrt - real-time local correction data combination",
"",
"SYNOPSIS:",
"  lclcmbrt [options]",
"",
"DESCRIPTION：",
"   Generate local combination data as a realtime step.",
"",
"OPTIONS: (* indicates required options)",
" -instr  file  input stream option file *",
" -grid   file  generate grid option file. *",
" -sta    file  generate station option file. *",
" -idist  dist  max ionospheric delay distance (km) from target to",
"               station.[50]",
" -tdist  dist  max tropospheric delay distance (km) from target to",
"               station.[100]",
" -out    path  output stream of combined data *",
" -log    path  log stream of combined data",
" -p      port  port number for monitor console",
" -t      level debug trace level (0:off) [0]",
" -rep          enables replay mode based on input data timestamps.",
"",
"CONSOLE COMMANDS:",
" stat [-r]     show stream status. repeated with r. stopped by ctrl C.",
" path          show stream paths",
" enable sta    enable stream of station sta",
" disable sta   disable stream of station sta",
" shutdown      shutdown lclcmbrt",
" logout        logout monitor console",
" help|?        show help",
"",
"In the stream status, the fields indicate as follows:",
" NO            stream number",
" STA           station name (OUT:combined data)",
" F             stream formmat (S:STAT,R:RTCM3)",
" STAT          number of stat data messsages",
" KBYTE         input/output data amount (KB)",
" KBPS          input/output data rate (KB/s)",
" CS            connect  status(-:close,W:wait,C:connect,E:error)",
" ES            enable/disable status(E:Enable,D:Disable) ",
" STR_STATUS    status/error messages",
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
/* clear line buffer ---------------------------------------------------------*/
static int buff_clear(console_t *cp)
{
    int i;
    int len=strlen(cp->buff);
    
    for (i=0;i<cp->p;i++) if (write(cp->sock,"\b",1)<1) return 0;
    for (i=0;i<len  ;i++) if (write(cp->sock," " ,1)<1) return 0;
    for (i=0;i<len  ;i++) if (write(cp->sock,"\b",1)<1) return 0;
    cp->n=cp->p=0;
    return 1;
}
/* refresh line buffer after cursor ------------------------------------------*/
static int buff_refresh(console_t *cp)
{
    int i;
    
    for (i=cp->p;i<cp->n;i++) {
        if (write(cp->sock,cp->blind?"*":cp->buff+i,1)<1) return 0;
    }
    if (write(cp->sock," ",1)<1) return 0;
    for (;i>=cp->p;i--) {
        if (write(cp->sock,"\b",1)<1) return 0;
    }
    return 1;
}
/* move cursor right ---------------------------------------------------------*/
static int cursor_right(console_t *cp)
{
    if (cp->p>=cp->n) return 1;
    if (write(cp->sock,cp->blind?"*":cp->buff+cp->p,1)<1) return 0;
    cp->p++;
    return 1;
}
/* move cursor left ----------------------------------------------------------*/
static int cursor_left(console_t *cp)
{
    if (cp->p<=0) return 1;
    cp->p--;
    return write(cp->sock,"\b",1)==1;
}
/* delete charactor before cursor --------------------------------------------*/
static int cursor_delete(console_t *cp)
{
    int i;
    
    if (cp->p<=0) return 1;
    for (i=cp->p;i<cp->n;i++) cp->buff[i-1]=cp->buff[i];
    cp->n--;
    return cursor_left(cp)&&buff_refresh(cp);
}
/* insert character before cursor --------------------------------------------*/
static int cursor_insert(console_t *cp, char c)
{
    int i;
    
    if (cp->n>=MAX_CMDLEN) return 1;
    for (i=cp->n++;i>cp->p;i--) cp->buff[i]=cp->buff[i-1];
    cp->buff[cp->p++]=c;
    if (write(cp->sock,cp->blind?"*":&c,1)<1) return 0;
    return buff_refresh(cp);
}
/* add command history -------------------------------------------------------*/
static int add_history(console_t *cp, const char *buff)
{
    int i,len;
    
    if ((len=strlen(buff))<=0) return 1;
    free(cp->hist[MAX_CMDHIST-1]);
    for (i=MAX_CMDHIST-1;i>0;i--) {
        cp->hist[i]=cp->hist[i-1];
    }
    if (!(cp->hist[0]=(char *)malloc(len+1))) return 0;
    strcpy(cp->hist[0],buff);
    return 1;
}
/* call previous command history ---------------------------------------------*/
static int prev_history(console_t *cp)
{
    char *p;
    
    if (cp->hp>=MAX_CMDHIST||!cp->hist[cp->hp]) return 1;
    if (!buff_clear(cp)) return 0;
    for (p=cp->hist[cp->hp++];*p;p++) {
        if (!cursor_insert(cp,*p)) return 0;
    }
    return 1;
}
/* call next command history -------------------------------------------------*/
static int next_history(console_t *cp)
{
    char *p;
    
    if (!buff_clear(cp)) return 0;
    if (cp->hp==0||!cp->hist[cp->hp-1]) return 1;
    for (p=cp->hist[--cp->hp];*p;p++) {
        if (!cursor_insert(cp,*p)) return 0;
    }
    return 1;
}
/* handle Telnet command sequence (ref [1]) ----------------------------------*/
static int telnet_seq(console_t *cp)
{
    char msg[3]={C_IAC,0,0};
    
    /* WILL command */
    if (cp->esc[1]==C_WILL) {
        if (cp->ep<3) return 1;
        
        /* response support of only ECHO, SUPPGA */
        msg[1]=cp->esc[2]==C_ECHO||cp->esc[2]==C_SUPPGA?C_DO:C_DONT;
        msg[2]=cp->esc[2];
        if (write(cp->sock,msg,3)<3) return 0;
    }
    /* DO command */
    else if (cp->esc[1]==C_DO) {
        if (cp->ep<3) return 1;
        
        /* response accept of only ECHO, SUPPGA */
        msg[1]=cp->esc[2]==C_ECHO||cp->esc[2]==C_SUPPGA?C_WILL:C_WONT;
        msg[2]=cp->esc[2];
        if (write(cp->sock,msg,3)<3) return 0;
    }
    /* WON'T or DON'T */
    else if (cp->esc[1]==C_WONT||cp->esc[1]==C_DONT) {
        if (cp->ep<3) return 1;
        
        /* response of no support or no accept of ALL */
        msg[1]=cp->esc[1]==C_WONT?C_DONT:C_WONT;
        msg[2]=cp->esc[2];
        if (write(cp->sock,msg,3)<3) return 0;
    }
    /* BRK or IP (interrupt process) */
    else if (cp->esc[1]==C_BRK||cp->esc[1]==C_IP) {
        cp->brk=1;
    }
    /* EC (erase character) */
    else if (cp->esc[1]==C_EC) {
        if (!cursor_delete(cp)) return 0;
    }
    /* EL (erase line) */
    else if (cp->esc[1]==C_EL) {
        if (!buff_clear(cp)) return 0;
    }
    /* NOP (no operation) */
    else if (cp->esc[1]==C_NOP) ;
    
    /* other commands are ignored and logged */
    else {
        trace(4,"telnet IAC %u\n",cp->esc[1]);
    }
    cp->ep=0;
    return 1;
}
/* handle ESC sequence -------------------------------------------------------*/
static int escape_seq(console_t *cp)
{
    if (cp->ep<3) return 1;
    
    cp->ep=0;
    
    if (cp->esc[1]!='[') return 1;
    
    /* only support cursor keys */
    switch (cp->esc[2]) {
        case 'A': if (!prev_history(cp)) return 0; break; /* U */
        case 'B': if (!next_history(cp)) return 0; break; /* D */
        case 'C': if (!cursor_right(cp)) return 0; break; /* R */
        case 'D': if (!cursor_left (cp)) return 0; break; /* L */
    }
    return 1;
}
/* output string to monitor console ------------------------------------------*/
static int console_puts(console_t *cp, const char *buff)
{
    const char *p=buff,*q;
    size_t len;
    
    cp->tactive=tickget();
    
    for (p=buff;(q=strchr(p,'\n'));p=q+1) {
        if (cp->fp_log) {
            fwrite(p,1,q-p,cp->fp_log);
            fwrite("\r\n",1,2,cp->fp_log);
        }
        if (write(cp->sock,p,q-p)<q-p) return 0;
        if (write(cp->sock,"\r\n",2)<2) return 0;
    }
    len=strlen(p);
    if (cp->fp_log) {
        fwrite(p,1,len,cp->fp_log);
    }
    return write(cp->sock,p,len)==len;
}

/* input character from monitor console --------------------------------------*/
static int console_getc(console_t *cp, char *c)
{
    struct timeval tv={0,SOCKET_TIMEOUT*1000};
    fd_set rs;
    int ret;
    
    *c='\0';
    
    /* wait input with timeout */
    FD_ZERO(&rs); FD_SET(cp->sock,&rs);
    if ((ret=select(cp->sock+1,&rs,NULL,NULL,&tv))<0) return 0; /* error */
    if (ret==0) return 1; /* timeout */
    
    /* input character */
    if (read(cp->sock,c,1)<1) return 0;
    
    /* Telnet IAC (interpret as command) or ESC */
    if (*c==C_IAC||*c==C_ESC) {
        cp->ep=0;
        cp->esc[cp->ep++]=*c;
        *c='\0';
    }
    /* handle Telnet command sequence */
    else if (cp->ep>0&&cp->esc[0]==C_IAC) {
        cp->esc[cp->ep++]=*c;
        if (!telnet_seq(cp)) return 0;
        *c='\0';
    }
    /* handle ESC sequence */
    else if (cp->ep>0&&cp->esc[0]==C_ESC) {
        cp->esc[cp->ep++]=*c;
        if (!escape_seq(cp)) return 0;
        *c='\0';
    }
    /* CR */
    else if (*c=='\r') {
        if (!console_puts(cp,"\n")) return 0;
    }
    /* CTRL-C */
    else if (*c==C_CTRC) {
        cp->brk=1;
        if (!console_puts(cp,"^C\n")) return 0;
    }
    cp->tactive=tickget();
    return 1;
}

/* print with format to monitor console --------------------------------------*/
static int console_printf(console_t *cp, const char *format, ...)
{
    va_list ap;
    char buff[MAX_CMDLEN+1];
    
    va_start(ap,format);
    vsprintf(buff,format,ap);
    va_end(ap);
    return console_puts(cp,buff);
}
/* test inactive timeout -----------------------------------------------------*/
static int timeout_inact(console_t *cp)
{
    if (cp->timeout<=0||(int)(tickget()-cp->tactive)/1000<cp->timeout) return 0;
    
    console_printf(cp,"\nInactive timeout.\n");
    
    trace(2,"console timeout: user=%s addr=%s\n",cp->user,cp->saddr);
    
    sleepms(1000);
    return 1;
}
/* check break from monitor console ------------------------------------------*/
static int console_chkbrk(console_t *cp)
{
    char c='\0';
    
    while (cp->state) {
        if (!console_getc(cp,&c)||cp->brk) return 1;
        if (c=='\0') return 0;
    }
    return 1;
}
/* output character to monitor console ---------------------------------------*/
static int console_putc(console_t *cp, char c)
{
    cp->tactive=tickget();
    
    if (c=='\n') {
        if (cp->fp_log) fwrite("\r\n",1,2,cp->fp_log);
        return write(cp->sock,"\r\n",2)==2;
    }
    else {
        if (cp->fp_log) fwrite(&c,1,1,cp->fp_log);
        return write(cp->sock,&c,1)==1;
    }
}
/* input line from monitor console -------------------------------------------*/
static int console_gets(console_t *cp, char *buff, int nmax)
{
    char c;
    
    cp->hp=cp->brk=0;
    
    while (cp->state) {
        
        /* test inactive timeout */
        if (timeout_inact(cp)) {
            cp->state=0;
            continue;
        }
        /* input character from monitor console */
        if (!console_getc(cp,&c)) break;
        
        /* logout if ctrl-d at line head */
        if (c==C_CTRD&&cp->n==0) {
            cp->state=0;
            continue;
        }
        /* return if break */
        if (cp->brk) {
            cp->buff[0]=buff[0]='\0';
            cp->n=cp->p=0;
            return 1;
        }
        /* CR as line end */
        if (c=='\r') {
            cp->buff[cp->n]='\0';
            memcpy(buff,cp->buff,nmax-1);
            buff[nmax-1]='\0';
            cp->n=cp->p=0;
            return 1;
        }
        /* BS, DEL */
        else if (c=='\b'||c==C_DEL) {
            if (!cursor_delete(cp)) break;
        }
        /* BEL, FF, HTAB, VTAB */
        if (c=='\a'||c=='\f'||c=='\t'||c=='\v') {
            if (!console_putc(cp,c)) break;
        }
        /* save printable character to buffer */
        else if (isprint(c)) {
            if (!cursor_insert(cp,c)) break;
        }
    }
    return 0;
}
/* close log file for console ------------------------------------------------*/
static int console_closelog(console_t *cp)
{
    if (!cp->fp_log) return 0;
    fclose(cp->fp_log);
    cp->fp_log=NULL;
    return 1;
}
/* test login user -----------------------------------------------------------*/
static int test_login(const char *user, const char *login)
{
    const char *p,*q;
    
    for (p=login;(q=strchr(p,','));p=q+1) {
        if (!strncmp(user,p,q-p)&&strlen(user)==q-p) return 1;
    }
    return !strcmp(user,p);
}
/* monitor console login -----------------------------------------------------*/
static int console_login(console_t *cp)
{
    const char *passwd;
    char buff[MAX_CMDLEN];
    int n=0;
    
    cp->timeout=LOGIN_TIMEOUT;
    
    while (cp->state) {
        
        /* test number of tries to login */
        if (++n>LOGIN_MAXTRY) {
            console_printf(cp,"Login failed.\n");
            sleepms(1000);
            return 0;
        }
        console_printf(cp,"%s\n",cp->monitor->prompt);
        
        /* input and test login user */
        if (write(cp->sock,"Login as: ",10)<10) return 0;
        if (!console_gets(cp,buff,sizeof(buff))||cp->brk) return 0;
        strncpy(cp->user,cp->buff,MAX_LOGINLEN-1);
        cp->user[MAX_LOGINLEN-1]='\0';
        
        if (test_login(cp->user,cp->monitor->login_a)) {
            cp->level=1;
            passwd=cp->monitor->passwd_a;
        }
        else if (test_login(cp->user,cp->monitor->login_u)) {
            passwd=cp->monitor->passwd_u;
        }
        else {
            console_printf(cp,"Invalid user: %s.\n",cp->user);
            continue;
        }
        if (!*passwd) break;
        
        /* input and test password */
        if (write(cp->sock,"Password: ",10)<10) return 0;
        cp->blind=1;
        if (!console_gets(cp,buff,sizeof(buff))||cp->brk) return 0;
        cp->blind=0;
        if (!strcmp(cp->buff,passwd)) break;
        
        console_printf(cp,"Invalid password.\n\n");
    }
    console_puts(cp,"\n");
    
    return 1;
}
/* execute command -----------------------------------------------------------*/
static int exec_command(char **args, int n, console_t *cp)
{
    int i;
    
    /* search command */
    for (i=0;i<cp->monitor->ncmd;i++) {
        if (cp->monitor->commands[i].level>cp->level) continue;
        if (strcmp(args[0],cp->monitor->commands[i].name)) continue;
        break;
    }
    if (i>=cp->monitor->ncmd) {
        console_printf(cp,"Unknown command: %-32.32s\n",args[0]);
        return 0;
    }
    /* call command function */
    return cp->monitor->commands[i].func(args,n,cp);
}
/* external shell command ----------------------------------------------------*/
static void exec_system(const char *cmd, console_t *cp)
{
#ifdef ENAEXTCMD
    FILE *fp;
    char tmpfile[1024]="",buff[1024+64];
    
    if (strlen(cmd)>=1024) {
        console_printf(cp,"too long command\n");
        return;
    }
    if (strchr(cmd,'>')) {
        sprintf(buff,"%s\n",cmd);
    }
    else {
        sprintf(tmpfile,TMP_FILE,(int)getpid(),cp->id);
        sprintf(buff,"%s > %s\n",cmd,tmpfile);
    }
    if (system(buff)) {
        console_printf(cp,"command exec error\n");
        return;
    }
    if (!*tmpfile||!(fp=fopen(tmpfile,"r"))) return;
    
    while (fgets(buff,sizeof(buff),fp)) {
        console_printf(cp,"%s",buff);
    }
    fclose(fp);
    remove(tmpfile);
#else
    console_printf(cp,"external commands disabled\n");
#endif
}
/* monitor console thread ----------------------------------------------------*/
static void *console_thread(void *arg)
{
    const char charmode[]={C_IAC,C_WILL,C_SUPPGA,C_IAC,C_WILL,C_ECHO};
    console_t *cp=(console_t *)arg;
    char *
    
    args[MAX_CMDARG],buff[MAX_CMDLEN],*p;
    int i,n;
    
    /* send Telnet character mode (ref [1]) */
    if (write(cp->sock,charmode,6)<6) return 0;
    
    sleepms(100);
    
    /* monitor console login */
    if (!console_login(cp)) {
        close(cp->sock);
        cp->state=0;
        return 0;
    }
    trace(4,"console login  : user=%s addr=%s\n",cp->user,cp->saddr);
    
    cp->state=2; /* login */
    cp->tstart=cp->tactive=tickget();
    cp->timeout=cp->monitor->timeout;
    
    /* command shell loop */
    while (cp->state) {
        
        /* output prompt */
        if (!console_puts(cp,cp->level?CMD_PROMPT_A:CMD_PROMPT_U)) break;
        
        /* input command */
        if (!console_gets(cp,buff,sizeof(buff))) break;
        
        /* add command history */
        if (!add_history(cp,buff)) break;
        
        /* external shell command */
        if (buff[0]=='!') {
            exec_system(buff+1,cp);
            continue;
        }
        /* parse command */
        n=0;
        for (p=strtok(buff," \t");p&&n<MAX_CMDARG;p=strtok(NULL," \t")) {
            args[n++]=p;
        }
        if (n==0) continue;
        
        /* execute command */
        if (exec_command(args,n,cp)<0) {
            cp->state=0; /* exit if ret < 0 */
        }
    }
    close(cp->sock);
    console_closelog(cp);
    
    for (i=0;i<MAX_CMDHIST;i++) {
        free(cp->hist[i]);
    }
    trace(4,"console logout : user=%s addr=%s\n",cp->user,cp->saddr);
    
    return 0;
}
/* new monitor console -------------------------------------------------------*/
static console_t *console_new(int id, int sock, const char *saddr,
                              monitor_t *monitor, void *ap)
{
    console_t *cp;
    int i;
    
    if (!(cp=(console_t *)malloc(sizeof(console_t)))) return NULL;
    
    cp->state=1; /* connect */
    cp->id=id;
    cp->sock=sock;
    cp->n=cp->p=0;
    cp->ep=cp->hp=0;
    cp->blind=0;
    cp->level=0;
    cp->brk=0;
    cp->timeout=0;
    cp->rep_cyc=REP_CYCLE;
    cp->user[0]='\0';
    strcpy(cp->saddr,saddr);
    cp->tstart=cp->tactive=tickget();
    cp->monitor=monitor;
    cp->ap=ap;
    for (i=0;i<MAX_CMDHIST;i++) cp->hist[i]=NULL;
    cp->fp_log=NULL;
    
    /* generate monitor console thread */
    if (pthread_create(&cp->thread,NULL,console_thread,cp)) {
        free(cp);
        return NULL;
    }
    return cp;
}
/* free monitor console ------------------------------------------------------*/
static void console_free(console_t *cp)
{
    if (!cp) return;
    
    cp->state=0;
    pthread_join(cp->thread,NULL);
    
    free(cp);
}
/* set socket options --------------------------------------------------------*/
static int set_sock(int sock)
{
    int mode=1;
    const char *opt=(const char *)&mode;
    
    /* socket option: tcp_nodelay */
    if (setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,opt,sizeof(mode))==-1) {
        trace(4,"setsockopt error: nodelay\n");
    }
    return 1;
}
/* generate server socket ----------------------------------------------------*/
static int gen_svrsock(int port)
{
    struct sockaddr_in addr;
    int sock;
    int mode=1;
    const char *opt=(const char *)&mode;
    
    /* generate socket */
    if ((sock=socket(AF_INET,SOCK_STREAM,0))==-1) {
        trace(4,"socket error (%d)\n",errno);
        return 0;
    }
    /* set socket options to server socket */
    if (!set_sock(sock)) {
        close(sock);
        return 0;
    }
    /* server socket option: so_reuseaddr */
    if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,opt,sizeof(mode))==-1) {
        trace(4,"sockopt error: resuseaddr\n");
    }
    /* bind port and listen */
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_port=htons(port);
    
    if (bind(sock,(struct sockaddr *)&addr,sizeof(addr))==-1) {
        trace(4,"bind error (%d) port=%d\n",errno,port);
        close(sock);
        return 0;
    }
    listen(sock,5);
    return sock;
}
/* accept client connection --------------------------------------------------*/
static int accept_sock(int sock, char *saddr)
{
    struct timeval tv={0,SOCKET_TIMEOUT*1000};
    struct sockaddr_in addr;
    socklen_t len=sizeof(addr);
    int sock_cli;
    fd_set rs;
    
    /* test accept with timeout */
    FD_ZERO(&rs); FD_SET(sock,&rs);
    if (!select(sock+1,&rs,NULL,NULL,&tv)) return 0;
    
    /* accept client connection */
    if (!(sock_cli=accept(sock,(struct sockaddr *)&addr,&len))) {
        trace(4,"accept error (%d)\n",errno);
        return 0;
    }
    /* set socket options to client socket */
    if (!set_sock(sock_cli)) {
        close(sock_cli);
        return 0;
    }
    strcpy(saddr,inet_ntoa(addr.sin_addr));
    return sock_cli;
}
/* control and monitor thread ------------------------------------------------*/
static void *monitor_thread(void *arg)
{
    monitor_t *monitor=(monitor_t *)arg;
    char saddr[128];
    int i,sock;
    
    trace(4,"control and monitor start\n");
    
    while (monitor->state) {
        
        /* free closed monitor console */
        for (i=0;i<MAX_LOGIN;i++) {
            if (!monitor->console[i]||monitor->console[i]->state) continue;
            console_free(monitor->console[i]);
            monitor->console[i]=NULL;
        }
        /* accept client connection */
        if (!(sock=accept_sock(monitor->sock,saddr))) continue;
        
        for (i=0;i<MAX_LOGIN;i++) {
            if (!monitor->console[i]) break;
        }
        if (i>=MAX_LOGIN) {
            trace(4,"number of login exceeds max login\n");
            close(sock);
        }
        /* generate new monitor console */
        else if (!(monitor->console[i]=console_new(i,sock,saddr,monitor,
                                                   monitor->ap))) {
            close(sock);
        }
    }
    close(monitor->sock);
    
    for (i=0;i<MAX_LOGIN;i++) {
        console_free(monitor->console[i]);
    }
    trace(4,"control and monitor stop\n");
    
    return 0;
}
/* new control and monitor ---------------------------------------------------*/
static monitor_t *monitor_new(int port, int timeout, const char *prompt,
                              void *ap)
{
    monitor_t *monitor;
    int i;
    
    trace(4,"monitor_new\n");
    
    if (!(monitor=(monitor_t *)malloc(sizeof(monitor_t)))) return NULL;
    
    monitor->state=1;
    monitor->port=port;
    strcpy(monitor->prompt,prompt);
    monitor->login_a[0]=monitor->login_u[0]='\0';
    monitor->passwd_a[0]=monitor->passwd_u[0]='\0';
    monitor->timeout=timeout;
    monitor->ap=ap;
    monitor->ncmd=0;
    
    /* generate server socket */
    if (!(monitor->sock=gen_svrsock(monitor->port))) {
        free(monitor);
        return NULL;
    }
    for (i=0;i<MAX_LOGIN;i++) {
        monitor->console[i]=NULL;
    }
    /* generate control and monitor thread */
    if (pthread_create(&monitor->thread,NULL,monitor_thread,monitor)) {
        free(monitor);
        return NULL;
    }
    return monitor;
}
/* add user and password -----------------------------------------------------*/
static void monitor_addusr(monitor_t *monitor, const char *login,
                           const char *passwd, int type)
{
    trace(4,"monitor_addusr: login=%s passwd=%s type=%d\n",login,passwd,
              type);
    
    if (type) {
        strcpy(monitor->login_a,login);
        strcpy(monitor->passwd_a,passwd);
    }
    else {
        strcpy(monitor->login_u,login);
        strcpy(monitor->passwd_u,passwd);
    }
}
/* add command callback ------------------------------------------------------*/
static void monitor_addcmd(monitor_t *monitor, const char *name,
                           cmdfunc_t *func, int level)
{
    trace(4,"monitor_addcmd: name=%s level=%d\n",name,level);
    
    if (monitor->ncmd>=MAX_NCMD) return;
    
    strcpy(monitor->commands[monitor->ncmd].name,name);
    monitor->commands[monitor->ncmd].func=func;
    monitor->commands[monitor->ncmd++].level=level;
}
/* free control and monitor --------------------------------------------------*/
static void monitor_free(monitor_t *monitor)
{
    trace(4,"monitor_free\n");
    
    if (!monitor) return;
    
    monitor->state=0;
    pthread_join(monitor->thread,NULL);
    
    free(monitor);
}
/* signal function -----------------------------------------------------------*/
static void sigfunc(int sig)
{
    intr=1;
    signal(sig,sigfunc);
}
/* lock/unlock server --------------------------------------------------------*/
static void initlock(rtk_lock_t *lock) {rtk_initlock  (lock);}
static void lock    (rtk_lock_t *lock) {rtk_lock  (lock);}
static void unlock  (rtk_lock_t *lock) {rtk_unlock(lock);}

/* realtime local combination  -----------------------------------------------*/
static void rtlclcmb(lclcmbsvr_t *svr) 
{
    static gtime_t proctime={0};
    gtime_t nowtime={0};

    if (proctime.time==0) {
        if (rep==0) {
            proctime.time=(time_t) ceil(svr->ts.time / TOW_LSB) * TOW_LSB;
        }
        else if (svr->stat.time[0].time>0.0) {
            proctime.time=(time_t) ceil(svr->stat.time[0].time / TOW_LSB) * TOW_LSB;
            trace(3,"rtlclcmb : replay mode enabled, timestamps for station=%s\n",
                svr->stat.strp[0].site.name);
        }
    }
    if (proctime.time==0) return;
    
    if (rep==0) {
        nowtime = utc2gpst(timeget());
    }
    else{
        nowtime = svr->stat.time[0];
    }
    if (timediff(nowtime,proctime) < 0) return ;
    
    /* iono&trop correction interpolated value */
    if (svr->btype==BTYPE_GRID) {
        grid_intp_trop(proctime, &svr->stat, &svr->rtcm.lclblk);
        grid_intp_iono(proctime, &svr->stat, &svr->rtcm.lclblk, -1, NULL, NULL);
    }
    else if (svr->btype==BTYPE_STA) {
        sta_sel_trop(proctime, &svr->stat, &svr->rtcm.lclblk);
        sta_sel_iono(proctime, &svr->stat, &svr->rtcm.lclblk);
    }
    output_lclcmb(&svr->rtcm, svr->btype, &svr->ostr);
    output_lclcmb(&svr->rtcm, svr->btype, &svr->olog);
    svr->cnt++;
    trace(3,"rtlclcmb : output rtcm3 %s byte=%d\n",time_str(proctime,3),svr->btype);
    proctime=timeadd(proctime,TOW_LSB);
}
/* update stat corrections ---------------------------------------------------*/
static void update_stat(lclcmbsvr_t *svr, int siteno, sstat_t *sstat)
{
    stat_t *stat=&svr->stat;

    /* update stat corrections */
    stat->time[siteno]=sstat->time;
    memcpy(&stat->sion[siteno],&sstat->sion,sizeof(siteion_t));
    memcpy(&stat->strp[siteno],&sstat->strp,sizeof(sitetrp_t));
    trace(3, "update_stat: %s site=%s\n", time_str(sstat->time, 3), 
        stat->sion[siteno].site.name);
}
/* local combination server thread -------------------------------------------*/
static void *lclcmbsvr_thread(void *arg)
{
    int i,j,n,stano;
    int ret=0;
    lclcmbsvr_t *svr=(lclcmbsvr_t *)arg;
    unsigned int tick;
    uint8_t *p,*q;
    static char buff[MAXSITES][4096];
    static int nbyte[MAXSITES]={0};
    
    fprintf(stderr,"lclcmbrt server thread start\n");
    
    for (i=0;svr->state;i++) {
        tick=tickget();
        /* update stat corrections from input streams */
        for (i=0;i<svr->ninp;i++) {
            lock(&svr->lock);
            p=svr->ins[i].buff+svr->ins[i].nbyte; q=svr->ins[i].buff+4096;
            
            /* read data from input stream */
            if ((n=strread(&svr->ins[i].str,p,q-p))<=0) {
                unlock(&svr->lock);
                continue;
            }
            /* write data to log stream */
            strwrite(&svr->ins[i].log,p,n);
            svr->ins[i].nbyte+=n;
            unlock(&svr->lock);
            if (n>0) {
                stano = getstano(&svr->stat, svr->ins[i].sta);
                for (j=0;j<svr->ins[i].nbyte;j++) {
                    ret = input_stat(&svr->ins[i].sstat, svr->ins[i].buff[j], 
                        buff[i], &nbyte[i]);
                    if (ret>0&&svr->ins[i].dis==0) {
                        update_stat(svr, stano,&svr->ins[i].sstat);
                        svr->ins[i].cnt++;
                    }
                }
                svr->ins[i].nbyte=0;
            }
        }

        rtlclcmb(svr);
        
        sleepms(SVR_CYCLE-(int)(tickget()-tick));
    }
    fprintf(stderr,"lclcmbrt server thread stop\n");
    
    return 0;
}
/* decode path -----------------------------------------------------------------
* decode the given path into type and string path.
* This function was created for this source file with reference to 
* RTKLIB2.4.3 b35's str2str.c.
*-----------------------------------------------------------------------------*/
static int decodepath(const char *path, int *type, char *strpath)
{
    char buff[1024],*p;
    
    strcpy(buff,path);
    
    /* decode type */
    if (!(p=strstr(buff,"://"))) {
        strcpy(strpath,buff);
        *type=STR_FILE;
        return 1;
    }
    if      (!strncmp(path,"serial",6)) *type=STR_SERIAL;
    else if (!strncmp(path,"tcpsvr",6)) *type=STR_TCPSVR;
    else if (!strncmp(path,"tcpcli",6)) *type=STR_TCPCLI;
    else if (!strncmp(path,"ntripc",6)) *type=STR_NTRIPCAS;
    else if (!strncmp(path,"ntrips",6)) *type=STR_NTRIPSVR;
    else if (!strncmp(path,"ntrip", 5)) *type=STR_NTRIPCLI;
    else if (!strncmp(path,"file",  4)) *type=STR_FILE;
    else {
        fprintf(stderr,"stream path error: %s\n",buff);
        return 0;
    }
    strcpy(strpath,p+3);
    return 1;
}
/* read input stream option file ---------------------------------------------*/
static int read_istconf(lclcmbsvr_t *svr, const char *file)
{
    FILE *fp;
    gtime_t time0={0};
    char buff[2048],sta[32],path_str[1024],path_log[1024],*p;
    int i=0;
    
    if (!(fp=fopen(file,"r"))) {
        fprintf(stderr,"stream option file open error: %s\n",file);
        return 0;
    }
    while (fgets(buff,sizeof(buff),fp)&&svr->ninp<MAXSITES) {
        if ((p=strchr(buff,'#'))) *p='\0';
        
        if (sscanf(buff,"%31[^,],%31[^,],%s",sta,path_str,path_log)<3) {
            continue;
        }
        memset(&svr->ins[i],0x00,sizeof(instat_t));
        strcpy(svr->ins[i].sta,sta);
        strcpy(svr->ins[i].sstat.strp.site.name,sta);
        strcpy(svr->ins[i].sstat.sion.site.name,sta);
        svr->ins[i].fmt=STRFMT_STAT;
        svr->ins[i].stat=0;
        svr->ins[i].time=time0;
        if (decodepath(path_str,&svr->ins[i].type_str,svr->ins[i].path_str)==1) {
            strinit(&svr->ins[i].str);
        }
        if (decodepath(path_log,&svr->ins[i].type_log,svr->ins[i].path_log)==1) {
            strinit(&svr->ins[i].log);
        }
        i++;
    }
    fclose(fp);
    svr->ninp=i;
    return 1;
}
/* new stream local combination server ---------------------------------------*/
static int lclcmbsvr_new(const char *optfile,lclcmbsvr_t *svr)
{
    gtime_t time0={0};
    
    svr->ts=time0;
    svr->state=0;
    svr->ninp=0;
    strinit(&svr->ostr);
    strinit(&svr->olog);
    svr->thread=0;
    initlock(&svr->lock);
    
    /* read stream option file */
    if (!read_istconf(svr,optfile)) {
        return 0;
    }

    return 1;
}
/* open input stream -------------------------------------------------*/
static void inpstr_open(instat_t *ins)
{
    gtime_t time=utc2gpst(timeget()),time0={0};
    char path1[MAXSTRPATH],path2[MAXSTRPATH];
    int stropt[8]={30000,30000,3000,32768,0};
    
    strsetopt(stropt);
    
    ins->stat=ins->dis=0;
    ins->cnt=0;
    reppath(ins->path_str,path1,time ,"","");
    reppath(ins->path_log,path2,time0,"","");
    stropen(&ins->str,ins->type_str,STR_MODE_R,path1);
    stropen(&ins->log,ins->type_log,STR_MODE_W,path2);
    
    ins->tcon=time0;
    ins->time=time0;
    ins->nbyte=0;
}
/* close input stream --------------------------------------------------------*/
static void inpstr_close(instat_t *ins)
{
    strclose(&ins->str);
    strclose(&ins->log);
}
/* start local combination server --------------------------------------------*/
static int lclcmbsvr_start(lclcmbsvr_t *svr, lclopt_t *lopt)
{
    char path[MAXSTRPATH];
    int i,type;
    int opt[8]={60000,1000,1000,32768,30};
     
    strsetopt(opt);
    if (lopt->outpath) {
        decodepath(lopt->outpath,&type,path);
        if (!stropen(&svr->ostr,type,STR_MODE_W,path)) {
            fprintf(stderr,"output stream open error: %s\n",path);
        }
    }
    if (lopt->outlog) {
        decodepath(lopt->outlog,&type,path);
        if (!stropen(&svr->olog,type,STR_MODE_W,path)) {
            fprintf(stderr,"log stream open error: %s\n",path);
        }
    }
    svr->cnt=0;
    for (i=0;i<svr->ninp;i++) {
        inpstr_open(svr->ins+i);
    }
    svr->state=1;
    svr->ts=utc2gpst(timeget());
    
    if (pthread_create(&svr->thread,NULL,lclcmbsvr_thread,svr)) {
        
        for (i=0;i<svr->ninp;i++) {
            inpstr_close(svr->ins+i);
        }
        strclose(&svr->ostr);
        strclose(&svr->olog);
        return 0;
    }
    return 1;
}
/* stop local combination server ---------------------------------------------*/
static void lclcmbsvr_stop(lclcmbsvr_t *svr)
{
    int i;
    
    if (!svr->state) return;
    
    svr->state=0;
    pthread_join(svr->thread,NULL);
    
    for (i=0;i<svr->ninp;i++) {
        inpstr_close(svr->ins+i);
    }
    strclose(&svr->ostr);
    strclose(&svr->olog);
    svr->ninp=0;
}
/* show status ---------------------------------------------------------------*/
static int show_stat(lclcmbsvr_t *svr, console_t *cp)
{
    char *fmt,tstr[32],*buff,*p;
    char *sstr[]={"E","-","W","C","C"},msg[MAXSTRMSG]="",*estr[]={"E","D"};
    instat_t *ins;
    gtime_t gt;
    double tt;
    int i,stat,inb,inr,outb,outr;
    
    if (!(buff=p=(char *)malloc((svr->ninp+4)*(80+MAXSTRMSG)))) return -1;
    
    gt=utc2gpst(timeget());
    tt=timediff(gt,svr->ts);
    time2str(gt,tstr,0);
    p+=sprintf(p,"%s %s : STREAM STATUS (%.1fs)\n",ESC_BOLD,tstr,tt);
    p+=sprintf(p,"%3s: %-6s %s %9s %9s %6s %s %s %s%s\n",
               "NO","STA","F","CNT","KBYTE","KBPS","CS","ES","STR_STATUS",ESC_RESET);
    
    lock(&svr->lock);
    
    for (i=0;i<svr->ninp;i++) {
        ins=svr->ins+i;
        stat=strstat(&ins->str,msg);
        switch (ins->fmt) {
            case STRFMT_STAT: fmt="S"; break;
            default: fmt="-"; break;
        }
        strsum(&ins->str,&inb,&inr,NULL,NULL);
        p+=sprintf(p,"%3d: %-6s %s %9u %9.0f %6.1f %2s %2s %s\n",
                   i+1, ins->sta, fmt, ins->cnt, (unsigned int)inb*1E-3,
                   (unsigned int)inr*1E-3, sstr[stat+1], estr[ins->dis], msg);
    }
    stat=strstat(&svr->ostr,msg);
    strsum(&svr->ostr,NULL,NULL,&outb,&outr);
    p+=sprintf(p,"%3d: %-6s %s %9u %9.0f %6.1f %2s %2s %s\n",
               ++i,"OUT","R",svr->cnt, (unsigned int)outb*1E-3,
               (unsigned int)outr*1E-3,sstr[stat+1], "E", msg);
    
    unlock(&svr->lock);
    
    console_puts(cp,buff);
    trace(3,"%s",buff);
    free(buff);
    return 0;
}
/* hide password and options in path -----------------------------------------*/
static void hide_path(char *path)
{
    char *p;
    
    if ((p=strchr(path,'@'))) {
        for (p--;p>=path;p--) if (*p==':'||*p=='/') break; else *p='*';
    }
    if ((p=strstr(path,"::"))) *p='\0';
}
/* console command path ------------------------------------------------------*/
static int command_path(char **args, int n, console_t *cp)
{
    static const char *types[]={
        "","","","tcpsvr://","tcpcli://","","ntrips://","ntrip://"
    };
    lclcmbsvr_t *svr=(lclcmbsvr_t *)cp->ap;
    instat_t *ins;
    char path1[MAXSTRPATH],path2[MAXSTRPATH];
    int i;
    
    trace(3,"command_path: \n");
    console_printf(cp,"%s STREAM PATHS\n",ESC_BOLD);
    trace(3,"%s STREAM PATHS\n",ESC_BOLD);
    console_printf(cp,"%3s: %-6s %s | %s%s\n","NO","STA","STR_PATH","LOG_PATH",
                   ESC_RESET);
    trace(3,"%3s: %-6s %s | %s%s\n","NO","STA","STR_PATH","LOG_PATH",
                   ESC_RESET);
    
    for (i=0;i<svr->ninp;i++) {
        ins=svr->ins+i;
        sprintf(path1,"%s%s",types[ins->str.type],ins->str.path);
        sprintf(path2,"%s%s",types[ins->log.type],ins->log.path);
        hide_path(path1);
        hide_path(path2);
        console_printf(cp,"%3d: %-6s %s | %s\n",i+1,ins->sta,path1,path2);
        trace(3,"%3d: %-6s %s | %s\n",i+1,ins->sta,path1,path2);
    }
    sprintf(path1,"%s%s",types[svr->ostr.type],svr->ostr.path);
    sprintf(path2,"%s%s",types[svr->olog.type],svr->olog.path);
    hide_path(path1);
    hide_path(path2);
    console_printf(cp,"%3d: %-6s %s | %s\n",++i,"OUT",path1,path2);
    trace(3,"%3d: %-6s %s | %s\n",++i,"OUT",path1,path2);

    return 0;
}
/* console command enable ----------------------------------------------------*/
static int command_enable(char **args, int n, console_t *cp)
{
    lclcmbsvr_t *svr=(lclcmbsvr_t *)cp->ap;
    int i,j;

    trace(3,"command_enable: \n");
    for (i=1;i<n;i++) for (j=0;j<svr->ninp;j++) {
        if (strcmp(args[i],svr->ins[j].sta)) continue;
        console_printf(cp,"%s enabled.\n",svr->ins[j].sta);
        trace(3,"%s enabled.\n",svr->ins[j].sta);
        svr->ins[j].dis=0;
    }
    return 0;
}
/* console command disable ---------------------------------------------------*/
static int command_disable(char **args, int n, console_t *cp)
{
    lclcmbsvr_t *svr=(lclcmbsvr_t *)cp->ap;
    int i,j;

    trace(3,"command_disable: \n");
    for (i=1;i<n;i++) for (j=0;j<svr->ninp;j++) {
        if (strcmp(args[i],svr->ins[j].sta)) continue;
        console_printf(cp,"%s disabled.\n",svr->ins[j].sta);
        trace(3,"%s disabled.\n",svr->ins[j].sta);
        svr->ins[j].dis=1;
    }
    return 0;
}
/* console command logout ----------------------------------------------------*/
static int command_logout(char **args, int n, console_t *cp)
{
    trace(3,"command_logout: \n");
    return -1;
}
/* console command shutdown --------------------------------------------------*/
static int command_shutdown(char **args, int n, console_t *cp)
{
    char buff[256]="";
    
    trace(3,"command_shutdown: \n");
    console_printf(cp,"Confirm shutdown. (y or n): ");
    if (!console_gets(cp,buff,sizeof(buff))) return 0;
    if (!strcmp(buff,"y")||!strcmp(buff,"Y")) intr=1;
    trace(3,"get command[%s]\n", buff);
    return 0;
}
/* console command help ------------------------------------------------------*/
static int command_help(char **args, int n, console_t *cp)
{
    static const char *text[]={
        " stat [-r]     show stream status",
        " path          show stream paths",
        " enable sta    enable stream",
        " disable sta   disable stream",
        " shutdown      shutdown lclrtc",
        " logout        logout monitor console",
        " help|?        show help", NULL
    };
    int i;
    trace(3,"command_help: \n");
    for (i=0;text[i];i++) console_printf(cp,"%s\n",text[i]);
    return 0;
}
/* console command status ----------------------------------------------------*/
static int command_stat(char **args, int n, console_t *cp)
{
    lclcmbsvr_t *svr=(lclcmbsvr_t *)cp->ap;
    int i,rep=0;
    unsigned int tick;
    
    trace(3,"command_stat:\n");
    for (i=1;i<n;i++) {
        if (!strcmp(args[i],"-r")) rep=1;
    }
    if (!rep) {
        return show_stat(svr,cp);
    }
    for (i=0;!console_chkbrk(cp);i++) {
        tick=tickget();
        if (i%MAX(1,cp->rep_cyc/100)==0) {
            console_printf(cp,ESC_CLEAR);
            show_stat(svr,cp);
        }
        sleepms(100-(int)(tickget()-tick));
    }
    return 0;
}
/* open monitor console ------------------------------------------------------*/
static monitor_t *open_console(int port, lclcmbsvr_t *svr)
{
    monitor_t *mon;
    
    if (!(mon=monitor_new(port,INACT_TIMEOUT,LOGIN_PROMPT,svr))) {
        fprintf(stderr,"monitor console open error port=%d\n",port);
        return NULL;
    }
    monitor_addusr(mon,LOGIN_USER,LOGIN_PASSWD,0);
    monitor_addcmd(mon,"stat"    ,command_stat    ,0);
    monitor_addcmd(mon,"path"    ,command_path    ,0);
    monitor_addcmd(mon,"enable"  ,command_enable  ,0);
    monitor_addcmd(mon,"disable" ,command_disable ,0);
    monitor_addcmd(mon,"logout"  ,command_logout  ,0);
    monitor_addcmd(mon,"shutdown",command_shutdown,0);
    monitor_addcmd(mon,"help"    ,command_help    ,0);
    monitor_addcmd(mon,"?"       ,command_help    ,0);
    return mon;
}

/* lclcmbrt main -------------------------------------------------------------*/
int main(int argc, char **argv)
{
    monitor_t *mon=NULL;
    int i,n;
    int tlevel=0,port=0;
    
    lclopt_t lopt ={0};

    /* default settings for distance thresholds */
    svr.rtcm.lclblk.idist = MAX_DIST_ION;
    svr.rtcm.lclblk.tdist = MAX_DIST_TRP;
    svr.rtcm.lclblk.interp = MODE_DISTWGT;

    /* set options parameters*/
    for (i=1,n=0;i<argc;i++) {
        if (!strcmp(argv[i],"-instr")) {
            lopt.istfile=argv[++i];
        }
        else if (!strcmp(argv[i],"-idist")) {
            svr.rtcm.lclblk.idist = atof(argv[++i]);
        }
        else if (!strcmp(argv[i],"-tdist")) {
            svr.rtcm.lclblk.tdist = atof(argv[++i]);
        }
        else if (!strcmp(argv[i],"-grid")) {
            lopt.gridfile=argv[++i];
        }
        else if (!strcmp(argv[i],"-sta")) {
            lopt.stafile=argv[++i];
        }
        else if (!strcmp(argv[i],"-out" )&&i+1<argc) {
            lopt.outpath=argv[++i];
        }
        else if (!strcmp(argv[i],"-log" )&&i+1<argc) {
            lopt.outlog=argv[++i];
        }
        else if (!strcmp(argv[i],"-p")&&i+1<argc) port=atoi(argv[++i]);
        else if (!strcmp(argv[i],"-t")&&i+1<argc) {
            tlevel=atoi(argv[++i]);
        }
        else if (!strcmp(argv[i],"-rep")) {
            rep=1;
        }
        else if (*argv[i] == '-') {
            print_help();
            return -1;
        }
    }
    
    if (tlevel>0) {
        traceclose();
        traceopen(TRACEFILE);
        tracelevel(tlevel);
    }

    trace(3,"Parameters : %s\n",argv[1]);
    for (i=2;i<argc;i++) trace(3," %s\n",argv[i]);
    trace(3,"\n");

    /* parameter check */
    if (lopt.outpath==NULL) return -1;

    /* generate lclcmb server */
    if (lclcmbsvr_new(lopt.istfile,&svr)==0) return -1;

    /* generate grid/station setting */
    if (lopt.gridfile!=NULL) {
        svr.btype=BTYPE_GRID;
        if (initgridsta(lopt.gridfile,&svr.rtcm.lclblk,svr.btype)==0) {
            return 0;
        }
    }
    else if (lopt.stafile!=NULL) {
        svr.btype=BTYPE_STA;
        if (initgridsta(lopt.stafile,&svr.rtcm.lclblk,svr.btype)==0) {
            return 0;
        }
    }
    else{
        trace(2,"select output type -grid [grid file] or -sta [station file]\n");
        return 0;
    }

    /* open monitor console */
    if (port>0) {
        mon=open_console(port,&svr);
    }

    /* ignore SIGHUP and SIGPIPE */
    signal(SIGHUP ,SIG_IGN);
    signal(SIGPIPE,SIG_IGN);
    
    /* start local combination server */
    if (!lclcmbsvr_start(&svr,&lopt)) {
        monitor_free(mon);
        traceclose();
        return -1;
    }
    /* wait for SIGINT, SIGTERM */
    signal(SIGTERM,sigfunc);
    signal(SIGINT ,sigfunc);
    while (!intr) sleepms(100);
    
    /* stop monitor server */
    lclcmbsvr_stop(&svr);
    monitor_free(mon);
    
    traceclose();
    return 0;
}
