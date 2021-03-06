/* -*- mode: C; c-basic-offset: 4 -*- */
/* ex: set shiftwidth=4 tabstop=4 expandtab: */
/*
 * Copyright (c) 2008-2014, Georgia Tech Research Corporation
 * Copyright (c) 2015, Rice University
 * All rights reserved.
 *
 * Author(s): Neil T. Dantam <ntd@rice.edu>
 * Georgia Tech Humanoid Robotics Lab
 * Under Direction of Prof. Mike Stilman <mstilman@cc.gatech.edu>
 *
 *
 * This file is provided under the following "BSD-style" License:
 *
 *
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *   * Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 *
 */

/** \file achproxy.h
 *  \author Neil T. Dantam
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include "ach.h"
#include "ach/private_posix.h"
#include "achutil.h"
#include "achd.h"

size_t opt_msg_cnt = ACH_DEFAULT_FRAME_COUNT;
bool opt_truncate = false;
enum ach_map opt_map = ACH_MAP_DEFAULT;
size_t opt_msg_size = ACH_DEFAULT_FRAME_SIZE;
char *opt_chan_name = NULL;
const char *opt_domain = "local";
int opt_verbosity = 0;
bool opt_1 = false;
int opt_mode = -1;
int opt_port = 8076;
bool opt_advertise = false;
int (*opt_command)(void) = NULL;



/* TODO: be smarter about this path */
#define ADV_PATH  "/etc/avahi/services/"
#define ADV_PREFIX "ach-"
#define ADV_SUFFIX ".service"
#define ADV_SIZE ( ACH_CHAN_NAME_MAX + sizeof(ADV_PATH) + sizeof(ADV_PREFIX) + sizeof(ADV_SUFFIX) + 1 )
#define ADV_FMT ADV_PATH ADV_PREFIX "%s" ADV_SUFFIX


static void check_status(ach_status_t r, const char fmt[], ...);
static void parse_cmd( int (*cmd_fun)(void), char *arg );
static void set_cmd( int (*cmd_fun)(void) );

static int parse_mode( const char *arg ) {
    errno = 0;
    long i = strtol( arg, NULL, 8 );
    if( errno ) {
        fprintf( stderr, "Invalid mode %s: (%d) %s\n",
                 arg, errno, strerror(errno) );
        exit(EXIT_FAILURE);
    }
    return (int) i;
}

/* Commands */
int cmd_file(void);
int cmd_dump(void);
int cmd_unlink(void);
int cmd_create(void);
int cmd_chmod(void);
int cmd_search(void);
int cmd_adv(void);
int cmd_hide(void);

void cleanup() {
    if(opt_chan_name) free(opt_chan_name);
    opt_chan_name = NULL;
}

static void posarg(int i, const char *arg) {
    (void)i;
    switch(i) {
    case 0:
        if( opt_verbosity ) {
            fprintf( stderr, "Setting command: %s\n", arg );
        }
        if( 0 == strcasecmp(arg, "chmod") ) {
            set_cmd( cmd_chmod );
        } else if( 0 == strcasecmp(arg, "create")   ||
                   0 == strcasecmp(arg, "mk")       ||
                   0 == strcasecmp(arg, "make") )
        {
            set_cmd( cmd_create );
        } else if( 0 == strcasecmp(arg, "unlink")   ||
                   0 == strcasecmp(arg, "rm")       ||
                   0 == strcasecmp(arg, "remove") )
        {
            set_cmd( cmd_unlink );
        } else if( 0 == strcasecmp(arg, "dump") ) {
            set_cmd( cmd_dump );
        } else if( 0 == strcasecmp(arg, "file") ) {
            set_cmd( cmd_file );
        } else if( 0 == strcasecmp(arg, "search") ) {
            set_cmd( cmd_search );
        } else if( 0 == strcasecmp(arg, "adv") ||
                   0 == strcasecmp(arg, "advertise") ) {
            set_cmd( cmd_adv );
        } else if( 0 == strcasecmp(arg, "hide") ||
                   0 == strcasecmp(arg, "unadv") ||
                   0 == strcasecmp(arg, "unadvertise")  ) {
            set_cmd( cmd_hide );
        } else {
            goto INVALID;
        }
        break;
    case 1:
        if( cmd_chmod == opt_command ) {
            opt_mode = parse_mode( arg );
        } else {
            opt_chan_name = strdup( arg );
        }
        break;
    case 2:
        if( cmd_chmod == opt_command ) {
            opt_chan_name = strdup( arg );
        } else if (cmd_search == opt_command ) {
            opt_domain = strdup(arg);
        } else {
            goto INVALID;
        }
        break;
    default:
    INVALID:
        fprintf( stderr, "Invalid argument: %s\n", arg );
        exit(EXIT_FAILURE);
    }
}

