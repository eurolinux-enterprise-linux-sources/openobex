/**
	\file obex_object.c
	OBEX object related functions.
	OpenOBEX library - Free implementation of the Object Exchange protocol.

	Copyright (c) 1999, 2000 Pontus Fuchs, All Rights Reserved.
	Copyright (c) 1999, 2000 Dag Brattli, All Rights Reserved.

	OpenOBEX is free software; you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as
	published by the Free Software Foundation; either version 2.1 of
	the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with OpenOBEX. If not, see <http://www.gnu.org/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "obex_main.h"
#include "obex_object.h"
#include "obex_header.h"
#include "obex_connect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Function obex_object_new ()
 *
 *    Create a new OBEX object
 *
 */
obex_object_t *obex_object_new(void)
{
	obex_object_t *object;

	object =  malloc(sizeof(obex_object_t));
	if (object == NULL)
		return(NULL);

	memset(object, 0, sizeof(obex_object_t));

	obex_object_setrsp(object, OBEX_RSP_NOT_IMPLEMENTED, OBEX_RSP_NOT_IMPLEMENTED);

	return object;
}

/*
 * Function free_headerq(q)
 *
 *    Free all headers in a header queue.
 *
 */
static void free_headerq(slist_t **q)
{
	struct obex_header_element *h;

	DEBUG(4, "\n");
	while (*q != NULL) {
		h = (*q)->data;
		*q = slist_remove(*q, h);
		buf_free(h->buf);
		free(h);
	}
}

/*
 * Function obex_object_delete (object)
 *
 *    Delete OBEX object
 *
 */
int obex_object_delete(obex_object_t *object)
{
	DEBUG(4, "\n");
	obex_return_val_if_fail(object != NULL, -1);

	/* Free the headerqueues */
	free_headerq(&object->tx_headerq);
	free_headerq(&object->rx_headerq);
	free_headerq(&object->rx_headerq_rm);

	/* Free tx and rx msgs */
	buf_free(object->tx_nonhdr_data);
	object->tx_nonhdr_data = NULL;

	buf_free(object->rx_nonhdr_data);
	object->rx_nonhdr_data = NULL;

	buf_free(object->rx_body);
	object->rx_body = NULL;

	free(object);

	return 0;
}

/*
 * Function obex_object_setcmd ()
 *
 *    Set command of object
 *
 */
int obex_object_setcmd(obex_object_t *object, uint8_t cmd, uint8_t lastcmd)
{
	DEBUG(4,"%02x\n", cmd);
	object->cmd = cmd;
	object->opcode = cmd;
	object->lastopcode = lastcmd;
	return 1;
}

/*
 * Function obex_object_setrsp ()
 *
 *    Set the response for an object
 *
 */
int obex_object_setrsp(obex_object_t *object, uint8_t rsp, uint8_t lastrsp)
{
	DEBUG(4,"\n");
	object->opcode = rsp;
	object->lastopcode = lastrsp;
	return 1;
}

int obex_object_getspace(obex_t *self, obex_object_t *object, unsigned int flags)
{
	if (flags & OBEX_FL_FIT_ONE_PACKET)
		return self->mtu_tx - object->totallen - sizeof(struct obex_common_hdr);
	else
		return self->mtu_tx - sizeof(struct obex_common_hdr);
}

/*
 * Function int obex_object_addheader(obex_object_t *object, uint8_t hi,
 *                      obex_headerdata_t hv, uint32_t hv_size, unsigned int flags)
 *
 *    Add a header to the TX-queue.
 *
 */
