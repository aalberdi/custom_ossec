/* Copyright (C) 2010 Trend Micro Inc.
 * All right reserved.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */

/* SCHED_BATCH is Linux specific and is only picked up with _GNU_SOURCE */
#ifdef __linux__
#define _GNU_SOURCE
#include <sched.h>
#endif

#include "shared.h"
#include "syscheck.h"
#include "os_crypto/md5/md5_op.h"
#include "os_crypto/sha1/sha1_op.h"
#include "os_crypto/md5_sha1/md5_sha1_op.h"
#include "rootcheck/rootcheck.h"

/* Prototypes */
static void send_sk_db(void);


/* Send a message related to syscheck change/addition */
int send_syscheck_msg(const char *msg)
{
    if (SendMSG(syscheck.queue, msg, SYSCHECK, SYSCHECK_MQ) < 0) {
        merror(QUEUE_SEND, ARGV0);

        if ((syscheck.queue = StartMQ(DEFAULTQPATH, WRITE)) < 0) {
            ErrorExit(QUEUE_FATAL, ARGV0, DEFAULTQPATH);
        }

        /* Try to send it again */
        SendMSG(syscheck.queue, msg, SYSCHECK, SYSCHECK_MQ);
    }
    return (0);
}

/* Send a syscheck file deletion message.
 */
int send_syscheck_deletion_msg(const char *file_name)
{
	char alert_msg[PATH_MAX + 4];
	snprintf(alert_msg, PATH_MAX + 4, "-1 %s", file_name);

	return (send_syscheck_msg(alert_msg));
}

/* Send a message related to rootcheck change/addition */
int send_rootcheck_msg(const char *msg)
{
    if (SendMSG(syscheck.queue, msg, ROOTCHECK, ROOTCHECK_MQ) < 0) {
        merror(QUEUE_SEND, ARGV0);

        if ((syscheck.queue = StartMQ(DEFAULTQPATH, WRITE)) < 0) {
            ErrorExit(QUEUE_FATAL, ARGV0, DEFAULTQPATH);
        }

        /* Try to send it again */
        SendMSG(syscheck.queue, msg, ROOTCHECK, ROOTCHECK_MQ);
    }
    return (0);
}

/* Mark a record as unscanned
*/
int mark_as_unscanned(dbrecord *record)
{
	record->scanned = 0;
	return (0);
}

/* Check if db record was deleted
 */
int check_if_deleted(char *key, dbrecord *record)
{
	if (record->scanned)
		record->scanned = 0;
	else
	{
		send_syscheck_deletion_msg(key);

		free(key);
		free(record);

		return (-1);
	}

	return (0);
}

/* Send syscheck db to the server */
static void send_sk_db()
{
	/* Send scan start message */
	if (syscheck.dir[0]) {
		merror("%s: INFO: Starting syscheck scan (forwarding database).", ARGV0);
		send_rootcheck_msg("Starting syscheck scan.");	
	} else {
		return;
	}

	create_db();

	/* Send scan ending message */
	sleep(syscheck.tsleep + 10);

	if (syscheck.dir[0]) {
		merror("%s: INFO: Ending syscheck scan (forwarding database).", ARGV0);
		send_rootcheck_msg("Ending syscheck scan.");

		/* Sending database completed message */
		send_syscheck_msg(HC_SK_DB_COMPLETED);
		debug2("%s: DEBUG: Sending database completed message.", ARGV0);

		OSHash_ForEach(syscheck.fp, (OSHash_Function) &mark_as_unscanned);
	}
}

