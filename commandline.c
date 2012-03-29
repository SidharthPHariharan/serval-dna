/* 
Serval Distributed Numbering Architecture (DNA)
Copyright (C) 2010 Paul Gardner-Stephen 

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <sys/stat.h>
#include "serval.h"

typedef struct command_line_option {
  int (*function)(int argc,char **argv,struct command_line_option *o);
  char *words[32]; // 32 words should be plenty!
  unsigned long long flags;
#define CLIFLAG_NONOVERLAY (1<<0) /* Uses a legacy IPv4 DNA call instead of overlay mnetwork */
#define CLIFLAG_STANDALONE (1<<1) /* Cannot be issued to a running instance */
  char *description; // describe this invocation
} command_line_option;

extern command_line_option command_line_options[];

static int servalNodeRunning(int *pid)
{
  char *instancepath = serval_instancepath();
  struct stat st;
  int r=stat(instancepath,&st);
  if (r) {
    fprintf(stderr,
	    "ERROR: Instance path '%s' non existant or not accessable.\n"
	    "       Operating system says: %s (errno=%d)\n",
	    instancepath,strerror(errno),errno);
    fprintf(stderr,
	    "       (Set SERVALINSTANCE_PATH to specify an alternate location.)\n");
    return -1;
  }
  if ((st.st_mode&S_IFMT)!=S_IFDIR) {
    fprintf(stderr,
	    "ERROR: Instance path must be a valid directory.\n"
	    "       '%s' is not a directory.\n",instancepath);
    *pid=-1;
    return -1;
  }

  int running=0;
  char filename[1024];
  if (FORM_SERVAL_INSTANCE_PATH(filename, "serval.pid")) {
    FILE *f=fopen(filename,"r");
    if (f) {
      char line[1024];
      line[0]=0; fgets(line,1024,f);    
      *pid = strtoll(line,NULL,10);
      running=*pid;
      if (running) {
	/* Check that process is really running.
	  Some systems don't have /proc (including mac), 
	  so we need to find out some otherway.*/
	running=1; // assume pid means is running for now      
      }
      fclose(f);
    } 
  }

  return running;
}

int cli_usage() {
  fprintf(stderr,"\nServal Mesh version <version>.\n");
  fprintf(stderr,"Usage:\n");
  int i,j;
  for(i=0;command_line_options[i].function;i++)
    {
      for(j=0;command_line_options[i].words[j];j++)
	fprintf(stderr," %s",command_line_options[i].words[j]);
      fprintf(stderr,"\n   %s\n",command_line_options[i].description);
    }
  return -1;
}

char *cli_arg(int argc,char **argv,command_line_option *o,
	    char *argname,char *defaultvalue)
{
  int arglen=strlen(argname)+2;
  int i;
  for(i=0;o->words[i];i++)
    if ((strlen(o->words[i])==arglen)
	&&(o->words[i][0]=='<')
	&&(o->words[i][arglen-1]=='>')
	&&(!strncasecmp(&o->words[i][1],argname,arglen-2)))
      {
	/* Found the arg, so return the corresponding argument */
	return argv[i];
      }

  /* No matching argument was found, so return default value.
     It might seem that this should never happen, but it can because more than
     one version of a command line optiom may exist, one with a given argument
     and another without, and allowing a default value means we can have a single
     function handle both in a fairly simple manner. */
  return defaultvalue;
}

/* args[] excludes command name (unless hardlinks are used to use first words 
   of command sequences as alternate names of the command. */
