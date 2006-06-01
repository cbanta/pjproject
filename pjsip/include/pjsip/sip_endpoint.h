/* $Id$ */
/* 
 * Copyright (C) 2003-2006 Benny Prijono <benny@prijono.org>
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
#ifndef __PJSIP_SIP_ENDPOINT_H__
#define __PJSIP_SIP_ENDPOINT_H__

/**
 * @file sip_endpoint.h
 * @brief SIP Endpoint.
 */

#include <pjsip/sip_transport.h>
#include <pjsip/sip_resolve.h>

PJ_BEGIN_DECL

/**
 * @defgroup PJSIP SIP Stack Core
 * Implementation of core SIP protocol stack processing.
 */

/**
 * @defgroup PJSIP_ENDPT SIP Endpoint
 * @ingroup PJSIP
 * @brief
 * Representation of SIP node instance.
 *
 * SIP Endpoint instance (pjsip_endpoint) can be viewed as the master/owner of
 * all SIP objects in an application. It performs the following roles:
 *  - it manages the allocation/deallocation of memory pools for all objects.
 *  - it manages listeners and transports, and how they are used by 
 *    transactions.
 *  - it receives incoming messages from transport layer and automatically
 *    dispatches them to the correct transaction (or create a new one).
 *  - it has a single instance of timer management (timer heap).
 *  - it manages modules, which is the primary means of extending the library.
 *  - it provides single polling function for all objects and distributes 
 *    events.
 *  - it automatically handles incoming requests which can not be handled by
 *    existing modules (such as when incoming request has unsupported method).
 *  - and so on..
 *
 * Theoritically application can have multiple instances of SIP endpoint, 
 * although it's not clear why application may want to do it.
 *
 * @{
 */

/**
 * Create an instance of SIP endpoint from the specified pool factory.
 * The pool factory reference then will be kept by the endpoint, so that 
 * future memory allocations by SIP components will be taken from the same
 * pool factory.
 *
 * @param pf	        Pool factory that will be used for the lifetime of 
 *                      endpoint.
 * @param name          Optional name to be specified for the endpoint.
 *                      If this parameter is NULL, then the name will use
 *                      local host name.
 * @param endpt         Pointer to receive endpoint instance.
 *
 * @return              PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjsip_endpt_create(pj_pool_factory *pf,
					const char *name,
                                        pjsip_endpoint **endpt);

/**
 * Destroy endpoint instance. Application must make sure that all pending
 * transactions have been terminated properly, because this function does not
 * check for the presence of pending transactions.
 *
 * @param endpt		The SIP endpoint to be destroyed.
 */
PJ_DECL(void) pjsip_endpt_destroy(pjsip_endpoint *endpt);

/**
 * Get endpoint name.
 *
 * @param endpt         The SIP endpoint instance.
 *
 * @return              Endpoint name, as was registered during endpoint
 *                      creation. The string is NULL terminated.
 */
PJ_DECL(const pj_str_t*) pjsip_endpt_name(const pjsip_endpoint *endpt);

/**
 * Poll for events. Application must call this function periodically to ensure
 * that all events from both transports and timer heap are handled in timely
 * manner.  This function, like all other endpoint functions, is thread safe, 
 * and application may have more than one thread concurrently calling this function.
 *
 * @param endpt		The endpoint.
 * @param max_timeout	Maximum time to wait for events, or NULL to wait forever
 *			until event is received.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjsip_endpt_handle_events( pjsip_endpoint *endpt, 
					        const pj_time_val *max_timeout);


/**
 * Handle events with additional info about number of events that
 * have been handled.
 *
 * @param endpt		The endpoint.
 * @param max_timeout	Maximum time to wait for events, or NULL to wait forever
 *			until event is received.
 * @param count		Optional argument to receive the number of events that
 *			have been handled by the function.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjsip_endpt_handle_events2(pjsip_endpoint *endpt,
					        const pj_time_val *max_timeout,
					        unsigned *count);
/**
 * Schedule timer to endpoint's timer heap. Application must poll the endpoint
 * periodically (by calling #pjsip_endpt_handle_events) to ensure that the
 * timer events are handled in timely manner. When the timeout for the timer
 * has elapsed, the callback specified in the entry argument will be called.
 * This function, like all other endpoint functions, is thread safe.
 *
 * @param endpt	    The endpoint.
 * @param entry	    The timer entry.
 * @param delay	    The relative delay of the timer.
 * @return	    PJ_OK (zero) if successfull.
 */
PJ_DECL(pj_status_t) pjsip_endpt_schedule_timer( pjsip_endpoint *endpt,
						 pj_timer_entry *entry,
						 const pj_time_val *delay );

/**
 * Cancel the previously registered timer.
 * This function, like all other endpoint functions, is thread safe.
 *
 * @param endpt	    The endpoint.
 * @param entry	    The timer entry previously registered.
 */
PJ_DECL(void) pjsip_endpt_cancel_timer( pjsip_endpoint *endpt, 
					pj_timer_entry *entry );


