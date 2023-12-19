/*
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
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
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \file
 *      Example resource
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#include <stdlib.h>
#include <string.h>
#include "rest-engine.h"
#include "er-coap.h"
#include "lib/random.h"
#include "net/ip/agg_payloads.h"
#include <node-id.h>

#define MAX_N_PAYLOADS 40
#define LEN_SINGLE_PAYLOAD 2 // This is the payload size in bytes


static void res_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void res_periodic_handler(void);


/*
 * A handler function named [resource name]_handler must be implemented for each RESOURCE.
 * A buffer for the response payload is provided through the buffer pointer. Simple resources can ignore
 * preferred_size and offset, but must respect the REST_MAX_CHUNK_SIZE limit for the buffer.
 * If a smaller block size is requested for CoAP, the REST framework automatically splits the data.
 */
PERIODIC_RESOURCE(res_hello,
         "title=\"Hello world: ?len=0..\";rt=\"Text\";obs",
         res_get_handler,
         NULL,NULL,NULL,
         CLOCK_SECOND * 30,
         res_periodic_handler);


static void
res_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
	int i,count_payload_size=0, value, j=0, agg[2], count_payload_bytes;
	char v[2], agg_pay[60];


	/*
	printf("--- Number of payloads is %d \n",get_num_payloads());
	
	// Inserting the payload produced by itself
		agg[0]='1';
		agg[1]='0';
		count_payload_size=count_payload_size+LEN_SINGLE_PAYLOAD;
	// Inserting the agg payloads
		for(i=0;i<get_num_payloads();i++){
			value=get_payloads(i);
			sprintf(v, "%d", value);
			printf("--- Value in int is %d \n", value);
			printf("--- Value in char is %s \n", v);
			agg_pay[count_payload_size]=v[0];
			agg_pay[count_payload_size+1]=v[1];
			count_payload_size=count_payload_size+LEN_SINGLE_PAYLOAD;
			printf("--- Length is %d \n",count_payload_size);
		}
	//printf("--- Length is %d \n", count_payload_size);
	//agg_pay[count_payload_size]='\0';// EOF


	//Call a function that reset the varialbes in agg_stat
	reset_payloads();

	// The query string can be retrieved by rest_get_query() or parsed for its key-value pairs.
	//memcpy(buffer, &agg, length);

	*/
	sprintf((char *)buffer, "%s", agg_pay);


	// text/plain is the default, hence this option could be omitted.
	REST.set_header_content_type(response, REST.type.TEXT_PLAIN); 
	REST.set_response_payload(response, buffer, 60);
}


static void
res_periodic_handler()
{
  /* Do a periodic task here, e.g., sampling a sensor. */
  //++event_counter;

  /* Usually a condition is defined under with subscribers are notified, e.g., large enough delta in sensor reading. */
  if(1) {
    /* Notify the registered observers which will trigger the res_get_handler to create the response. */
    REST.notify_subscribers(&res_hello);
  }
}