int parseCommandLine(int argc, char **args)
{
  int i,j;
  int ambiguous=0;
  int cli_call=-1;

  for(i=0;command_line_options[i].function;i++)
    {
      for(j=0;(j<argc)&&command_line_options[i].words[j];j++)
	if ((command_line_options[i].words[j][0]!='<')&&
	    strcasecmp(command_line_options[i].words[j],args[j])) {
	  /* Words don't match, and word is not a place-holder for an argument,
	     so it isn't this command line call. */
	  break;
	}

      if ((j==argc)&&(!command_line_options[i].words[j])) {
	/* We used up all words in args and command line call sequence, so we have
	   a match. If we have multiple matches, then note that the call is 
	   ambiguous. */
	if (cli_call>=0) ambiguous++;
	if (ambiguous==1) {
	  fprintf(stderr,"Ambiguous command line call:\n   ");
	  for(j=0;j<argc;j++) fprintf(stderr," %s",args[j]);
	  fprintf(stderr,"\nMatches the following known command line calls:\n");
	}
	if (ambiguous) {
	  fprintf(stderr,"   ");
	  for(j=0;j<argc;j++) fprintf(stderr," %s",command_line_options[i].words[j]);
	  fprintf(stderr,"\n");
	}
	cli_call=i;
      }
    }
  
  /* Don't process ambiguous calls */
  if (ambiguous) return -1;
  /* Complain if we found no matching calls */
  if (cli_call<0) return cli_usage();

  /* Otherwise, make call */
  return command_line_options[cli_call].function(argc,args,
					  &command_line_options[cli_call]);
}

int app_dna_lookup(int argc,char **argv,struct command_line_option *o)
{
  return WHY("Not implemented");
}

int confValueRotor=0;
char confValue[4][128];
char *confValueGet(char *var,char *defaultValue)
{
  if (!var) return defaultValue;
  int varLen=strlen(var);

  char filename[1024];
  if (!FORM_SERVAL_INSTANCE_PATH(filename, "serval.conf")) {
    fprintf(stderr, "Using default value of %s: %s\n", var, defaultValue);
    return defaultValue;
  }
  FILE *f = fopen(filename,"r");
  if (!f) {
    fprintf(stderr, "Cannot open serval.conf.  Using default value of %s: %s\n", var, defaultValue);
    return defaultValue;
  }
  
  char line[1024];
  line[0]=0; fgets(line,1024,f);
  while(line[0]) {
    if (!strncasecmp(line,var,varLen)) {
      if (line[varLen]=='=') {
	fclose(f);
	if (strlen(&line[varLen+1])>127) return defaultValue;
	/* The rotor is used to pick which of four buffers to return in.
	   This allows the use of up to four calls to confValueGet() in
	   a single string formatting exercise, without unexpected side
	   effect. */
	confValueRotor++; confValueRotor&=3;
	strcpy(&confValue[confValueRotor][0],&line[varLen+1]);
	return &confValue[confValueRotor][0];
      }
    }
    line[0]=0; fgets(line,1024,f);
  }
  fclose(f);
  return defaultValue;
}

int app_server_start(int argc,char **argv,struct command_line_option *o)
{
  int foregroundP=0;
  char *instancepath=getenv("SERVALINSTANCE_PATH");
  if (!instancepath) instancepath=DEFAULT_INSTANCE_PATH;

  /* Process optional arguments */
  if ((argc>=3)&&(!strcasecmp(argv[2],"foreground"))) foregroundP=1;
  if ((argc>=4)&&(!strcasecmp(argv[2],"in"))) instancepath=argv[3];

  /* Record instance path for easy access by whole process */
  thisinstancepath=strdup(instancepath);

  /* Now that we know our instance path, we can ask for the default set of
     network interfaces that we will take interest in. */
  overlay_interface_args(confValueGet("interfaces",""));
  if (strlen(confValueGet("interfaces",""))<1) {
    fprintf(stderr,
	    "WARNING: Noone has told me which network interfaces to listen on.\n"
	    "         You should probably put something in the interfaces setting.\n");
  }

  setVerbosity(confValueGet("debug",""));

  int pid=-1;
  int running = servalNodeRunning(&pid);
  if (running<0) return -1;
  if (running>0) {
    fprintf(stderr,"ERROR: Serval process already running (pid=%d)\n",pid);
    return -1;
  }
  /* Start the Serval process.
     All server settings will be read by the server process from the
     instance directory when it starts up.
     We can just become the server process ourselves --- no need to fork.
     
  */
  rhizome_datastore_path=strdup(instancepath);
  rhizome_opendb();
  char *hlr_file = asprintf("%s/%s", instancepath, "hlr.dat");
  hlr_size=atof(confValueGet("hlr_size","1"))*1048576;
  if (hlr_size<0) {
    fprintf(stderr,"HLR Size must be >0MB\n");
    return -1;
  }

  overlayMode=1;
  return server(hlr_file,hlr_size,foregroundP);
}