int obex_object_addheader(obex_t *self, obex_object_t *object, uint8_t hi,
	obex_headerdata_t hv, uint32_t hv_size, unsigned int flags)
{
	int ret = -1;
	struct obex_header_element *element;
	unsigned int maxlen;

	DEBUG(4, "\n");
	/* End of stream marker */
	if (flags & OBEX_FL_STREAM_DATAEND) {
		if (self->object == NULL)
			return -1;
		self->object->s_stop = TRUE;
		self->object->s_buf = hv.bs;
		self->object->s_len = hv_size;
		return 1;
	}

	/* Stream data */
	if (flags & OBEX_FL_STREAM_DATA) {
		if (self->object == NULL)
			return -1;
		self->object->s_buf = hv.bs;
		self->object->s_len = hv_size;
		return 1;
	}

	if (flags & OBEX_FL_FIT_ONE_PACKET) {
		/* In this command all headers must fit in one packet! */
		DEBUG(3, "Fit one packet!\n");
		maxlen = self->mtu_tx - object->totallen - sizeof(struct obex_common_hdr);
	} else
		maxlen = self->mtu_tx - sizeof(struct obex_common_hdr);

	element = malloc(sizeof(struct obex_header_element));
	if (element == NULL)
		return -1;

	memset(element, 0, sizeof(struct obex_header_element));

	element->hi = hi;
	element->flags = flags;

	/* Is this a stream? */
	if (flags & OBEX_FL_STREAM_START) {
		DEBUG(3, "Adding stream\n");
		element->stream = TRUE;
		object->tx_headerq = slist_append(object->tx_headerq, element);
		return 1;
	}

	if (hi == OBEX_HDR_EMPTY) {
		DEBUG(2, "Empty header\n");
		object->tx_headerq = slist_append(object->tx_headerq, element);
		return 1;
	}

	switch (hi & OBEX_HI_MASK) {
	case OBEX_INT:
		DEBUG(2, "4BQ header %d\n", hv.bq4);

		element->buf = buf_new(sizeof(struct obex_uint_hdr));
		if (element->buf) {
			element->length = (unsigned int) sizeof(struct obex_uint_hdr);
			ret = insert_uint_header(element->buf, hi, hv.bq4);
		}
		break;

	case OBEX_BYTE:
		DEBUG(2, "1BQ header %d\n", hv.bq1);

		element->buf = buf_new(sizeof(struct obex_ubyte_hdr));
		if (element->buf) {
			element->length = sizeof(struct obex_ubyte_hdr);
			ret = insert_ubyte_header(element->buf, hi, hv.bq1);
		}
		break;

	case OBEX_BYTE_STREAM:
		DEBUG(2, "BS  header size %d\n", hv_size);

		element->buf = buf_new(hv_size + sizeof(struct obex_byte_stream_hdr) );
		if (element->buf) {
			element->length = hv_size + sizeof(struct obex_byte_stream_hdr);
			ret = insert_byte_stream_header(element->buf, hi, hv.bs, hv_size);
		}
		break;

	case OBEX_UNICODE:
		DEBUG(2, "Unicode header size %d\n", hv_size);

		element->buf = buf_new(hv_size + sizeof(struct obex_unicode_hdr));
		if (element->buf) {
			element->length = hv_size + sizeof(struct obex_unicode_hdr);
			ret = insert_unicode_header(element->buf, hi, hv.bs, hv_size);
		}
		break;

	default:
		DEBUG(2, "Unsupported encoding %02x\n", hi & OBEX_HI_MASK);
		ret = -1;
		break;
	}

	/* Check if you can send this header without violating MTU or OBEX_FIT_ONE_PACKET */
	if (element->hi != OBEX_HDR_BODY || (flags & OBEX_FL_FIT_ONE_PACKET)) {
		if (maxlen < element->length) {
			DEBUG(0, "Header to big\n");
			ret = -1;
		}
	}

	if (ret > 0) {
		object->totallen += ret;
		object->tx_headerq = slist_append(object->tx_headerq, element);
		ret = 1;
	} else {
		buf_free(element->buf);
		free(element);
	}

	return ret;
}


/*
 * Function send_stream(object, header, txmsg, tx_left)
 *
 *  Send a streaming header.
 *
 */