static void set_cmd( int (*cmd_fun)(void) ) {
    if( NULL == opt_command ) {
        opt_command = cmd_fun;
    }else {
        fprintf(stderr, "Can only specify one command\n");
        cleanup();
        exit(EXIT_FAILURE);
    }
}

static void parse_cmd( int (*cmd_fun)(void), char *arg ) {
    set_cmd( cmd_fun );
    opt_chan_name = strdup( arg );
}


static void deprecate(int opt, const char *cmd )
{
    fprintf(stderr, "Warning: Option flag '-%c' is deprecated.  Please use command '%s' instead.\n",
            opt, cmd );
}

int main( int argc, char **argv ) {
    /* Parse Options */
    int c, i = 0;
    opterr = 0;
    while( (c = getopt( argc, argv, "C:U:D:F:vn:m:o:1p:takuhH?V")) != -1 ) {
        switch(c) {
        case 'C':   /* create   */
            deprecate(c, "mk");
            parse_cmd( cmd_create, optarg );
            break;
        case 'U':   /* unlink   */
            deprecate(c, "rm");
            parse_cmd( cmd_unlink, optarg );
            break;
        case 'D':   /* dump     */
            deprecate(c, "dump");
            parse_cmd( cmd_dump, optarg );
            break;
        case 'F':   /* file     */
            deprecate(c, "file");
            parse_cmd( cmd_file, optarg );
            break;
        case 'n':   /* msg-size */
            opt_msg_size = (size_t)atoi( optarg );
            break;
        case 'm':   /* msg-cnt  */
            opt_msg_cnt = (size_t)atoi( optarg );
            break;
        case 'o':   /* mode     */
            opt_mode = parse_mode( optarg );
            break;
        case 't':   /* truncate */
            opt_truncate = true;
            break;
        case 'a':   /* advertise */
            opt_advertise = true;
            break;
        case 'k':   /* kernel */
            opt_map = ACH_MAP_KERNEL;
            break;
        case 'u':   /* user */
            opt_map = ACH_MAP_USER;
            break;
        case 'v':   /* verbose  */
            opt_verbosity++;
            break;
        case '1':   /* once     */
            opt_1 = true;
            break;
        case 'p':   /* port     */
            opt_port = atoi(optarg);
            if( 0 == opt_port ) {
                fprintf(stderr, "Invalid port: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            opt_advertise = true;
            break;
        case 'V':   /* version     */
            ach_print_version("ach");
            exit(EXIT_SUCCESS);
        case '?':   /* help     */
        case 'h':
        case 'H':
            puts( "Usage: ach [OPTION...] [mk|rm|chmod|dump|file|adv|hide] [mode] [channel-name]\n"
                  "General tool to interact with ach channels\n"
                  "\n"
                  "Options:\n"
                  /* "  -C CHANNEL-NAME,          Create a new channel\n" */
                  /* "  -U CHANNEL-NAME,          Unlink (delete) a channel\n" */
                  /* "  -D CHANNEL-NAME,          Dump info about channel\n" */
                  /* "  -F CHANNEL-NAME,          Print filename for channel (Linux-only)\n" */
                  "  -m MSG-COUNT,             Number of messages to buffer\n"
                  "  -n MSG-SIZE,              Nominal size of a message\n"
                  "  -o OCTAL,                 Mode for created channel\n"
                  "  -t,                       Truncate and reinit newly create channel.\n"
                  "  -1,                       With 'mk', accept an already created channel\n"
                  "                            WARNING: this will clobber processes\n"
                  "                            Currently using the channel.\n"
                  "  -k,                       Create kernel-mapped channel\n"
                  "  -u,                       Create user-mapped channel\n"
                  "  -a,                       advertise create channel\n"
                  "  -p port,                  port number to use\n"
                  "  -v,                       Make output more verbose\n"
                  "  -?,                       Give program help list\n"
                  "  -V,                       Print program version\n"
                  "\n"
                  "Examples:\n"
                  "  ach mk foo                Create channel 'foo' with default buffer sizes.\n"
                  "  ach mk foo -m 10 -n 256   Create channel 'foo' which can buffer up to 10\n"
                  "                            messages of nominal size 256 bytes.\n"
                  "                            Bigger/smaller messages are OK, up to 10*256\n"
                  "                            bytes max (only one message of that size can be\n"
                  "                            buffered at a time).\n"
                  "  ach mk -1 foo             Create channel 'foo' unless it already exists,\n"
                  "                            in which case leave it alone.\n"
                  "  ach mk foo -o 600         Create channel 'foo' with octal permissions\n"
                  "                            '600'. Note that r/w (6) permission necessary\n"
                  "                            for channel access in order to properly\n"
                  "                            synchronize.\n"
                  "  ach mk -a foo             Create channel 'foo' and advertise via mDNS\n"
                  "  ach rm foo                Remove channel 'foo' (also removes mDNS entry)\n"
                  "  ach chmod 666 foo         Set permissions of channel 'foo' to '666'\n"
                  "  ach search foo            Search mDNS for host and port of channel 'foo'\n"
                  "  ach adv foo               Start advertising channel 'foo' via mDNS\n"
                  "  ach adv -p 12345 foo      Start advertising channel 'foo' at port 12345 via mDNS\n"
                  "  ach hide foo              Stop advertising channel 'foo' via mDNS\n"
                  "\n"
                  "Report bugs to " PACKAGE_BUGREPORT "\n"
                );
            exit(EXIT_SUCCESS);
        default:
            posarg(i++, optarg);
        }
    }
    while( optind < argc ) {
        posarg(i++, argv[optind++]);
    }

    /* Be Verbose */
    if( opt_verbosity >= 2 ) {
        fprintf(stderr, "Verbosity:    %d\n", opt_verbosity);
        fprintf(stderr, "Channel Name: %s\n", opt_chan_name);
        fprintf(stderr, "Message Size: %"PRIuPTR"\n", opt_msg_size);
        fprintf(stderr, "Message Cnt:  %"PRIuPTR"\n", opt_msg_cnt);
    }
    /* Do Something */
    int r;
    errno = 0;
    if( opt_command ) {
        r = opt_command();
    } else if ( opt_chan_name && opt_mode >= 0 ) {
        r = cmd_chmod();
    } else{
        fprintf(stderr, "Must specify a command. Say `ach -H' for help.\n");
        r = EXIT_FAILURE;
    }
    cleanup();
    return r;
}

int cmd_create(void) {
    if( opt_verbosity > 0 ) {
        fprintf(stderr, "Creating Channel %s\n", opt_chan_name);
    }
    if( opt_msg_cnt < 1 ) {
        fprintf(stderr, "Message count must be greater than zero, not %"PRIuPTR".\n", opt_msg_cnt);
        return -1;
    }
    if( opt_msg_size < 1 ) {
        fprintf(stderr, "Message size must be greater than zero, not %"PRIuPTR".\n", opt_msg_size);
        return -1;
    }
    ach_status_t i = ACH_OK;
    {
        ach_create_attr_t attr;
        ach_create_attr_init(&attr);
        check_status( ach_create_attr_set_truncate( &attr, opt_truncate ),
                      "set truncate attribute" );
        check_status(ach_create_attr_set_map( &attr, opt_map ),
                     "set map attribute" );
        i = ach_create( opt_chan_name, opt_msg_cnt, opt_msg_size, &attr );
    }

    if( opt_1 && i == ACH_EEXIST ) {
        i = ACH_OK;
    } else {
        check_status( i, "Error creating channel '%s'", opt_chan_name );
    }

    if( opt_mode > 0 ) {
        i = (enum ach_status)cmd_chmod();
        if( ACH_OK != i ) return i;
    }

    if( opt_advertise ) {
        i = (enum ach_status)cmd_adv();
    }

    return i;
}
int cmd_unlink(void) {
    if( opt_verbosity > 0 ) {
        fprintf(stderr, "Unlinking Channel %s\n", opt_chan_name);
    }

    /* Unadvertise */
    char buf[ACH_CHAN_NAME_MAX + 32];
    int i;
    i = snprintf(buf, sizeof(buf), ADV_FMT, opt_chan_name);
    if( i >= (int)sizeof(buf) ) {
        fprintf(stderr, "Channel name too long\n");
        exit( EXIT_FAILURE );
    }
    if( 0 == access(buf, F_OK) ) {
        if( unlink(buf) ) {
            fprintf(stderr, "Could not unlink '%s': %s\n",
                    opt_chan_name, strerror(errno));
        }
    }

    /* Unlink */
    enum ach_status r = ach_unlink(opt_chan_name);

    check_status( r, "Failed to remove channel '%s'", opt_chan_name );

    return 0;
}

int cmd_dump(void) {
    if( opt_verbosity > 0 ) {
        fprintf(stderr, "Dumping Channel %s\n", opt_chan_name);
    }
    ach_channel_t chan;
    ach_status_t r = ach_open( &chan, opt_chan_name, NULL );
    check_status( r, "Error opening ach channel '%s'", opt_chan_name );

    ach_dump( chan.shm );

    r = ach_close( &chan );
    check_status( r, "Error closing ach channel '%s'", opt_chan_name );

    return r;
}


int cmd_file(void) {
    if( opt_verbosity > 0 ) {
        fprintf(stderr, "Printing file for %s\n", opt_chan_name);
    }
    printf(ACH_SHM_CHAN_NAME_PREFIX_PATH ACH_SHM_CHAN_NAME_PREFIX_NAME  "%s\n", opt_chan_name );
    return 0;
}

int cmd_chmod(void) {
    assert(opt_mode >=0 );
    if( opt_verbosity > 0 ) {
        fprintf( stderr, "Changing mode of %s to %o\n",
                 opt_chan_name, (unsigned)opt_mode );
    }

    /* open */
    ach_channel_t chan;
    errno = 0;
    ach_status_t r = ach_open( &chan, opt_chan_name, NULL );
    check_status( r, "Error opening channel '%s'", opt_chan_name );

    /* chmod */
    r = ach_chmod( &chan, (mode_t)opt_mode );
    check_status( r, "Error chmodding channel '%s'", opt_chan_name );

    /* close */
    r = ach_close( &chan );
    check_status( r, "Error closing channel '%s'", opt_chan_name );

    return (ACH_OK==r) ? 0 : -1;
}

int cmd_search(void)
{
    if( NULL == opt_chan_name ) {
        fprintf(stderr, "Must specify channel\n");
        exit(EXIT_FAILURE);
    }
    if( NULL == opt_domain ) {
        fprintf(stderr, "Must specify domain\n");
        exit(EXIT_FAILURE);
    }

    char host[512];
    int port;
    enum ach_status r = ach_srv_search( opt_chan_name, opt_domain, host, 512, &port );
    if( ACH_OK == r ) {
        printf("channel: %s\n"
               "host: %s\n"
               "port: %d\n"
               ".\n",
               opt_chan_name, host, port );
    } else {
        fprintf( stderr, "Could not lookup channel `%s' %s, %s\n",
                 opt_chan_name, ach_result_to_string(r), ach_errstr() );
    }
    return 0;
}

static void check_status(ach_status_t r, const char fmt[], ...) {
    if( ACH_OK != r ) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end( ap );

        if( errno ) {
            fprintf( stderr, ": %s, errno (%d) %s\n",
                     ach_result_to_string(r), errno, strerror(errno) );
        } else {
            fprintf( stderr, ": %s\n", ach_result_to_string(r) );
        }
        exit( EXIT_FAILURE );
    }
}

int cmd_adv(void)
{
    char buf[ADV_SIZE];
    int i;
    FILE *fp;
    i = snprintf(buf, sizeof(buf), ADV_FMT, opt_chan_name);
    if( i >= (int)sizeof(buf) ) {
        fprintf(stderr, "Channel name too long\n");
        exit( EXIT_FAILURE );
    }

    if( opt_verbosity ) {
        printf("Creating file '%s'\n", buf);
    }

    fp = fopen( buf, "w");
    if( !fp ) {
        fprintf(stderr, "Could not open service file '%s': %s\n",
                buf, strerror(errno));
        exit(EXIT_FAILURE);
    }
    /* TODO: validate channel name */
    i = fprintf(fp,
                "<?xml version='1.0' standalone='no'?>\n"
                "<!DOCTYPE service-group SYSTEM 'avahi-service.dtd'>\n"
                "\n"
                "<!-- This file generated by `ach adv' -->\n"
                "\n"
                "<service-group>\n"
                "  <name>%s</name>\n"
                "  <service>\n"
                "    <type>_ach._tcp</type>\n"
                "    <port>%d</port>\n"
                "  </service>\n"
                "</service-group>\n",
                opt_chan_name, opt_port );
    if( ferror(fp) ) {
        fprintf(stderr, "Error writing service file '%s'\n", buf);
        exit(EXIT_FAILURE);
    }

    if( fclose(fp) ) {
        fprintf(stderr, "Error closing service file '%s': %s\n",
                buf, strerror(errno));
        exit(EXIT_FAILURE);
    }

    return 0;
}
int cmd_hide(void)
{
    char buf[ACH_CHAN_NAME_MAX + 32];
    int i;
    i = snprintf(buf, sizeof(buf), ADV_FMT, opt_chan_name);
    if( i >= (int)sizeof(buf) ) {
        fprintf(stderr, "Channel name too long\n");
        exit( EXIT_FAILURE );
    }

    if( unlink(buf) ) {
        fprintf(stderr, "Could not unlink '%s': %s\n",
                opt_chan_name, strerror(errno));
    }

    return 0;
}