int app_server_stop(int argc,char **argv,struct command_line_option *o)
{
  thisinstancepath = cli_arg(argc,argv,o,"instance path",getenv("SERVALINSTANCE_PATH"));

  int pid=-1;
  int running = servalNodeRunning(&pid);
  if (running>0) {
    /* Is running, so we can try to kill it.
       This is a little complicated by the fact that we catch most signals
       so that unexpected aborts just restart.
       What we can do is put some code in the signal handler that does abort
       the process if a certain file exists, perhaps instance_path/doshutdown,
       and removes the file.
    */
    if (pid<0) {
      WHY("Could not determine process id of Serval process.  Stale instance perhaps?");
      return -1;
    }

    char stopfile[1024];
    FILE *f;
    if (!(FORM_SERVAL_INSTANCE_PATH(stopfile, "doshutdown") && (f = fopen(stopfile, "w")))) {
      WHY("Could not create shutdown file");
      return -1;
    }
    fclose(f);
    int result=kill(pid,SIGHUP);
    if (!result) {
      fprintf(stderr,"Stop request sent to Serval process.\n");
    } else {
      WHY("Could not send SIGHUP to Serval process.");
      switch (errno) {
      case EINVAL: WHY("This is embarassing, but the operating system says I don't know how to send a signal."); break;
      case EPERM: WHY("I don't have permission to stop the Serval process.  You could try using sudo, or run the stop command as the appropriate user."); break;
      case ESRCH: WHY("The process id I have recorded doesn't seem to exist anymore.  Did someone kill the process without telling me?"); 
	/* Clean up any lingering mess */
	servalShutdownCleanly();
	break;
      default:
	perror("This is reason given by the operating system");
      }
      return -1;
    }

    /* Allow a few seconds for the process to die, and keep an eye on things 
       while this is happening. */
    time_t timeout=time(0)+5;
    while(timeout>time(0)) {
      pid=-1;
      int running = servalNodeRunning(&pid);
      if (running<1) {
	fprintf(stderr,"Serval process appears to have stopped.\n");
	return 0;
      }
    }
    return WHY("I tried to stop it, but it seems that the Serval process is still running.");

  } else {
      return WHY("Serval process for that instance does not appear to be running.");
  }
 
  return WHY("Not implemented");
}

int app_server_status(int argc,char **argv,struct command_line_option *o)
{
  thisinstancepath = cli_arg(argc, argv, o, "instance path", NULL);
  
  /* Display configuration information */
  char filename[1024];
  FILE *f;
  if (FORM_SERVAL_INSTANCE_PATH(filename, "serval.conf") && (f = fopen(filename, "r"))) {
    char line[1024];
    line[0]=0; fgets(line,1024,f);
    printf("\nServal Mesh configuration:\n");
    while(line[0]) {
      printf("   %s",line);
      line[0]=0; fgets(line,1024,f);
    }
    fclose(f);
  }

  /* Display running status of daemon from serval.pid file */
  int pid=-1;
  int running = servalNodeRunning(&pid);
  if (running<0) return -1;

  printf("For Serval Mesh instance %s:\n", serval_instancepath());
  if (running)
    printf("  Serval mesh process is running (pid=%d)\n",pid);
  else
    printf("  Serval Mesh process not running\n");
  
  return 0;
}