/**
 * Register new module to the endpoint.
 * The endpoint will then call the load and start function in the module to 
 * properly initialize the module, and assign a unique module ID for the 
 * module.
 *
 * @param endpt		The endpoint.
 * @param module	The module to be registered.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjsip_endpt_register_module( pjsip_endpoint *endpt,
						  pjsip_module *module );

/**
 * Unregister a module from the endpoint.
 * The endpoint will then call the stop and unload function in the module to 
 * properly shutdown the module.
 *
 * @param endpt		The endpoint.
 * @param module	The module to be registered.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjsip_endpt_unregister_module( pjsip_endpoint *endpt,
						    pjsip_module *module );


/**
 * Create pool from the endpoint. All SIP components should allocate their
 * memory pool by calling this function, to make sure that the pools are
 * allocated from the same pool factory. This function, like all other endpoint
 * functions, is thread safe.
 *
 * @param endpt		The SIP endpoint.
 * @param pool_name	Name to be assigned to the pool.
 * @param initial	The initial size of the pool.
 * @param increment	The resize size.
 * @return		Memory pool, or NULL on failure.
 *
 * @see pj_pool_create
 */
PJ_DECL(pj_pool_t*) pjsip_endpt_create_pool( pjsip_endpoint *endpt,
					     const char *pool_name,
					     pj_size_t initial,
					     pj_size_t increment );

/**
 * Return back pool to endpoint to be released back to the pool factory.
 * This function, like all other endpoint functions, is thread safe.
 *
 * @param endpt	    The endpoint.
 * @param pool	    The pool to be destroyed.
 */
PJ_DECL(void) pjsip_endpt_release_pool( pjsip_endpoint *endpt,
					pj_pool_t *pool );

/**
 * Create a new transaction. After creating the transaction, application MUST
 * initialize the transaction as either UAC or UAS (by calling
 * #pjsip_tsx_init_uac or #pjsip_tsx_init_uas), then must register the 
 * transaction to endpoint with #pjsip_endpt_register_tsx.
 * This function, like all other endpoint functions, is thread safe.
 *
 * @param endpt	    The SIP endpoint.
 * @param p_tsx	    Pointer to receive the transaction.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjsip_endpt_create_tsx(pjsip_endpoint *endpt,
					    pjsip_transaction **p_tsx);

/**
 * Find transaction in endpoint's transaction table by the transaction's key.
 * This function normally is only used by modules. The key for a transaction
 * can be created by calling #pjsip_tsx_create_key.
 *
 * @param endpt	    The endpoint instance.
 * @param key	    Transaction key, as created with #pjsip_tsx_create_key.
 *
 * @return	    The transaction, or NULL if it's not found.
 */
PJ_DECL(pjsip_transaction*) pjsip_endpt_find_tsx( pjsip_endpoint *endpt,
					          const pj_str_t *key );

/**
 * Register the transaction to the endpoint's transaction table.
 * Before the transaction is registered, it must have been initialized as
 * either UAS or UAC by calling #pjsip_tsx_init_uac or #pjsip_tsx_init_uas.
 * This function, like all other endpoint functions, is thread safe.
 *
 * @param endpt	    The SIP endpoint.
 * @param tsx	    The transaction.
 */
PJ_DECL(void) pjsip_endpt_register_tsx( pjsip_endpoint *endpt,
					pjsip_transaction *tsx);

/**
 * Forcefull destroy the transaction.
 * The only time where application needs to call this function is when the
 * transaction fails to initialize in #pjsip_tsx_init_uac or
 * #pjsip_tsx_init_uas. For other cases. the transaction will be destroyed
 * automaticly by endpoint.
 *
 * @param endpt	    The endpoint.
 * @param tsx	    The transaction to destroy.
 */
PJ_DECL(void) pjsip_endpt_destroy_tsx( pjsip_endpoint *endpt,
				      pjsip_transaction *tsx);

/**
 * Create a new transmit data buffer.
 * This function, like all other endpoint functions, is thread safe.
 *
 * @param endpt	    The endpoint.
 * @param p_tdata    Pointer to receive transmit data buffer.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjsip_endpt_create_tdata( pjsip_endpoint *endpt,
					       pjsip_tx_data **p_tdata);

/**
 * Asynchronously resolve a SIP target host or domain according to rule 
 * specified in RFC 3263 (Locating SIP Servers). When the resolving operation
 * has completed, the callback will be called.
 *
 * Note: at the moment we don't have implementation of RFC 3263 yet!
 *
 * @param resolver  The resolver engine.
 * @param pool	    The pool to allocate resolver job.
 * @param target    The target specification to be resolved.
 * @param token	    A user defined token to be passed back to callback function.
 * @param cb	    The callback function.
 */
PJ_DECL(void) pjsip_endpt_resolve( pjsip_endpoint *endpt,
				   pj_pool_t *pool,
				   pjsip_host_info *target,
				   void *token,
				   pjsip_resolver_callback *cb);