static int send_stream(obex_t *self,
				struct obex_header_element *h,
				buf_t *txmsg, unsigned int tx_left)
{
	obex_object_t *object;
	struct obex_byte_stream_hdr *body_txh;
	int actual;	/* Number of bytes sent in this fragment */

	DEBUG(4, "\n");

	object = self->object;

	/* Fill in length and header type later, but reserve space for it */
	body_txh  = (struct obex_byte_stream_hdr*) buf_reserve_end(txmsg,
				sizeof(struct obex_byte_stream_hdr) );
	tx_left -= sizeof(struct obex_byte_stream_hdr);
	actual = sizeof(struct obex_byte_stream_hdr);

	do {
		if (object->s_len == 0) {
			/* Ask app for more data if no more */
			object->s_offset = 0;
			object->s_buf = NULL;
			obex_deliver_event(self, OBEX_EV_STREAMEMPTY, 0, 0, FALSE);
			DEBUG(4, "s_len=%d, s_stop = %d\n",
						object->s_len, object->s_stop);
			/* End of stream ?*/
			if (object->s_stop)
				break;

			/* User suspended and didn't provide any new data */
			if (object->suspend && object->s_buf == NULL)
				break;

			/* Error ?*/
			if (object->s_buf == NULL) {
				DEBUG(1, "Unexpected end-of-stream\n");
				return -1;
			}
		}

		if (tx_left < object->s_len) {
			/* There is more data left in buffer than tx_left */
			DEBUG(4, "More data than tx_left. Buffer will not be empty\n");

			buf_insert_end(txmsg, (uint8_t*) object->s_buf + object->s_offset, tx_left);
			object->s_len -= tx_left;
			object->s_offset += tx_left;
			actual += tx_left;
			tx_left = 0;
		} else {
			/* There less data in buffer than tx_left */
			DEBUG(4, "Less data that tx_left. Buffer will be empty\n");
			buf_insert_end(txmsg, (uint8_t*) object->s_buf + object->s_offset, object->s_len);
			tx_left -= object->s_len;
			object->s_offset += object->s_len;
			actual += object->s_len;
			object->s_len = 0;
			if (object->suspend)
				tx_left = 0;
		}
	} while (tx_left > 0);

	DEBUG(4, "txmsg full or no more stream-data. actual = %d\n", actual);
	body_txh->hi = OBEX_HDR_BODY;

	if (object->s_stop && object->s_len == 0) {
		/* We are done. Remove header from tx-queue */
		object->tx_headerq = slist_remove(object->tx_headerq, h);
		body_txh->hi = OBEX_HDR_BODY_END;
		buf_free(h->buf);
		free(h);
	}

	body_txh->hl = htons((uint16_t)actual);

	return actual;
}


/*
 * Function send_body(object, header, txmsg, tx_left)
 *
 *  Fragment and send the body
 *
 */
static int send_body(obex_object_t *object,
				struct obex_header_element *h,
				buf_t *txmsg, unsigned int tx_left)
{
	struct obex_byte_stream_hdr *body_txh;
	unsigned int actual;

	body_txh = (struct obex_byte_stream_hdr*) buf_reserve_end(txmsg, sizeof(struct obex_byte_stream_hdr));

	if (!h->body_touched) {
		/* This is the first time we try to send this header
		   obex_object_addheaders has added a struct_byte_stream_hdr
		   before the actual body-data. We shall send this in every fragment
		   so we just remove it for now.*/

		buf_remove_begin(h->buf,  sizeof(struct obex_byte_stream_hdr) );
		h->body_touched = TRUE;
	}

	if (tx_left < ( h->buf->data_size +
			sizeof(struct obex_byte_stream_hdr) ) )	{
		DEBUG(4, "Add BODY header\n");
		body_txh->hi = OBEX_HDR_BODY;
		body_txh->hl = htons((uint16_t)tx_left);

		buf_insert_end(txmsg, h->buf->data, tx_left
				- sizeof(struct obex_byte_stream_hdr) );

		buf_remove_begin(h->buf, tx_left
				- sizeof(struct obex_byte_stream_hdr) );
		/* We have completely filled the tx-buffer */
		actual = tx_left;
	} else {
		DEBUG(4, "Add BODY_END header\n");

		body_txh->hi = OBEX_HDR_BODY_END;
		body_txh->hl = htons((uint16_t) (h->buf->data_size + sizeof(struct obex_byte_stream_hdr)));
		buf_insert_end(txmsg, h->buf->data, h->buf->data_size);
		actual = h->buf->data_size;

		object->tx_headerq = slist_remove(object->tx_headerq, h);
		buf_free(h->buf);
		free(h);
	}

	return actual;
}


