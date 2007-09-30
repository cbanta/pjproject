/* $Id$ */
/* 
 * Copyright (C) 2003-2007 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pjsip-ua/sip_100rel.h>
#include <pjsip/sip_endpoint.h>
#include <pjsip/sip_event.h>
#include <pjsip/sip_module.h>
#include <pjsip/sip_transaction.h>
#include <pj/assert.h>
#include <pj/ctype.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/rand.h>

#if defined(PJSIP_HAS_100REL) && PJSIP_HAS_100REL!=0

#define THIS_FILE	"sip_100rel.c"

typedef struct dlg_data dlg_data;

/*
 * Static prototypes.
 */
static pj_status_t mod_100rel_load(pjsip_endpoint *endpt);
static void	   mod_100rel_on_tsx_state(pjsip_transaction*, pjsip_event*);

static void handle_incoming_prack(dlg_data *dd, pjsip_transaction *tsx,
				  pjsip_event *e);
static void handle_incoming_response(dlg_data *dd, pjsip_transaction *tsx,
				     pjsip_event *e);
static void on_retransmit(pj_timer_heap_t *timer_heap,
			  struct pj_timer_entry *entry);


/* PRACK method */
const pjsip_method pjsip_prack_method =
{
	PJSIP_OTHER_METHOD,
	{ "PRACK", 5 }
};

const pj_str_t tag_100rel = { "100rel", 6 };
const pj_str_t RSEQ = { "RSeq", 4 };
const pj_str_t RACK = { "RAck", 4 };


/* 100rel module */
static struct mod_100rel
{
    pjsip_module	 mod;
    pjsip_endpoint	*endpt;
} mod_100rel = 
{
    {
	NULL, NULL,			    /* prev, next.		*/
	{ "mod-100rel", 10 },		    /* Name.			*/
	-1,				    /* Id			*/
	PJSIP_MOD_PRIORITY_DIALOG_USAGE,    /* Priority			*/
	&mod_100rel_load,		    /* load()			*/
	NULL,				    /* start()			*/
	NULL,				    /* stop()			*/
	NULL,				    /* unload()			*/
	NULL,				    /* on_rx_request()		*/
	NULL,				    /* on_rx_response()		*/
	NULL,				    /* on_tx_request.		*/
	NULL,				    /* on_tx_response()		*/
	&mod_100rel_on_tsx_state,	    /* on_tsx_state()		*/
    }

};

/* List of pending transmission (may include the final response as well) */
typedef struct tx_data_list
{
	PJ_DECL_LIST_MEMBER(struct tx_data_list);
	pj_uint32_t	 rseq;
	pjsip_tx_data	*tdata;
} tx_data_list;


/* Below, UAS and UAC roles are of the INVITE transaction */

/* UAS state. */
typedef struct uas_state
{
	pj_int32_t	 cseq;
	pj_uint32_t	 rseq;	/* Initialized to -1 */
	pj_bool_t	 has_sdp;
	tx_data_list	 tx_data_list;
	unsigned	 retransmit_count;
	pj_timer_entry	 retransmit_timer;
} uas_state;


/* UAC state */
typedef struct uac_state
{
	pj_int32_t	cseq;
	pj_uint32_t	rseq;	/* Initialized to -1 */
} uac_state;


/* State attached to each dialog. */
struct dlg_data
{
	pjsip_inv_session	*inv;
	uas_state		*uas_state;
	uac_state		*uac_state;
};


/*****************************************************************************
 **
 ** Module
 **
 *****************************************************************************
 */
static pj_status_t mod_100rel_load(pjsip_endpoint *endpt)
{
	mod_100rel.endpt = endpt;
	pjsip_endpt_add_capability(endpt, &mod_100rel.mod, 
				   PJSIP_H_ALLOW, NULL,
				   1, &pjsip_prack_method.name);
	pjsip_endpt_add_capability(endpt, &mod_100rel.mod, 
				   PJSIP_H_SUPPORTED, NULL,
				   1, &tag_100rel);

	return PJ_SUCCESS;
}

