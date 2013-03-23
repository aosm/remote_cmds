/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * "Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 1.0 (the 'License').  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License."
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*	$OpenBSD: ypxfr.c,v 1.22 1997/07/30 12:07:02 maja Exp $ */

/*
 * Copyright (c) 1994 Mats O Jansson <moj@stacken.kth.se>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mats O Jansson
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef LINT
__unused static char rcsid[] = "$OpenBSD: ypxfr.c,v 1.22 1997/07/30 12:07:02 maja Exp $";
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>

#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>

#include "yplib_host.h"
#include "yplog.h"
#include "ypdb.h"
#include "ypdef.h"

extern char *__progname;		/* from crt0.o */
DBM	*db;

extern bool_t xdr_ypresp_all_seq();
extern int getrpcport(char *, int, int, int);

extern int (*ypresp_allfn)();
extern void *ypresp_data;

static int
ypxfr_foreach(status,keystr,keylen,valstr,vallen,data)
int status,keylen,vallen;
char *keystr,*valstr,*data;
{
	datum	key,val;

	if (status == YP_NOMORE)
		return(0);

	keystr[keylen] = '\0';
	valstr[vallen] = '\0';

	key.dptr = keystr;
	key.dsize = strlen(keystr);

	val.dptr = valstr;
	val.dsize = strlen(valstr);
	
	ypdb_store(db, key, val, YPDB_INSERT);

	return 0;
}

int
get_local_ordernum(domain, map, lordernum)
char *domain;
char *map;
u_int32_t *lordernum;
{
	char map_path[MAXPATHLEN];
	char order_key[] = YP_LAST_KEY;
	char order[MAX_LAST_LEN+1];
	struct stat finfo;
	DBM *db;
	datum k,v;
	int status;

	/* This routine returns YPPUSH_SUCC or YPPUSH_NODOM */
	
	status = YPPUSH_SUCC;
	
	snprintf(map_path, sizeof map_path, "%s/%s", YP_DB_PATH, domain);
	if (!((stat(map_path, &finfo) == 0) &&
	      ((finfo.st_mode & S_IFMT) == S_IFDIR))) {
		fprintf(stderr, "%s: domain %s not found locally\n",
		    __progname, domain);
		status = YPPUSH_NODOM;
	}

	if(status > 0) {
		snprintf(map_path, sizeof map_path, "%s/%s/%s%s",
		    YP_DB_PATH, domain, map, YPDB_SUFFIX);
		if(!(stat(map_path, &finfo) == 0)) {
			status = YPPUSH_NOMAP;
		}
	}
	
	if(status > 0) {
		snprintf(map_path, sizeof map_path, "%s/%s/%s",
		    YP_DB_PATH, domain, map);
		db = ypdb_open(map_path, O_RDONLY, 0444);
		if(db == NULL) {
			status = YPPUSH_DBM;
		}
		
	}
		
	if(status > 0) {
		k.dptr = (char *)&order_key;
		k.dsize = YP_LAST_LEN;

		v = ypdb_fetch(db,k);
		ypdb_close(db);
		
		if (v.dptr == NULL) {
			*lordernum = 0;
		} else {
	        	strncpy(order, v.dptr, sizeof order-1);
			order[sizeof order-1] = '\0';
			*lordernum = (u_int32_t)atol(order);
		}
	}

	if((status == YPPUSH_NOMAP) || (status == YPPUSH_DBM)) {
		*lordernum = 0;
		status = YPPUSH_SUCC;
	}

	return(status);

}

int
get_remote_ordernum(client, domain, map, lordernum, rordernum)
CLIENT *client;
char *domain;
char *map;
u_int32_t lordernum;
u_int32_t *rordernum;
{
	int status;

	status = yp_order_host(client, domain, map, rordernum);

	if (status == 0) {
		if(*rordernum <= lordernum) {
			status = YPPUSH_AGE;
		} else {
			status = YPPUSH_SUCC;
		}
	} 

	return status;
}

void
get_map(client,domain,map,incallback)
CLIENT *client;
char *domain;
char *map;
struct ypall_callback *incallback;
{
	(void)yp_all_host(client, domain, map, incallback);
		
}

DBM *
create_db(domain,map,temp_map)
char *domain;
char *map;
char *temp_map;
{
	return ypdb_open_suf(temp_map, O_RDWR, 0444);
}

int
install_db(domain,map,temp_map)
char *domain;
char *map;
char *temp_map;
{
	char	db_name[MAXPATHLEN];

	snprintf(db_name, sizeof db_name, "%s/%s/%s%s",
	    YP_DB_PATH, domain, map, YPDB_SUFFIX);
	rename(temp_map, db_name);

	return YPPUSH_SUCC;
}