int app_mdp_ping(int argc,char **argv,struct command_line_option *o)
{
  char *sid=cli_arg(argc,argv,o,"SID|broadcast","broadcast");

  /* MDP frames consist of:
     destination SID (32 bytes)
     destination port (4 bytes)
     payload length (2 bytes)
     payload (rest of packet) */
  overlay_mdp_frame mdp;

  /* Get list of local addresses */
  mdp.packetTypeAndFlags=MDP_GETADDRS;
  mdp.addrlist.first_sid=-1;
  mdp.addrlist.last_sid=0x7fffffff;
  mdp.addrlist.frame_sid_count=MDP_MAX_SID_REQUEST;
  int result=overlay_mdp_send(&mdp,MDP_AWAITREPLY,5000);
  if (result) {
    if (mdp.packetTypeAndFlags==MDP_ERROR)
      {
	fprintf(stderr,"Could not get list of local MDP addresses\n");
	fprintf(stderr,"  MDP Server error #%d: '%s'\n",
		mdp.error.error,mdp.error.message);
      }
    else
      fprintf(stderr,"Could not get list of local MDP addresses\n");
    return -1;
  } else {
    if (mdp.packetTypeAndFlags!=MDP_ADDRLIST) 
      return WHY("MDP Server returned wrong frame type.");
  }

  /* Bind to MDP socket and await confirmation */
  int port=32768+(random()&32767);
  mdp.packetTypeAndFlags=MDP_BIND;
  if (0)
    bzero(&mdp.bind.sid[0],SID_SIZE); // listen on all addressses
  else
    /* Listen on a local address.
       Must be done before setting anything else in mdp.bind, since mdp.bind
       and mdp.addrlist share storage as a union in the mdp structure. */
    bcopy(&mdp.addrlist.sids[0][0],mdp.bind.sid,SID_SIZE);
  unsigned char srcsid[SID_SIZE];
  bcopy(mdp.bind.sid,srcsid,SID_SIZE);
  mdp.bind.port_number=port;
  result=overlay_mdp_send(&mdp,MDP_AWAITREPLY,5000);
  if (result) {
    if (mdp.packetTypeAndFlags==MDP_ERROR)
      fprintf(stderr,"Could not bind to MDP port %d: error=%d, message='%s'\n",
	      port,mdp.error.error,mdp.error.message);
    else
      fprintf(stderr,"Could not bind to MDP port %d (no reason given)\n",port);
    return -1;
  }

  /* First sequence number in the echo frames */
  unsigned int firstSeq=random();
  unsigned int sequence_number=firstSeq;

  /* Get SID that we want to ping.
     XXX - allow lookup of SID prefixes and telephone numbers
     (that would require MDP lookup of phone numbers, which doesn't yet occur) */
  int i;
  unsigned char ping_sid[SID_SIZE];
  if (strcasecmp(sid,"broadcast")) {
    stowSid(ping_sid,0,sid);
  } else {
    for(i=0;i<SID_SIZE;i++) ping_sid[i]=0xff;
  }

  /* XXX Eventually we should try to resolve SID to phone number and vice versa */
  printf("MDP PING %s (%s): 12 data bytes\n",
	 overlay_render_sid(ping_sid),
	 overlay_render_sid(ping_sid));

  while(1) {
    /* Now send the ping packets */
    mdp.packetTypeAndFlags=MDP_TX|MDP_NOCRYPT|MDP_NOSIGN;
    mdp.out.src.port=port;
    bcopy(srcsid,mdp.out.src.sid,SID_SIZE);
    /* Set destination to broadcast */
    for(i=0;i<SID_SIZE;i++) mdp.out.dst.sid[i]=ping_sid[i];
    /* Set port to well known echo port (from /etc/services) */
    mdp.out.dst.port=7;
    mdp.out.payload_length=4+8;
    int *seq=(int *)&mdp.out.payload;
    *seq=sequence_number;
    long long *txtime=(long long *)&mdp.out.payload[4];
    *txtime=overlay_gettime_ms();
    
    int res=overlay_mdp_send(&mdp,0,0);
    if (res) {
      fprintf(stderr,"ERROR: Could not dispatch PING frame #%d (error %d)\n",
	      sequence_number-firstSeq,res);
      if (mdp.packetTypeAndFlags==MDP_ERROR) 
	fprintf(stderr,"       Error message: %s\n",mdp.error.message);
    }

    /* Now look for replies until one second has passed, and print any replies
       with appropriate information as required */
    long long now=overlay_gettime_ms();
    long long timeout=now+1000;

    while(now<timeout) {
      long long timeout_ms=timeout-overlay_gettime_ms();
      result = overlay_mdp_client_poll(timeout_ms);

      if (result>0) {
	int ttl=-1;
	while (overlay_mdp_recv(&mdp,&ttl)==0) {
	  switch(mdp.packetTypeAndFlags&MDP_TYPE_MASK) {
	  case MDP_ERROR:
	    fprintf(stderr,"mdpping: overlay_mdp_recv: %s (code %d)\n",
		    mdp.error.message,mdp.error.error);
	    break;
	  case MDP_RX:
	    {
	      int *rxseq=(int *)&mdp.in.payload;
	      long long *txtime=(long long *)&mdp.in.payload[4];
	      printf("%s: seq=%d time=%lld ms\n",
		     overlay_render_sid(mdp.in.src.sid),
		     (*rxseq)-firstSeq,overlay_gettime_ms()-*txtime);
	    }
	    break;
	  default:
	    fprintf(stderr,"mdpping: overlay_mdp_recv: Unexpected MDP frame type 0x%x\n",mdp.packetTypeAndFlags);
	    break;
	  }
	}
      }
      now=overlay_gettime_ms();
      if (servalShutdown) {
	/* XXX Report final statistics before going */
	WHY("I should be giving you ping statistics now");
	return 0;
      }
    }
    sequence_number++;
    timeout=now+1000;
  }

  return 0;
}