static pjsip_require_hdr *find_req_hdr(pjsip_msg *msg)
{
	pjsip_require_hdr *hreq;

	hreq = (pjsip_require_hdr*)
		pjsip_msg_find_hdr(msg, PJSIP_H_REQUIRE, NULL);

	while (hreq) {
		unsigned i;
		for (i=0; i<hreq->count; ++i) {
			if (!pj_stricmp(&hreq->values[i], &tag_100rel)) {
				return hreq;
			}
		}

		if ((void*)hreq->next == (void*)&msg->hdr)
			return NULL;

		hreq = (pjsip_require_hdr*)
			pjsip_msg_find_hdr(msg, PJSIP_H_REQUIRE, hreq->next);

	}

	return NULL;
}

static void mod_100rel_on_tsx_state(pjsip_transaction *tsx, pjsip_event *e)
{
	pjsip_dialog *dlg;
	dlg_data *dd;

	dlg = pjsip_tsx_get_dlg(tsx);
	if (!dlg)
		return;

	dd = (dlg_data*) dlg->mod_data[mod_100rel.mod.id];
	if (!dd)
		return;

	if (tsx->role == PJSIP_ROLE_UAS &&
	    tsx->state == PJSIP_TSX_STATE_TRYING &&
	    pjsip_method_cmp(&tsx->method, &pjsip_prack_method)==0)
	{
		/* 
		 * Handle incoming PRACK request.
		 */
		handle_incoming_prack(dd, tsx, e);

	} else if (tsx->role == PJSIP_ROLE_UAC &&
		   tsx->method.id == PJSIP_INVITE_METHOD &&
		   e->type == PJSIP_EVENT_TSX_STATE &&
		   e->body.tsx_state.type == PJSIP_EVENT_RX_MSG && 
		   e->body.tsx_state.src.rdata->msg_info.msg->line.status.code > 100 &&
		   e->body.tsx_state.src.rdata->msg_info.msg->line.status.code < 200 &&
		   e->body.tsx_state.src.rdata->msg_info.require != NULL)
	{
		/*
		 * Handle incoming provisional response which wants to 
		 * be PRACK-ed
		 */

		if (find_req_hdr(e->body.tsx_state.src.rdata->msg_info.msg)) {
			/* Received provisional response which needs to be 
			 * PRACK-ed.
			 */
			handle_incoming_response(dd, tsx, e);
		}

	} else if (tsx->role == PJSIP_ROLE_UAC &&
		   tsx->state == PJSIP_TSX_STATE_COMPLETED &&
		   pjsip_method_cmp(&tsx->method, &pjsip_prack_method)==0)
	{
		/*
		 * Handle the status of outgoing PRACK request.
		 */
		if (tsx->status_code == PJSIP_SC_CALL_TSX_DOES_NOT_EXIST ||
		    tsx->status_code == PJSIP_SC_REQUEST_TIMEOUT ||
		    tsx->status_code == PJSIP_SC_TSX_TIMEOUT ||
		    tsx->status_code == PJSIP_SC_TSX_TRANSPORT_ERROR)
		{
			/* These are fatal errors which should terminate
			 * the session AND dialog!
			 */
			PJ_TODO(TERMINATE_SESSION_ON_481);
		}

	} else if (tsx == dd->inv->invite_tsx &&
		   tsx->role == PJSIP_ROLE_UAS &&
		   tsx->state == PJSIP_TSX_STATE_TERMINATED)
	{
		/* Make sure we don't have pending transmission */
		if (dd->uas_state) {
			pj_assert(!dd->uas_state->retransmit_timer.id);
			pj_assert(pj_list_empty(&dd->uas_state->tx_data_list));
		}
	}
}

static void parse_rack(const pj_str_t *rack,
		       pj_uint32_t *p_rseq, pj_int32_t *p_seq,
		       pj_str_t *p_method)
{
	const char *p = rack->ptr, *end = p + rack->slen;
	pj_str_t token;

	token.ptr = (char*)p;
	while (p < end && pj_isdigit(*p))
		++p;
	token.slen = p - token.ptr;
	*p_rseq = pj_strtoul(&token);

	++p;
	token.ptr = (char*)p;
	while (p < end && pj_isdigit(*p))
		++p;
	token.slen = p - token.ptr;
	*p_seq = pj_strtoul(&token);

	++p;
	if (p < end) {
		p_method->ptr = (char*)p;
		p_method->slen = end - p;
	} else {
		p_method->ptr = NULL;
		p_method->slen = 0;
	}
}