int
add_order(db, ordernum)
DBM *db;
u_int32_t ordernum;
{
	char	datestr[11];
	datum	key,val;
	char	keystr[] = YP_LAST_KEY;
	int	status;

	sprintf(datestr, "%010u", ordernum);

	key.dptr = keystr;
	key.dsize = strlen(keystr);
	
	val.dptr = datestr;
	val.dsize = strlen(datestr);
	
	status = ypdb_store(db, key, val, YPDB_INSERT);
	if(status >= 0) {
		status = YPPUSH_SUCC;
	} else {
		status = YPPUSH_DBM;
	}
	return(status);
}

int
add_master(client, domain, map, db)
CLIENT *client;
char *domain;
char *map;
DBM *db;
{
	char	keystr[] = YP_MASTER_KEY;
	char	*master;
	int	status;
	datum	key,val;

	master = NULL;

	/* Get MASTER */

	status = yp_master_host(client, domain, map, &master);
	
	if(master != NULL) {
	  key.dptr = keystr;
	  key.dsize = strlen(keystr);
	  
	  val.dptr = master;
	  val.dsize = strlen(master);
	
	  status = ypdb_store(db, key, val, YPDB_INSERT);
	  if(status >= 0) {
	  	status = YPPUSH_SUCC;
	  } else {
	  	status = YPPUSH_DBM;
	  }
	}

	return status;
}

int
add_interdomain(client, domain, map, db)
CLIENT *client;
char *domain;
char *map;
DBM *db;
{
	char	keystr[] = YP_INTERDOMAIN_KEY;
	char	*value;
	int	vallen;
	int	status;
	datum	k,v;

	/* Get INTERDOMAIN */

	k.dptr = keystr;
	k.dsize = strlen(keystr);

	status = yp_match_host(client, domain, map,
			       k.dptr, k.dsize, &value, &vallen);
	
	if(status == 0 && value) {
		v.dptr = value;
		v.dsize = vallen;
		
		if(v.dptr != NULL) {
			status = ypdb_store(db,k,v,YPDB_INSERT);
			if(status >= 0) {
				status = YPPUSH_SUCC;
			} else {
				status = YPPUSH_DBM;
			}
		}
	}

	return 1;
}

int
add_secure(client, domain, map, db)
CLIENT *client;
char *domain;
char *map;
DBM *db;
{
	char	keystr[] = YP_SECURE_KEY;
	char	*value;
	int	vallen;
	int	status;
	datum	k,v;

	/* Get SECURE */

	k.dptr = keystr;
	k.dsize = strlen(keystr);

	status = yp_match_host(client, domain, map,
			       k.dptr, k.dsize, &value, &vallen);
	
	if(status > 0) {
		v.dptr = value;
		v.dsize = vallen;
		
		if(v.dptr != NULL) {
			status = ypdb_store(db,k,v,YPDB_INSERT);
			if(status >= 0) {
				status = YPPUSH_SUCC;
			} else {
				status = YPPUSH_DBM;
			}
		}
	}

	return status;

}

int
send_clear(client)
CLIENT *client;
{
	struct	timeval tv;
	int	r;
	int	status;

	status = YPPUSH_SUCC;

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	/* Send CLEAR */

	r = clnt_call(client, YPPROC_CLEAR,
		      (xdrproc_t)xdr_void, 0, (xdrproc_t)xdr_void, 0, tv);
	if(r != RPC_SUCCESS) {
		clnt_perror(client, "yp_clear: clnt_call");
	}

	return status;

}

int
send_reply(client,status,tid)
CLIENT *client;
u_long	status;
u_long  tid;
{
	struct	timeval tv;
	struct	ypresp_xfr resp;
	int	r;

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	resp.transid = tid;
	resp.xfrstat = status;

	/* Send CLEAR */

	r = clnt_call(client, 1,
		      (xdrproc_t)xdr_ypresp_xfr, &resp, (xdrproc_t)xdr_void, 0, tv);
	if(r != RPC_SUCCESS) {
		clnt_perror(client, "yppushresp_xdr: clnt_call");
	}

	return status;

}