/* Periodically run the integrity checker */
void start_daemon()
{
    int day_scanned = 0;
    int curr_day = 0;
    time_t curr_time = 0;
    time_t prev_time_rk = 0;
    time_t prev_time_sk = 0;
    char curr_hour[12];
    struct tm *p;

#ifdef INOTIFY_ENABLED
    /* To be used by select */
    struct timeval selecttime;
    fd_set rfds;
#endif

    /* SCHED_BATCH forces the kernel to assume this is a cpu intensive
     * process and gives it a lower priority. This keeps ossec-syscheckd
     * from reducing the interactity of an ssh session when checksumming
     * large files. This is available in kernel flavors >= 2.6.16.
     */
#ifdef SCHED_BATCH
    struct sched_param pri;
    int status;

    pri.sched_priority = 0;
    status = sched_setscheduler(0, SCHED_BATCH, &pri);

    debug1("%s: Setting SCHED_BATCH returned: %d", ARGV0, status);
#endif

#ifdef DEBUG
    verbose("%s: Starting daemon ..", ARGV0);
#endif

    /* Some time to settle */
    memset(curr_hour, '\0', 12);
    sleep(syscheck.tsleep * 10);

    /* If the scan time/day is set, reset the
     * syscheck.time/rootcheck.time
     */
    if (syscheck.scan_time || syscheck.scan_day) {
        /* At least once a week */
        syscheck.time = 604800;
        rootcheck.time = 604800;
    }

    /* Will create the db to store syscheck data */
    if (syscheck.scan_on_start) {
        sleep(syscheck.tsleep * 15);
        send_sk_db();
    } else {
        prev_time_rk = time(0);
    }

    /* Before entering in daemon mode itself */
    prev_time_sk = time(0);
    sleep(syscheck.tsleep * 10);

    /* If the scan_time or scan_day is set, we need to handle the
     * current day/time on the loop.
     */
    if (syscheck.scan_time || syscheck.scan_day) {
        curr_time = time(0);
        p = localtime(&curr_time);

        /* Assign hour/min/sec values */
        snprintf(curr_hour, 9, "%02d:%02d:%02d",
                 p->tm_hour,
                 p->tm_min,
                 p->tm_sec);

        curr_day = p->tm_mday;

        if (syscheck.scan_time && syscheck.scan_day) {
            if ((OS_IsAfterTime(curr_hour, syscheck.scan_time)) &&
                    (OS_IsonDay(p->tm_wday, syscheck.scan_day))) {
                day_scanned = 1;
            }
        } else if (syscheck.scan_time) {
            if (OS_IsAfterTime(curr_hour, syscheck.scan_time)) {
                day_scanned = 1;
            }
        } else if (syscheck.scan_day) {
            if (OS_IsonDay(p->tm_wday, syscheck.scan_day)) {
                day_scanned = 1;
            }
        }
    }

    /* Check every SYSCHECK_WAIT */
    while (1) {
        int run_now = 0;
        curr_time = time(0);

        /* Check if syscheck should be restarted */
        run_now = os_check_restart_syscheck();

        /* Check if a day_time or scan_time is set */
        if (syscheck.scan_time || syscheck.scan_day) {
            p = localtime(&curr_time);

            /* Day changed */
            if (curr_day != p->tm_mday) {
                day_scanned = 0;
                curr_day = p->tm_mday;
            }

            /* Check for the time of the scan */
            if (!day_scanned && syscheck.scan_time && syscheck.scan_day) {
                /* Assign hour/min/sec values */
                snprintf(curr_hour, 9, "%02d:%02d:%02d",
                         p->tm_hour, p->tm_min, p->tm_sec);

                if ((OS_IsAfterTime(curr_hour, syscheck.scan_time)) &&
                        (OS_IsonDay(p->tm_wday, syscheck.scan_day))) {
                    day_scanned = 1;
                    run_now = 1;
                }
            } else if (!day_scanned && syscheck.scan_time) {
                /* Assign hour/min/sec values */
                snprintf(curr_hour, 9, "%02d:%02d:%02d",
                         p->tm_hour, p->tm_min, p->tm_sec);

                if (OS_IsAfterTime(curr_hour, syscheck.scan_time)) {
                    run_now = 1;
                    day_scanned = 1;
                }
            } else if (!day_scanned && syscheck.scan_day) {
                /* Check for the day of the scan */
                if (OS_IsonDay(p->tm_wday, syscheck.scan_day)) {
                    run_now = 1;
                    day_scanned = 1;
                }
            }
        }

        /* If time elapsed is higher than the rootcheck_time, run it */
        if (syscheck.rootcheck) {
            if (((curr_time - prev_time_rk) > rootcheck.time) || run_now) {
                run_rk_check();
                prev_time_rk = time(0);
            }
        }

        /* If time elapsed is higher than the syscheck time, run syscheck time */
        if (((curr_time - prev_time_sk) > syscheck.time) || run_now) {
            if (syscheck.scan_on_start == 0) {
                /* Need to create the db if scan on start is not set */
                sleep(syscheck.tsleep * 10);
                send_sk_db();
                sleep(syscheck.tsleep * 10);

                syscheck.scan_on_start = 1;
            } else {
                /* Send scan start message */
                if (syscheck.dir[0]) {
                    merror("%s: INFO: Starting syscheck scan.", ARGV0);
                    send_rootcheck_msg("Starting syscheck scan.");
                }
#ifdef WIN32
                /* Check for registry changes on Windows */
                os_winreg_check();
#endif
                /* Check for changes */
                run_dbcheck();
		/* Send scan ending message */
		sleep(syscheck.tsleep + 20);
		if (syscheck.dir[0]) {
			merror("%s: INFO: Ending syscheck scan.", ARGV0);
			send_rootcheck_msg("Ending syscheck scan.");
			OSHash_ForEach(syscheck.fp, (OSHash_Function) &check_if_deleted);
		}
            }

            prev_time_sk = time(0);
        }

#ifdef INOTIFY_ENABLED
        if (syscheck.realtime && (syscheck.realtime->fd >= 0)) {
            selecttime.tv_sec = SYSCHECK_WAIT;
            selecttime.tv_usec = 0;

            /* zero-out the fd_set */
            FD_ZERO (&rfds);
            FD_SET(syscheck.realtime->fd, &rfds);

            run_now = select(syscheck.realtime->fd + 1, &rfds,
                             NULL, NULL, &selecttime);
            if (run_now < 0) {
                merror("%s: ERROR: Select failed (for realtime fim).", ARGV0);
                sleep(SYSCHECK_WAIT);
            } else if (run_now == 0) {
                /* Timeout */
            } else if (FD_ISSET (syscheck.realtime->fd, &rfds)) {
                realtime_process();
            }
        } else {
            sleep(SYSCHECK_WAIT);
        }
#elif defined(WIN32)
        if (syscheck.realtime && (syscheck.realtime->fd >= 0)) {
            if (WaitForSingleObjectEx(syscheck.realtime->evt, SYSCHECK_WAIT * 1000, TRUE) == WAIT_FAILED) {
                merror("%s: ERROR: WaitForSingleObjectEx failed (for realtime fim).", ARGV0);
                sleep(SYSCHECK_WAIT);
            } else {
                sleep(1);
            }
        } else {
            sleep(SYSCHECK_WAIT);
        }
#else
        sleep(SYSCHECK_WAIT);
#endif
    }
}