/* Clear all responses in the transmission list */
static void clear_all_responses(dlg_data *dd)
{
	tx_data_list *tl;

	tl = dd->uas_state->tx_data_list.next;
	while (tl != &dd->uas_state->tx_data_list) {
		pjsip_tx_data_dec_ref(tl->tdata);
		tl = tl->next;
	}
	pj_list_init(&dd->uas_state->tx_data_list);
}


static void handle_incoming_prack(dlg_data *dd, pjsip_transaction *tsx,
				  pjsip_event *e)
{
	pjsip_rx_data *rdata;
	pjsip_msg *msg;
	pjsip_generic_string_hdr *rack_hdr;
	pjsip_tx_data *tdata;
	pj_uint32_t rseq;
	pj_int32_t cseq;
	pj_str_t method;
	pj_status_t status;


	rdata = e->body.tsx_state.src.rdata;
	msg = rdata->msg_info.msg;

	/* Always reply with 200/OK for PRACK */
	status = pjsip_endpt_create_response(tsx->endpt, rdata, 
					     200, NULL, &tdata);
	if (status == PJ_SUCCESS)
		pjsip_tsx_send_msg(tsx, tdata);

	/* Ignore if we don't have pending transmission */
	if (dd->uas_state == NULL ||
	    pj_list_empty(&dd->uas_state->tx_data_list))
	{
		PJ_LOG(4,(dd->inv->dlg->obj_name, 
			  "PRACK ignored - no pending response"));
		return;
	}

	/* Find RAck header */
	rack_hdr = (pjsip_generic_string_hdr*)
		   pjsip_msg_find_hdr_by_name(msg, &RACK, NULL);
	if (!rack_hdr) {
		/* RAck header not found */
		PJ_LOG(4,(dd->inv->dlg->obj_name, "No RAck header"));
		return;
	}
	parse_rack(&rack_hdr->hvalue, &rseq, &cseq, &method);

	/* Match RAck against outgoing transmission */
	if (rseq == dd->uas_state->tx_data_list.next->rseq &&
	    cseq == dd->uas_state->cseq)
	{
		tx_data_list *tl = dd->uas_state->tx_data_list.next;

		/* Yes it match! */
		if (dd->uas_state->retransmit_timer.id) {
			pjsip_endpt_cancel_timer(dd->inv->dlg->endpt,
						 &dd->uas_state->retransmit_timer);
			dd->uas_state->retransmit_timer.id = PJ_FALSE;
		}

		/* Remove from the list */
		if (tl != &dd->uas_state->tx_data_list) {
			pj_list_erase(tl);

			/* Destroy the response */
			pjsip_tx_data_dec_ref(tl->tdata);
		}

		/* Schedule next packet */
		dd->uas_state->retransmit_count = 0;
		if (!pj_list_empty(&dd->uas_state->tx_data_list)) {
			on_retransmit(NULL, &dd->uas_state->retransmit_timer);
		}

	} else {
		/* No it doesn't match */
		PJ_LOG(4,(dd->inv->dlg->obj_name, 
			 "Rx PRACK with no matching reliable response"));
	}
}


/*
 * Handle incoming provisional response with 100rel requirement.
 * In this case we shall transmit PRACK request.
 */
