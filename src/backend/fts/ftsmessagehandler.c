/*-------------------------------------------------------------------------
 *
 * ftsmessagehandler.c
 *	  Implementation of handling of FTS messages
 *
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 *
 *
 * IDENTIFICATION
 *	    src/backend/fts/ftsmessagehandler.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "libpq/pqformat.h"
#include "libpq/libpq.h"
#include "postmaster/fts.h"
#include "utils/guc.h"
#include "replication/gp_replication.h"

static void
SendFtsResponse(FtsResponse *response, const char *messagetype)
{
	StringInfoData buf;

	initStringInfo(&buf);

	BeginCommand(messagetype, DestRemote);

	pq_beginmessage(&buf, 'T');
	pq_sendint(&buf, Natts_fts_message_response, 2); /* 3 fields */

	pq_sendstring(&buf, "is_mirror_up");
	pq_sendint(&buf, 0, 4);		/* table oid */
	pq_sendint(&buf, Anum_fts_message_response_is_mirror_up, 2);		/* attnum */
	pq_sendint(&buf, BOOLOID, 4);		/* type oid */
	pq_sendint(&buf, 1, 2);	/* typlen */
	pq_sendint(&buf, -1, 4);		/* typmod */
	pq_sendint(&buf, 0, 2);		/* format code */

	pq_sendstring(&buf, "is_in_sync");
	pq_sendint(&buf, 0, 4);		/* table oid */
	pq_sendint(&buf, Anum_fts_message_response_is_in_sync, 2);		/* attnum */
	pq_sendint(&buf, BOOLOID, 4);		/* type oid */
	pq_sendint(&buf, 1, 2);	/* typlen */
	pq_sendint(&buf, -1, 4);		/* typmod */
	pq_sendint(&buf, 0, 2);		/* format code */

	pq_sendstring(&buf, "is_syncrep_enabled");
	pq_sendint(&buf, 0, 4);		/* table oid */
	pq_sendint(&buf, Anum_fts_message_response_is_syncrep_enabled, 2);		/* attnum */
	pq_sendint(&buf, BOOLOID, 4);		/* type oid */
	pq_sendint(&buf, 1, 2);	/* typlen */
	pq_sendint(&buf, -1, 4);		/* typmod */
	pq_sendint(&buf, 0, 2);		/* format code */

	pq_endmessage(&buf);

	/* Send a DataRow message */
	pq_beginmessage(&buf, 'D');
	pq_sendint(&buf, Natts_fts_message_response, 2);		/* # of columns */

	pq_sendint(&buf, 1, 4); /* col1 len */
	pq_sendint(&buf, response->IsMirrorUp, 1);

	pq_sendint(&buf, 1, 4); /* col2 len */
	pq_sendint(&buf, response->IsInSync, 1);

	pq_sendint(&buf, 1, 4); /* col3 len */
	pq_sendint(&buf, response->IsSyncRepEnabled, 1);

	pq_endmessage(&buf);
	EndCommand(messagetype, DestRemote);
	pq_flush();
}

static void
HandleFtsWalRepProbe(void)
{
	FtsResponse response;

	GetMirrorStatus(&response);

	/*
	 * We check response.IsSyncRepEnabled even though syncrep is again checked
	 * later in the set function to avoid acquiring the SyncRepLock again.
	 */
	if (response.IsMirrorUp && !response.IsSyncRepEnabled)
	{
		SetSyncStandbysDefined();
		/* Syncrep is enabled now, so respond accordingly. */
		response.IsSyncRepEnabled = true;
	}

	SendFtsResponse(&response, FTS_MSG_PROBE);
}

static void
HandleFtsWalRepSyncRepOff(void)
{
	FtsResponse response;

	ereport(LOG,
			(errmsg("turning off synchronous wal replication due to FTS request")));
	UnsetSyncStandbysDefined();
	GetMirrorStatus(&response);

	SendFtsResponse(&response, FTS_MSG_SYNCREP_OFF);
}

void
HandleFtsMessage(const char* query_string)
{
	if (strncmp(query_string, FTS_MSG_PROBE,
				strlen(FTS_MSG_PROBE)) == 0)
		HandleFtsWalRepProbe();
	else if (strncmp(query_string, FTS_MSG_SYNCREP_OFF,
					 strlen(FTS_MSG_SYNCREP_OFF)) == 0)
		HandleFtsWalRepSyncRepOff();
	else
		ereport(ERROR,
				(errmsg("received unknown FTS query: %s", query_string)));
}
