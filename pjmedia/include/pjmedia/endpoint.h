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
#ifndef __PJMEDIA_MEDIAMGR_H__
#define __PJMEDIA_MEDIAMGR_H__


/**
 * @file mediamgr.h
 * @brief Media Manager.
 */
/**
 * @defgroup PJMED_ENDPT Media Endpoint
 * @ingroup PJMEDIA
 * @{
 *
 * The media endpoint acts as placeholder for endpoint capabilities. Each 
 * media endpoint will have a codec manager to manage list of codecs installed
 * in the endpoint and a sound device factory.
 *
 * A reference to media endpoint instance is required when application wants
 * to create a media session (#pj_media_session_create or 
 * #pj_media_session_create_from_sdp).
 */

#include <pjmedia/sound.h>
#include <pjmedia/codec.h>


PJ_BEGIN_DECL



/**
 * Create an instance of media endpoint.
 *
 * @param pf		Pool factory, which will be used by the media endpoint
 *			throughout its lifetime.
 * @param p_endpt	Pointer to receive the endpoint instance.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_endpt_create( pj_pool_factory *pf,
					   pjmedia_endpt **p_endpt);

/**
 * Destroy media endpoint instance.
 *
 * @param endpt		Media endpoint instance.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_endpt_destroy(pjmedia_endpt *endpt);


/**
 * Request the media endpoint to create pool.
 *
 * @param endpt		The media endpoint instance.
 * @param name		Name to be assigned to the pool.
 * @param initial	Initial pool size, in bytes.
 * @param increment	Increment size, in bytes.
 *
 * @return		Memory pool.
 */
PJ_DECL(pj_pool_t*) pjmedia_endpt_create_pool( pjmedia_endpt *endpt,
					       const char *name,
					       pj_size_t initial,
					       pj_size_t increment);

/**
 * Get the codec manager instance of the media endpoint.
 *
 * @param endpt		The media endpoint instance.
 *
 * @return		The instance of codec manager belonging to
 *			this media endpoint.
 */
PJ_DECL(pjmedia_codec_mgr*) pjmedia_endpt_get_codec_mgr(pjmedia_endpt *mgr);


/**
 * Create a SDP session description that describes the endpoint
 * capability.
 *
 * @param endpt		The media endpoint.
 * @param pool		Pool to use to create the SDP descriptor.
 * @param stream_cnt	Number of elements in the sock_info array. This
 *			also denotes the maximum number of streams (i.e.
 *			the "m=" lines) that will be created in the SDP.
 * @param sock_info	Array of socket transport information. One 
 *			transport is needed for each media stream, and
 *			each transport consists of an RTP and RTCP socket
 *			pair.
 * @param p_sdp		Pointer to receive SDP session descriptor.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_endpt_create_sdp( pjmedia_endpt *endpt,
					       pj_pool_t *pool,
					       unsigned stream_cnt,
					       const pjmedia_sock_info sock_info[],
					       pjmedia_sdp_session **p_sdp );


/**
 * Dump media endpoint capabilities.
 *
 * @param endpt		The media endpoint.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_endpt_dump(pjmedia_endpt *endpt);


PJ_END_DECL


/**
 * @}
 */



#endif	/* __PJMEDIA_MEDIAMGR_H__ */