static void handle_incoming_response(dlg_data *dd, pjsip_transaction *tsx,
				     pjsip_event *e)
{
	pjsip_rx_data *rdata;
	pjsip_msg *msg;
	pjsip_generic_string_hdr *rseq_hdr;
	pjsip_generic_string_hdr *rack_hdr;
	unsigned rseq;
	pj_str_t rack;
	char rack_buf[80];
	pjsip_tx_data *tdata;
	pj_status_t status;

	rdata = e->body.tsx_state.src.rdata;
	msg = rdata->msg_info.msg;

	/* Check our assumptions */
	pj_assert( tsx->role == PJSIP_ROLE_UAC &&
		   tsx->method.id == PJSIP_INVITE_METHOD &&
		   e->type == PJSIP_EVENT_TSX_STATE &&
		   e->body.tsx_state.type == PJSIP_EVENT_RX_MSG && 
		   msg->line.status.code > 100 &&
		   msg->line.status.code < 200);


	/* Get the RSeq header */
	rseq_hdr = (pjsip_generic_string_hdr*)
		   pjsip_msg_find_hdr_by_name(msg, &RSEQ, NULL);
	if (rseq_hdr == NULL) {
		PJ_LOG(4,(dd->inv->dlg->obj_name, 
			 "Ignoring provisional response with no RSeq header"));
		return;
	}
	rseq = (pj_uint32_t) pj_strtoul(&rseq_hdr->hvalue);

	/* Create new UAC state if we don't have one */
	if (dd->uac_state == NULL) {
		dd->uac_state = PJ_POOL_ZALLOC_T(dd->inv->dlg->pool,
						 struct uac_state);
		dd->uac_state->cseq = rdata->msg_info.cseq->cseq;
		dd->uac_state->rseq = rseq - 1;
	}

	/* If this is from new INVITE transaction, reset UAC state */
	if (rdata->msg_info.cseq->cseq != dd->uac_state->cseq) {
		dd->uac_state->cseq = rdata->msg_info.cseq->cseq;
		dd->uac_state->rseq = rseq - 1;
	}

	/* Ignore provisional response retransmission */
	if (rseq <= dd->uac_state->rseq) {
		/* This should have been handled before */
		return;

	/* Ignore provisional response with out-of-order RSeq */
	} else if (rseq != dd->uac_state->rseq + 1) {
		PJ_LOG(4,(dd->inv->dlg->obj_name, 
			 "Ignoring provisional response because RSeq jump "
			 "(expecting %u, got %u)",
			 dd->uac_state->rseq+1, rseq));
		return;
	}

	/* Update our RSeq */
	dd->uac_state->rseq = rseq;

	/* Create PRACK */
	status = pjsip_dlg_create_request(dd->inv->dlg, &pjsip_prack_method,
					  -1, &tdata);
	if (status != PJ_SUCCESS) {
		PJ_LOG(4,(dd->inv->dlg->obj_name, 
			 "Error creating PRACK request (status=%d)", status));
		return;
	}

	/* Create RAck header */
	rack.ptr = rack_buf;
	rack.slen = pj_ansi_snprintf(rack.ptr, sizeof(rack_buf),
				     "%u %u %.*s",
				     rseq, rdata->msg_info.cseq->cseq,
				     (int)tsx->method.name.slen,
				     tsx->method.name.ptr);
	PJ_ASSERT_ON_FAIL(rack.slen > 0 && rack.slen < sizeof(rack_buf),
			{ pjsip_tx_data_dec_ref(tdata); return; });
	rack_hdr = pjsip_generic_string_hdr_create(tdata->pool, &RACK, &rack);
	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*) rack_hdr);

	/* Send PRACK */
	pjsip_dlg_send_request(dd->inv->dlg, tdata, 
			       mod_100rel.mod.id, (void*) dd);

}


/*
 * API: init module
 */
PJ_DEF(pj_status_t) pjsip_100rel_init_module(pjsip_endpoint *endpt)
{
	return pjsip_endpt_register_module(endpt, &mod_100rel.mod);
}


/*
 * API: attach 100rel support in invite session. Called by
 *      sip_inv.c
 */
PJ_DEF(pj_status_t) pjsip_100rel_attach(pjsip_inv_session *inv)
{
	dlg_data *dd;

	/* Check that 100rel module has been initialized */
	PJ_ASSERT_RETURN(mod_100rel.mod.id >= 0, PJ_EINVALIDOP);

	/* Create and attach as dialog usage */
	dd = PJ_POOL_ZALLOC_T(inv->dlg->pool, dlg_data);
	dd->inv = inv;
	pjsip_dlg_add_usage(inv->dlg, &mod_100rel.mod, (void*)dd);

	PJ_LOG(5,(dd->inv->dlg->obj_name, "100rel module attached"));

	return PJ_SUCCESS;
}