/**
 * Get transport manager instance.
 *
 * @param endpt	    The endpoint.
 *
 * @return	    Transport manager instance.
 */
PJ_DECL(pjsip_tpmgr*) pjsip_endpt_get_tpmgr(pjsip_endpoint *endpt);

/**
 * Get ioqueue instance.
 *
 * @param endpt	    The endpoint.
 *
 * @return	    The ioqueue.
 */
PJ_DECL(pj_ioqueue_t*) pjsip_endpt_get_ioqueue(pjsip_endpoint *endpt);

/**
 * Find a SIP transport suitable for sending SIP message to the specified
 * address. This function will complete asynchronously when the transport is
 * ready (for example, when TCP socket is connected), and when it completes,
 * the callback will be called with the status of the operation.
 *
 * @see pjsip_transport_get
 */
PJ_DECL(pj_status_t) 
pjsip_endpt_acquire_transport( pjsip_endpoint *endpt,
			       pjsip_transport_type_e type,
			       const pj_sockaddr_t *remote,
			       int addr_len,
			       pjsip_transport **p_transport);


/*****************************************************************************
 *
 * Capabilities Management
 *
 * Modules may implement new capabilities to the stack. These capabilities
 * are indicated by the appropriate SIP header fields, such as Accept,
 * Accept-Encoding, Accept-Language, Allow, Supported, etc.
 *
 * When a module provides new capabilities to the stack, it registers these
 * capabilities to the endpoint by supplying new tags (strings) to the
 * appropriate header fields. Application (or other modules) can then query
 * these header fields to get the list of supported capabilities, and may
 * include these headers in the outgoing message.
 *****************************************************************************
 */

/**
 * Get the value of the specified capability header field.
 *
 * @param endpt	    The endpoint.
 * @param htype	    The header type to be retrieved, which value may be:
 *		    - PJSIP_H_ACCEPT
 *		    - PJSIP_H_ALLOW
 *		    - PJSIP_H_SUPPORTED
 * @param hname	    If htype specifies PJSIP_H_OTHER, then the header name
 *		    must be supplied in this argument. Otherwise the value
 *		    must be set to NULL.
 *
 * @return	    The appropriate header, or NULL if the header is not
 *		    available.
 */
PJ_DECL(const pjsip_hdr*) pjsip_endpt_get_capability( pjsip_endpoint *endpt,
						      int htype,
						      const pj_str_t *hname);


/**
 * Add or register new capabilities as indicated by the tags to the
 * appropriate header fields in the endpoint.
 *
 * @param endpt	    The endpoint.
 * @param mod	    The module which registers the capability.
 * @param htype	    The header type to be set, which value may be:
 *		    - PJSIP_H_ACCEPT
 *		    - PJSIP_H_ALLOW
 *		    - PJSIP_H_SUPPORTED
 * @param hname	    If htype specifies PJSIP_H_OTHER, then the header name
 *		    must be supplied in this argument. Otherwise the value
 *		    must be set to NULL.
 * @param count	    The number of tags in the array.
 * @param tags	    Array of tags describing the capabilities or extensions
 *		    to be added to the appropriate header.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjsip_endpt_add_capability( pjsip_endpoint *endpt,
						 pjsip_module *mod,
						 int htype,
						 const pj_str_t *hname,
						 unsigned count,
						 const pj_str_t tags[]);

/**
 * Get list of additional headers to be put in outgoing request message.
 * Currently only Max-Forwards are defined.
 *
 * @param e	    The endpoint.
 *
 * @return	    List of headers.
 */
PJ_DECL(const pjsip_hdr*) pjsip_endpt_get_request_headers(pjsip_endpoint *e);


/**
 * Dump endpoint status to the log. This will print the status to the log
 * with log level 3.
 *
 * @param endpt		The endpoint.
 * @param detail	If non zero, then it will dump a detailed output.
 *			BEWARE that this option may crash the system because
 *			it tries to access all memory pools.
 */
PJ_DECL(void) pjsip_endpt_dump( pjsip_endpoint *endpt, pj_bool_t detail );



/**
 * Log an error.
 */
PJ_DECL(void) pjsip_endpt_log_error( pjsip_endpoint *endpt,
				     const char *sender,
                                     pj_status_t error_code,
                                     const char *format,
                                     ... );

#define PJSIP_ENDPT_LOG_ERROR(expr)   \
            pjsip_endpt_log_error expr

#define PJSIP_ENDPT_TRACE(tracing,expr) \
            do {                        \
                if ((tracing))          \
                    PJ_LOG(4,expr);     \
            } while (0)

/**
 * @}
 */

/*
 * Internal functions.
 */
/*
 * Receive transaction events from transactions and put in the event queue
 * to be processed later.
 */
void pjsip_endpt_send_tsx_event( pjsip_endpoint *endpt, pjsip_event *evt );

PJ_END_DECL

#endif	/* __PJSIP_SIP_ENDPOINT_H__ */