/* Read file information and return a pointer to the checksum */
int c_read_file(const char *file_name, const char *oldsum, char *newsum)
{
	struct stat statbuf;
	os_md5 mf_sum;
	os_sha1 sf_sum;
	int md5sum = (oldsum[4] == '+');
	int sha1sum = (oldsum[5] == '+') || (oldsum[5] == 's');

	/* Clean sums */
	strncpy(mf_sum, "xxx", 4);
	strncpy(sf_sum, "xxx", 4);

	/* Stat the file */
	if (stat(file_name, &statbuf) < 0)
	{
		send_syscheck_deletion_msg(file_name);
		return (-1);
	}


	/* Generate new checksum */
	if (S_ISREG(statbuf.st_mode))
	{
		if (sha1sum && md5sum)
			OS_MD5_SHA1_File((char *)file_name, syscheck.prefilter_cmd, mf_sum, sf_sum);
		else if (sha1sum)
			OS_SHA1_File_Prefilter((char *)file_name, syscheck.prefilter_cmd, sf_sum);
		else if (md5sum)
			OS_MD5_File_Prefilter((char *)file_name, syscheck.prefilter_cmd, mf_sum);
	}
#ifndef WIN32
	/* If it is a link, check if the actual file is valid */
	else if (S_ISDIR(statbuf.st_mode)) {
		strncpy(mf_sum, "ddd", 4);
		strncpy(sf_sum, "ddd", 4);
	}
#endif

	snprintf(newsum, 256, "%ld:%d:%d:%d:%s:%s",
		oldsum[0] == '+' ? (long)statbuf.st_size : 0,
		oldsum[1] == '+' ? (int)statbuf.st_mode : 0,
		oldsum[2] == '+' ? (int)statbuf.st_uid : 0,
		oldsum[3] == '+' ? (int)statbuf.st_gid : 0,
		mf_sum,
		sf_sum);

	return (0);
}