/*
 * This is retransmit timer callback, called initially to send the response,
 * and subsequently when the retransmission time elapses.
 */
static void on_retransmit(pj_timer_heap_t *timer_heap,
			  struct pj_timer_entry *entry)
{
	dlg_data *dd;
	tx_data_list *tl;
	pjsip_tx_data *tdata;
	pj_bool_t final;
	pj_time_val delay;

	PJ_UNUSED_ARG(timer_heap);

	dd = (dlg_data*) entry->user_data;

	entry->id = PJ_FALSE;

	++dd->uas_state->retransmit_count;
	if (dd->uas_state->retransmit_count >= 7) {
		/* If a reliable provisional response is retransmitted for
		   64*T1 seconds  without reception of a corresponding PRACK,
		   the UAS SHOULD reject the original request with a 5xx 
		   response.
		*/
		pj_str_t reason = pj_str("Reliable response timed out");
		pj_status_t status;

		/* Clear all pending responses */
		clear_all_responses(dd);

		/* Send 500 response */
		status = pjsip_inv_end_session(dd->inv, 500, &reason, &tdata);
		if (status == PJ_SUCCESS) {
			pjsip_dlg_send_response(dd->inv->dlg, 
						dd->inv->invite_tsx,
						tdata);
		}
		return;
	}

	pj_assert(!pj_list_empty(&dd->uas_state->tx_data_list));
	tl = dd->uas_state->tx_data_list.next;
	tdata = tl->tdata;

	pjsip_tx_data_add_ref(tdata);
	final = tdata->msg->line.status.code >= 200;

	if (dd->uas_state->retransmit_count == 1) {
		pjsip_tsx_send_msg(dd->inv->invite_tsx, tdata);
	} else {
		pjsip_tsx_retransmit_no_state(dd->inv->invite_tsx, tdata);
	}

	if (final) {
		/* This is final response, which will be retransmitted by
		 * UA layer. There's no more task to do, so clear the
		 * transmission list and bail out.
		 */
		clear_all_responses(dd);
		return;
	}

	/* Schedule next retransmission */
	if (dd->uas_state->retransmit_count < 6) {
		delay.sec = 0;
		delay.msec = (1 << dd->uas_state->retransmit_count) * 
			     PJSIP_T1_TIMEOUT;
		pj_time_val_normalize(&delay);
	} else {
		delay.sec = 1;
		delay.msec = 500;
	}


	pjsip_endpt_schedule_timer(dd->inv->dlg->endpt, 
				   &dd->uas_state->retransmit_timer,
				   &delay);

	entry->id = PJ_TRUE;
}

/* Clone response. */
static pjsip_tx_data *clone_tdata(dlg_data *dd,
				  const pjsip_tx_data *src)
{
	pjsip_tx_data *dst;
	const pjsip_hdr *hsrc;
	pjsip_msg *msg;
	pj_status_t status;

	status = pjsip_endpt_create_tdata(dd->inv->dlg->endpt, &dst);
	if (status != PJ_SUCCESS)
		return NULL;

	msg = pjsip_msg_create(dst->pool, PJSIP_RESPONSE_MSG);
	dst->msg = msg;
	pjsip_tx_data_add_ref(dst);

	/* Duplicate status line */
	msg->line.status.code = src->msg->line.status.code;
	pj_strdup(dst->pool, &msg->line.status.reason, 
		  &src->msg->line.status.reason);

	/* Duplicate all headers */
	hsrc = src->msg->hdr.next;
	while (hsrc != &src->msg->hdr) {
		pjsip_hdr *h = pjsip_hdr_clone(dst->pool, hsrc);
		pjsip_msg_add_hdr(msg, h);
		hsrc = hsrc->next;
	}

	/* Duplicate message body */
	if (src->msg->body)
		msg->body = pjsip_msg_body_clone(dst->pool, src->msg->body);

	PJ_LOG(5,(dd->inv->dlg->obj_name,
		 "Reliable response %s created",
		 pjsip_tx_data_get_info(dst)));

	return dst;
}