/*
 * Function obex_object_send()
 *
 *    Send away all headers attached to an object. Returns:
 *       1 on sucessfully done
 *       0 on progress made
 *     < 0 on error
 */
int obex_object_send(obex_t *self, obex_object_t *object,
		      int allowfinalcmd, int forcefinalbit)
{
	struct obex_header_element *h;
	buf_t *txmsg;
	int actual, finished = 0;
	uint16_t tx_left;
	int addmore = TRUE;
	int real_opcode;

	DEBUG(4, "\n");

	/* Don't do anything of object is suspended */
	if (object->suspend)
		return 0;

	/* Calc how many bytes of headers we can fit in this package */
	tx_left = self->mtu_tx - sizeof(struct obex_common_hdr);
	switch (self->trans.type) {
#ifdef HAVE_IRDA
	case OBEX_TRANS_IRDA:
		if (self->trans.mtu > 0 && self->mtu_tx > self->trans.mtu)
			tx_left -= self->mtu_tx%self->trans.mtu;
		break;
#endif /*HAVE_IRDA*/
	default:
		break;
	}

	/* Reuse transmit buffer */
	txmsg = buf_reuse(self->tx_msg);

	/* Add nonheader-data first if any (SETPATH, CONNECT)*/
	if (object->tx_nonhdr_data) {
		DEBUG(4, "Adding %d bytes of non-headerdata\n", object->tx_nonhdr_data->data_size);
		buf_insert_end(txmsg, object->tx_nonhdr_data->data, object->tx_nonhdr_data->data_size);

		buf_free(object->tx_nonhdr_data);
		object->tx_nonhdr_data = NULL;
	}

	DEBUG(4, "4\n");

	/* Take headers from the tx queue and try to stuff as
	   many as possible into the tx-msg */
	while (addmore == TRUE && object->tx_headerq != NULL) {

		h = object->tx_headerq->data;

		if (h->stream) {
			/* This is a streaming body */
			if (h->flags & OBEX_FL_SUSPEND)
				object->suspend = 1;
			actual = send_stream(self, h, txmsg, tx_left);
			if (actual < 0 )
				return -1;
			tx_left -= actual;
		} else if (h->hi == OBEX_HDR_BODY) {
			/* The body may be fragmented over several packets. */
			if (h->flags & OBEX_FL_SUSPEND)
				object->suspend = 1;
			tx_left -= send_body(object, h, txmsg, tx_left);
		} else if(h->hi == OBEX_HDR_EMPTY) {
			if (h->flags & OBEX_FL_SUSPEND)
				object->suspend = 1;
			object->tx_headerq = slist_remove(object->tx_headerq, h);
			free(h);
		} else if (h->length <= tx_left) {
			/* There is room for more data in tx msg */
			DEBUG(4, "Adding non-body header\n");
			buf_insert_end(txmsg, h->buf->data, h->length);
			tx_left -= h->length;
			if (h->flags & OBEX_FL_SUSPEND)
				object->suspend = 1;

			/* Remove from tx-queue */
			object->tx_headerq = slist_remove(object->tx_headerq, h);
			buf_free(h->buf);
			free(h);
		} else if (h->length > self->mtu_tx) {
			/* Header is bigger than MTU. This should not happen,
			   because OBEX_ObjectAddHeader() rejects headers
			   bigger than the MTU */

			DEBUG(0, "ERROR! header to big for MTU\n");
			return -1;
		} else {
			/* This header won't fit. */
			addmore = FALSE;
		}

		if (object->suspend)
			addmore = FALSE;

		if (tx_left == 0)
			addmore = FALSE;
	};

	/* Decide which command to use, and if to use final-bit */
	if (object->tx_headerq) {
		/* Have more headers (or body) to send */
		/* In server, final bit is always set.
		 * In client, final bit is set only when we finish sending.
		 * Jean II */
		if (forcefinalbit)
			real_opcode = object->opcode | OBEX_FINAL;
		else
			real_opcode = object->opcode;
		finished = 0;
	} else if (allowfinalcmd == FALSE) {
		/* Have no yet any headers to send, but not allowed to send
		 * final command (== server, receiving incomming request) */
		real_opcode = object->opcode | OBEX_FINAL;
		finished = 0;
	} else {
		/* Have no more headers to send, and allowed to send final
		 * command (== end data we are sending) */
		real_opcode = object->lastopcode | OBEX_FINAL;
		finished = 1;
	}

	DEBUG(4, "Sending package with opcode %d\n", real_opcode);
	actual = obex_data_request(self, txmsg, real_opcode);

	if (actual < 0) {
		DEBUG(4, "Send error\n");
		return actual;
	} else
		return finished;
}