int
main (argc,argv)
int argc;
char *argv[];
{
	int	 usage = 0;
	int	 cflag = 0;
	int	 fflag = 0;
	int	 Cflag = 0;
	int	 ch;
	extern	 char *optarg;
	char	 *domain;
	char	 *host = NULL;
	char	 *srcdomain = NULL;
	char	 *tid = NULL;
	char	 *prog = NULL;
	char	 *ipadd = NULL;
	char	 *port = NULL;
	char	 *map = NULL;
	u_int32_t ordernum, new_ordernum;
	struct	 ypall_callback callback;
	CLIENT   *client;
	int	 status,xfr_status;
	int	 srvport;
	
	status = YPPUSH_SUCC;
	client = NULL;

	yp_get_default_domain(&domain);

	while ((ch = getopt(argc, argv, "cd:fh:s:C:")) != -1)
	  switch (ch) {
	  case 'c':
	    cflag++;
	    break;
	  case 'd':
	    if (strchr(optarg, '/'))	/* Ha ha, we are not listening */
		break;
	    domain = optarg;
	    break;
	  case 'f':
	    fflag++;
	    break;
	  case 'h':
	    host = optarg;
	    break;
	  case 's':
	    if (strchr(optarg, '/'))	/* Ha ha, we are not listening */
		break;
	    srcdomain = optarg;
	    break;
	  case 'C':
	    if (optind + 3 >= argc) {
		usage++;
		optind = argc;
		break;
	    }
	    Cflag++;
	    tid = optarg;
	    prog = argv[optind++];
	    ipadd = argv[optind++];
	    port = argv[optind++];
	    break;
	  default:
	    usage++;
	    break;
	  }

	if(optind + 1 != argc) {
	  usage++;
	} else {
	  map = argv[optind];
	}

	if (usage) {
		status = YPPUSH_BADARGS;
		fprintf(stderr, "usage: %s %s %s\n",
		    "[-cf] [-d domain] [-h host] [-s domain]",
		    "[-C tid prog ipadd port] mapname\n",
		    __progname);
	}

	if (status > 0) {
		ypopenlog();

		yplog("ypxfr: Arguments:");
		yplog("YP clear to local: %s", (cflag) ? "no" : "yes");
		yplog("   Force transfer: %s", (fflag) ? "yes" : "no");
		yplog("           domain: %s", domain); 
		yplog("             host: %s", host);
		yplog("    source domain: %s", srcdomain);
		yplog("          transid: %s", tid);
		yplog("             prog: %s", prog);
		yplog("             port: %s", port);
		yplog("            ipadd: %s", ipadd);
		yplog("              map: %s", map);

		if(fflag != 0) {
			ordernum = 0;
		} else {
			status = get_local_ordernum(domain, map, &ordernum);
		}
	}

	if (status > 0) {

	        yplog("Get Master");

		if (host == NULL) {
			if (srcdomain == NULL) {
				status = yp_master(domain,map,&host);
		        } else {
				status = yp_master(srcdomain,map,&host);
			}
			if(status == 0) {
				status = YPPUSH_SUCC;
			} else {
				status = -status;
			}
		}
	};

	/* XXX this is raceable if portmap has holes! */
	if (status > 0) {
		
	        yplog("Check for reserved port on host: %s", host); 

		srvport = getrpcport(host,YPPROG,YPVERS,IPPROTO_TCP);
		if (srvport >= IPPORT_RESERVED)
			status = YPPUSH_REFUSED;
		
	}

	if (status > 0) {
	  	
	        yplog("Connect host: %s", host); 

		client = yp_bind_host(host,YPPROG,YPVERS,0,1);

		status = get_remote_ordernum(client, domain, map,
					     ordernum, &new_ordernum);
		
	}

	if (status == YPPUSH_SUCC) {
		char	tmpmapname[MAXPATHLEN];
		int	fd;

		/* Create temporary db */
		snprintf(tmpmapname, sizeof tmpmapname,
		    "%s/%s/ypdbXXXXXXXXXX", YP_DB_PATH, domain);
		fd = mkstemp(tmpmapname);
		if (fd == -1)
			status = YPPUSH_DBM;
		else
			close(fd);

		if (status > 0) {
			db = create_db(domain,map,tmpmapname);
			if(db == NULL)
				status = YPPUSH_DBM;
		}

	  	/* Add ORDER */
		if(status > 0) {
			status = add_order(db, new_ordernum);
		}
		
		/* Add MASTER */
		if(status > 0) {
			status = add_master(client,domain,map,db);
		}
		
	        /* Add INTERDOMAIN */
		if(status > 0) {
			status = add_interdomain(client,domain,map,db);
		}
		
	        /* Add SECURE */
		if(status > 0) {
			status = add_secure(client,domain,map,db);
		}
		
		if(status > 0) {
			callback.foreach=ypxfr_foreach;
			get_map(client,domain,map,&callback);
		}
		
		/* Close db */
		if(db != NULL) {
			ypdb_close(db);
		}

		/* Rename db */
		if(status > 0) {
			status = install_db(domain,map,tmpmapname);
		} else {
			unlink(tmpmapname);
			status = YPPUSH_SUCC;
		}
		
	}
	
	xfr_status = status;

	if(client != NULL) {
		clnt_destroy(client);
	}

	/* YP_CLEAR */

	if(!cflag) {
		client = yp_bind_local(YPPROG,YPVERS);
		status = send_clear(client);
		clnt_destroy(client);
	}

	if(Cflag > 0) {
		/* Send Response */
		client = yp_bind_host(ipadd,
				      atoi(prog),
				      1,
				      atoi(port),
				      0);
		status = send_reply(client,xfr_status,atoi(tid));
		clnt_destroy(client);
	}

	return(0);

}