PJ_DEF(pj_status_t) pjsip_100rel_tx_response(pjsip_inv_session *inv,
					     pjsip_tx_data *tdata)
{
	pjsip_cseq_hdr *cseq_hdr;
	pjsip_generic_string_hdr *rseq_hdr;
	pjsip_require_hdr *req_hdr;
	int status_code;
	dlg_data *dd;
	pjsip_tx_data *old_tdata;
	pj_status_t status;

	PJ_ASSERT_RETURN(tdata->msg->type == PJSIP_RESPONSE_MSG,
			 PJ_EINVALIDOP);

	status_code = tdata->msg->line.status.code;

	/* 100 response doesn't need PRACK */
	if (status_code == 100)
		return pjsip_dlg_send_response(inv->dlg, inv->invite_tsx, tdata);

	/* Get the dialog data */
	dd = (dlg_data*) inv->dlg->mod_data[mod_100rel.mod.id];
	PJ_ASSERT_RETURN(dd != NULL, PJ_EINVALIDOP);


	/* Clone tdata */
	old_tdata = tdata;
	tdata = clone_tdata(dd, old_tdata);
	pjsip_tx_data_dec_ref(old_tdata);

	/* Get CSeq header */
	cseq_hdr = (pjsip_cseq_hdr*)
		   pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ, NULL);
	PJ_ASSERT_RETURN(cseq_hdr != NULL, PJ_EBUG);
	PJ_ASSERT_RETURN(cseq_hdr->method.id == PJSIP_INVITE_METHOD, 
			 PJ_EINVALIDOP);

	/* Remove existing Require header */
	req_hdr = find_req_hdr(tdata->msg);
	if (req_hdr) {
		pj_list_erase(req_hdr);
	}

	/* Remove existing RSeq header */
	rseq_hdr = (pjsip_generic_string_hdr*)
		   pjsip_msg_find_hdr_by_name(tdata->msg, &RSEQ, NULL);
	if (rseq_hdr)
		pj_list_erase(rseq_hdr);

	/* Different treatment for provisional and final response */
	if (status_code/100 == 2) {

		/* RFC 3262 Section 3: UAS Behavior:

		The UAS MAY send a final response to the initial request 
		before having received PRACKs for all unacknowledged 
		reliable provisional responses, unless the final response 
		is 2xx and any of the unacknowledged reliable provisional 
		responses contained a session description.  In that case, 
		it MUST NOT send a final response until those provisional 
		responses are acknowledged.
		*/

		if (dd->uas_state && dd->uas_state->has_sdp) {
			/* Yes we have transmitted 1xx with SDP reliably.
			 * In this case, must queue the 2xx response.
			 */
			tx_data_list *tl;

			tl = PJ_POOL_ZALLOC_T(tdata->pool, tx_data_list);
			tl->tdata = tdata;
			tl->rseq = (pj_uint32_t)-1;
			pj_list_push_back(&dd->uas_state->tx_data_list, tl);

			/* Will send later */
			status = PJ_SUCCESS;

			PJ_LOG(4,(dd->inv->dlg->obj_name, 
				  "2xx response will be sent after PRACK"));

		} else if (dd->uas_state) {
			/* 
			If the UAS does send a final response when reliable
			responses are still unacknowledged, it SHOULD NOT 
			continue to retransmit the unacknowledged reliable
			provisional responses, but it MUST be prepared to 
			process PRACK requests for those outstanding 
			responses.
			*/
			
			PJ_LOG(4,(dd->inv->dlg->obj_name, 
				  "No SDP sent so far, sending 2xx now"));

			/* Cancel the retransmit timer */
			if (dd->uas_state->retransmit_timer.id) {
				pjsip_endpt_cancel_timer(dd->inv->dlg->endpt,
							 &dd->uas_state->retransmit_timer);
				dd->uas_state->retransmit_timer.id = PJ_FALSE;
			}

			/* Clear all pending responses (drop 'em) */
			clear_all_responses(dd);

			/* And transmit the 2xx response */
			status=pjsip_dlg_send_response(inv->dlg, 
						       inv->invite_tsx, tdata);

		} else {
			/* We didn't send any reliable provisional response */

			/* Transmit the 2xx response */
			status=pjsip_dlg_send_response(inv->dlg, 
						       inv->invite_tsx, tdata);
		}

	} else if (status_code >= 300) {

		/* 
		If the UAS does send a final response when reliable
		responses are still unacknowledged, it SHOULD NOT 
		continue to retransmit the unacknowledged reliable
		provisional responses, but it MUST be prepared to 
		process PRACK requests for those outstanding 
		responses.
		*/

		/* Cancel the retransmit timer */
		if (dd->uas_state && dd->uas_state->retransmit_timer.id) {
			pjsip_endpt_cancel_timer(dd->inv->dlg->endpt,
						 &dd->uas_state->retransmit_timer);
			dd->uas_state->retransmit_timer.id = PJ_FALSE;

			/* Clear all pending responses (drop 'em) */
			clear_all_responses(dd);
		}

		/* And transmit the 2xx response */
		status=pjsip_dlg_send_response(inv->dlg, 
					       inv->invite_tsx, tdata);

	} else {
		/*
		 * This is provisional response.
		 */
		char rseq_str[32];
		pj_str_t rseq;
		tx_data_list *tl;

		/* Create UAS state if we don't have one */
		if (dd->uas_state == NULL) {
			dd->uas_state = PJ_POOL_ZALLOC_T(inv->dlg->pool,
							 uas_state);
			dd->uas_state->cseq = cseq_hdr->cseq;
			dd->uas_state->rseq = pj_rand() % 0x7FFF;
			pj_list_init(&dd->uas_state->tx_data_list);
			dd->uas_state->retransmit_timer.user_data = dd;
			dd->uas_state->retransmit_timer.cb = &on_retransmit;
		}

		/* Check that CSeq match */
		PJ_ASSERT_RETURN(cseq_hdr->cseq == dd->uas_state->cseq,
				 PJ_EINVALIDOP);

		/* Add Require header */
		req_hdr = pjsip_require_hdr_create(tdata->pool);
		req_hdr->count = 1;
		req_hdr->values[0] = tag_100rel;
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)req_hdr);

		/* Add RSeq header */
		pj_ansi_snprintf(rseq_str, sizeof(rseq_str), "%u",
				 dd->uas_state->rseq);
		rseq = pj_str(rseq_str);
		rseq_hdr = pjsip_generic_string_hdr_create(tdata->pool, 
							   &RSEQ, &rseq);
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)rseq_hdr);

		/* Create list entry for this response */
		tl = PJ_POOL_ZALLOC_T(tdata->pool, tx_data_list);
		tl->tdata = tdata;
		tl->rseq = dd->uas_state->rseq++;

		/* Add to queue if there's pending response, otherwise
		 * transmit immediately.
		 */
		if (!pj_list_empty(&dd->uas_state->tx_data_list)) {
			
			int code = tdata->msg->line.status.code;

			/* Will send later */
			pj_list_push_back(&dd->uas_state->tx_data_list, tl);
			status = PJ_SUCCESS;

			PJ_LOG(4,(dd->inv->dlg->obj_name, 
				  "Reliable %d response enqueued (%d pending)", 
				  code, pj_list_size(&dd->uas_state->tx_data_list)));

		} else {
			pj_list_push_back(&dd->uas_state->tx_data_list, tl);

			dd->uas_state->retransmit_count = 0;
			on_retransmit(NULL, &dd->uas_state->retransmit_timer);
			status = PJ_SUCCESS;
		}

		/* Update SDP flag. Need to call this after the response
		 * is scheduled for transmission.
		 */
		dd->uas_state->has_sdp |= (tdata->msg->body != NULL);
	}

	return status;
}


#endif	/* PJSIP_HAS_100REL */