/*
 * Function obex_object_getnextheader()
 *
 * Return the next header in the rx-queue
 *
 */
int obex_object_getnextheader(obex_t *self, obex_object_t *object, uint8_t *hi,
				obex_headerdata_t *hv, uint32_t *hv_size)
{
	uint32_t *bq4;
	struct obex_header_element *h;

	DEBUG(4, "\n");

	/* No more headers */
	if (object->rx_headerq == NULL)
		return 0;

	/* New headers are appended at the end of the list while receiving, so
	   we pull them from the front.
	   Since we cannot free the mem used just yet just put the header in
	   another list so we can free it when the object is deleted. */

	h = object->rx_headerq->data;
	object->rx_headerq = slist_remove(object->rx_headerq, h);
	object->rx_headerq_rm = slist_append(object->rx_headerq_rm, h);

	*hi = h->hi;
	*hv_size= h->length;

	switch (h->hi & OBEX_HI_MASK) {
		case OBEX_BYTE_STREAM:
			hv->bs = &h->buf->data[0];
			break;

		case OBEX_UNICODE:
			hv->bs = &h->buf->data[0];
			break;

		case OBEX_INT:
			bq4 = (uint32_t*) h->buf->data;
			hv->bq4 = ntohl(*bq4);
			break;

		case OBEX_BYTE:
			hv->bq1 = h->buf->data[0];
			break;
	}

	return 1;
}

/*
 * Function obex_object_reparseheader()
 *
 * Allow the user to re-parse the headers in the rx-queue
 *
 */
int obex_object_reparseheaders(obex_t *self, obex_object_t *object)
{

	DEBUG(4, "\n");

	/* Check that there is no more active headers */
	if (object->rx_headerq != NULL)
		return 0;

	/* Put the old headers back in the active list */
	object->rx_headerq = object->rx_headerq_rm;
	object->rx_headerq_rm = NULL;

	/* Success */
	return 1;
}

/*
 * Function obex_object_receive_stream()
 *
 *    Handle receiving of body-stream
 *
 */
static void obex_object_receive_stream(obex_t *self, uint8_t hi,
				uint8_t *source, unsigned int len)
{
	obex_object_t *object = self->object;

	DEBUG(4, "\n");

	/* Spare the app this empty nonlast body-hdr */
	if (hi == OBEX_HDR_BODY && len == 0)
		return;

	object->s_buf = source;
	object->s_len = len;

	if (object->abort) {
		DEBUG(3, "Ignoring incomming data because request was aborted\n");
		return;
	}

	/* Notify app that data has arrived */
	obex_deliver_event(self, OBEX_EV_STREAMAVAIL, 0, 0, FALSE);

	/* If send send EOS to app */
	if (hi == OBEX_HDR_BODY_END && len != 0) {
		object->s_buf = source;
		object->s_len = 0;
		obex_deliver_event(self, OBEX_EV_STREAMAVAIL, 0, 0, FALSE);
	}
}