int app_server_set(int argc,char **argv,struct command_line_option *o)
{
  char *var=cli_arg(argc,argv,o,"variable","");
  char *val=cli_arg(argc,argv,o,"value","");

  char conffile[1024];
  FILE *in;
  if (!FORM_SERVAL_INSTANCE_PATH(conffile, "serval.conf") ||
      !((in = fopen(conffile, "r")) || (in = fopen(conffile, "w")))
    ) {
    return WHY("could not read configuration file.");
  }

  char tempfile[1024];
  FILE *out;
  if (!FORM_SERVAL_INSTANCE_PATH(tempfile, "serval.conf.temp") ||
      !(out = fopen(tempfile, "w"))
    ) {
    fclose(in);
    return WHY("could not write temporary file.");
  }

  /* Read and write lines of config file, replacing the variable in question
     if required.  If the variable didn't already exist, then write it out at
     the end. */
  char line[1024];
  int found=0;
  int varlen=strlen(var);
  line[0]=0; fgets(line,1024,in);
  while(line[0]) {
    if ((!strncasecmp(var,line,varlen))&&(line[varlen]=='='))
      {
	fprintf(out,"%s=%s\n",var,val);
	found=1;
      }
    else
      fprintf(out,"%s",line);
    line[0]=0; fgets(line,1024,in);
  }
  if (!found) fprintf(out,"%s=%s\n",var,val);
  fclose(in); fclose(out);
  
  if (rename(tempfile,conffile)) {
    return WHYF("Failed to rename \"%s\" to \"%s\".", tempfile, conffile);
  }

  return 0;
}

/* NULL marks ends of command structure.
   "<anystring>" marks an arg that can take any value.
   Only exactly matching prototypes will be used.
   Together with the description, this makes it easy for us to auto-generate the
   list of valid command line formats for display to the user if they try an
   invalid one.  It also means we can do away with getopt() etc.

   Keep this list alphabetically sorted for user convenience.
*/
command_line_option command_line_options[]={
  {app_dna_lookup,{"dna","lookup","<did>",NULL},CLIFLAG_NONOVERLAY,"Lookup the SIP/MDP address of the supplied telephone number (DID)."},
  {cli_usage,{"help",NULL},0,
   "Display command usage."},
  {app_server_start,{"node","start"},CLIFLAG_STANDALONE,
   "Start Serval Mesh node process.  Instance path is read from SERVALINSTANCE_PATH environment variable."},
  {app_server_start,{"node","start","foreground"},CLIFLAG_STANDALONE,
   "Start Serval Mesh node process without detatching from foreground."},
  {app_server_start,{"node","start","in","<instance path>"},CLIFLAG_STANDALONE,
   "Start Serval Mesh node process.  Instance path is as specified."},
  {app_server_stop,{"node","stop"},0,
   "Ask running Serval Mesh node process to stop. Instance path is read from SERVALINSTANCE_PATH environment variable."},
  {app_server_stop,{"node","stop","in","<instance path>"},0,
   "Ask running Serval Mesh node process to stop.  Instance path as specified."},
  {app_server_status,{"node","status"},0,
   "Display information about any running Serval Mesh node."},
  {app_mdp_ping,{"mdp","ping","<SID|broadcast>"},CLIFLAG_STANDALONE,
   "Attempts to ping specified node via Mesh Datagram Protocol (MDP)."},
  {app_server_set,{"set","<variable>","<value>"},0,
   "Set specified configuration variable."},
  {NULL,{NULL}}
};