/*
 * Function obex_object_receive_body()
 *
 *    Handle receiving of body
 *
 */
static int obex_object_receive_body(obex_object_t *object, buf_t *msg, uint8_t hi,
				uint8_t *source, unsigned int len)
{
	struct obex_header_element *element;

	DEBUG(4, "This is a body-header. Len=%d\n", len);

	if (len > msg->data_size) {
		DEBUG(1, "Header %d to big. HSize=%d Buffer=%d\n",
				hi, len, msg->data_size);
		return -1;
	}

	if (!object->rx_body) {
		int alloclen = OBEX_OBJECT_ALLOCATIONTRESHOLD + len;

		if (object->hinted_body_len)
			alloclen = object->hinted_body_len;

		DEBUG(4, "Allocating new body-buffer. Len=%d\n", alloclen);
		if (!(object->rx_body = buf_new(alloclen)))
			return -1;
	}

	/* Reallocate body buffer if needed */
	if (object->rx_body->data_avail + object->rx_body->tail_avail < (int) len) {
		int t;
		DEBUG(4, "Buffer too small. Go realloc\n");
		t = buf_total_size(object->rx_body);
		buf_resize(object->rx_body, t + OBEX_OBJECT_ALLOCATIONTRESHOLD + len);
		if (buf_total_size(object->rx_body) != t + OBEX_OBJECT_ALLOCATIONTRESHOLD + len) {
			DEBUG(1, "Can't realloc rx_body\n");
			return -1;
			/* FIXME: Handle this in a nice way... */
		}
	}

	buf_insert_end(object->rx_body, source, len);

	if (hi == OBEX_HDR_BODY_END) {
		DEBUG(4, "Body receive done\n");
		if ( (element = malloc(sizeof(struct obex_header_element)) ) ) {
			memset(element, 0, sizeof(struct obex_header_element));
			element->length = object->rx_body->data_size;
			element->hi = OBEX_HDR_BODY;
			element->buf = object->rx_body;

			/* Add element to rx-list */
			object->rx_headerq = slist_append(object->rx_headerq, element);
		} else
			buf_free(object->rx_body);

		object->rx_body = NULL;
	} else
		DEBUG(4, "Normal body fragment...\n");

	return 1;
}

/*
 * Function obex_object_receive()
 *
 *    Add any incoming headers to headerqueue.
 *
 */
int obex_object_receive(obex_t *self, buf_t *msg)
{
	obex_object_t *object;
	struct obex_header_element *element;
	uint8_t *source = NULL;
	unsigned int len, hlen;
	uint8_t hi;
	int err = 0;

	union {
		struct obex_unicode_hdr     *unicode;
		struct obex_byte_stream_hdr *bstream;
		struct obex_uint_hdr	    *uint;
	} h;


	DEBUG(4, "\n");
	object = self->object;

	/* Remove command from buffer */
	buf_remove_begin(msg, sizeof(struct obex_common_hdr));

	/* Copy any non-header data (like in CONNECT and SETPATH) */
	if (object->headeroffset) {
		object->rx_nonhdr_data = buf_new(object->headeroffset);
		if (!object->rx_nonhdr_data)
			return -1;
		buf_insert_end(object->rx_nonhdr_data, msg->data, object->headeroffset);
		DEBUG(4, "Command has %d bytes non-headerdata\n", object->rx_nonhdr_data->data_size);
		buf_remove_begin(msg, object->headeroffset);
		object->headeroffset = 0;
	}

	while ((msg->data_size > 0) && (!err)) {
		hi = msg->data[0];
		DEBUG(4, "Header: %02x\n", hi);
		switch (hi & OBEX_HI_MASK) {

		case OBEX_UNICODE:
			h.unicode = (struct obex_unicode_hdr *) msg->data;
			source = &msg->data[3];
			hlen = ntohs(h.unicode->hl);
			len = hlen - 3;
			break;

		case OBEX_BYTE_STREAM:
			h.bstream = (struct obex_byte_stream_hdr *) msg->data;
			source = &msg->data[3];
			hlen = ntohs(h.bstream->hl);
			len = hlen - 3;

			if (hi == OBEX_HDR_BODY || hi == OBEX_HDR_BODY_END) {
				/* The body-header need special treatment */
				if (object->s_srv)
					obex_object_receive_stream(self, hi, source, len);
				else {
					if (obex_object_receive_body(object, msg, hi, source, len) < 0)
						err = -1;
				}
				/* We have already handled this data! */
				source = NULL;
			}
			break;

		case OBEX_BYTE:
			source = &msg->data[1];
			len = 1;
			hlen = 2;
			break;

		case OBEX_INT:
			source = &msg->data[1];
			len = 4;
			hlen = 5;
			break;
		default:
			DEBUG(1, "Badly formed header received\n");
			source = NULL;
			hlen = 0;
			len = 0;
			err = -1;
			break;
		}

		/* Make sure that the msg is big enough for header */
		if (len > msg->data_size) {
			DEBUG(1, "Header %d to big. HSize=%d Buffer=%d\n",
					hi, len, msg->data_size);
			source = NULL;
			err = -1;
		}

		if (source) {
			/* The length MAY be useful when receiving body. */
			if (hi == OBEX_HDR_LENGTH) {
				h.uint = (struct obex_uint_hdr *) msg->data;
				object->hinted_body_len = ntohl(h.uint->hv);
				DEBUG(4, "Hinted body len is %d\n",
							object->hinted_body_len);
			}

			if ( (element = malloc(sizeof(struct obex_header_element)) ) ) {
				memset(element, 0, sizeof(struct obex_header_element));
				element->length = len;
				element->hi = hi;

				/* If we get an emtpy we have to deal with it...
				 * This might not be an optimal way, but it works. */
				if (len == 0) {
					DEBUG(4, "Got empty header. Allocating dummy buffer anyway\n");
					element->buf = buf_new(1);
				} else {
					element->buf = buf_new(len);
					if (element->buf) {
						DEBUG(4, "Copying %d bytes\n", len);
						buf_insert_end(element->buf, source, len);
					}
				}

				if (element->buf) {
					/* Add element to rx-list */
					object->rx_headerq = slist_append(object->rx_headerq, element);
				} else{
					DEBUG(1, "Cannot allocate memory\n");
					free(element);
					err = -1;
				}
			} else {
				DEBUG(1, "Cannot allocate memory\n");
				err = -1;
			}
		}

		if (err)
			return err;

		DEBUG(4, "Pulling %d bytes\n", hlen);
		buf_remove_begin(msg, hlen);
	}

	return 1;
}

/*
 * Function obex_object_readstream()
 *
 *    App wants to read stream fragment.
 *
 */
int obex_object_readstream(obex_t *self, obex_object_t *object, const uint8_t **buf)
{
	DEBUG(4, "\n");
	/* Enable streaming */
	if (buf == NULL) {
		DEBUG(4, "Streaming is enabled!\n");
		object->s_srv = TRUE;
		return 0;
	}

	DEBUG(4, "s_len = %d\n", object->s_len);
	*buf = object->s_buf;

	return object->s_len;
}

int obex_object_suspend(obex_object_t *object)
{
	object->suspend = 1;
	return 0;
}

int obex_object_resume(obex_t *self, obex_object_t *object)
{
	if (!object->suspend)
		return 0;

	object->suspend = 0;

	if (object->first_packet_sent && !object->continue_received)
		return 0;

	if (obex_object_send(self, object, TRUE, FALSE) < 0) {
		obex_deliver_event(self, OBEX_EV_LINKERR, object->opcode, 0, TRUE);
		return -1;
	}

	obex_deliver_event(self, OBEX_EV_PROGRESS, object->opcode, 0, FALSE);

	self->state = MODE_CLI | STATE_REC;
	object->first_packet_sent = 1;
	object->continue_received = 0;

	return 0;
}
