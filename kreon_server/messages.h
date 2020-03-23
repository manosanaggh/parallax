/*
 * tucana_messages.h
 * To define the network messages for Tucana Network
 * Created by Pilar Gonzalez-Ferez on 28/07/16.
 * Copyright (c) 2016 Pilar Gonzalez Ferez <pilar@ics.forth.gr>.
*/

#pragma once

#include <inttypes.h>
#include <semaphore.h>
#include <time.h>
#include "conf.h"

enum tucana_message_types {

	PUT_REQUEST = 1, // PUT operation: client -> server
	MULTI_PUT, // FIXME Remove this message type
	PUT_REPLY,
	TU_GET_QUERY, // GET operation: client -> server
	TU_GET_REPLY, // GET reply: server -> client
	MULTI_GET_REQUEST,
	MULTI_GET_REPLY,
	TU_FLUSH_VOLUME_QUERY, // Flush volume
	TU_FLUSH_VOLUME_REPLY,
	SCAN_REQUEST, // SCAN operation: client -> server
	SCAN_REPLY, // SCAN reply: server -> client
	SPILL_INIT,
	SPILL_INIT_ACK,
	SPILL_BUFFER_REQUEST, //message with sorted kv pairs from primary's L0 level
	SPILL_COMPLETE,
	SPILL_COMPLETE_ACK,
	FLUSH_SEGMENT_AND_RESET,
	FLUSH_SEGMENT,
	FLUSH_SEGMENT_ACK,
	FLUSH_SEGMENT_ACK_AND_RESET,
	FLUSH_SEGMENT_TEST,
	SYNC_SEGMENT,
	SYNC_SEGMENT_ACK,
	TU_UPDATE,
	TU_UPDATE_REPLY,
	/*control stuff*/
	RESET_BUFFER,
	RESET_BUFFER_ACK,
	RESET_RENDEZVOUS,
	I_AM_CLIENT,
	DISCONNECT,
	CHANGE_CONNECTION_PROPERTIES_REQUEST,
	CHANGE_CONNECTION_PROPERTIES_REPLY,
	/*test messages*/
	TEST_REQUEST,
	TEST_REPLY,
	TEST_REQUEST_FETCH_PAYLOAD,
	TEST_REPLY_FETCH_PAYLOAD,
	CLIENT_STOP_NOW,
	SERVER_I_AM_READY,
	CLIENT_RECEIVED_READY
};

typedef enum send_options { SYNC_REQUEST = 2, ASYNC_REQUEST, BUSY_WAIT } receive_options;
typedef struct msg_key {
	uint32_t size;
	void *key[0];
} msg_key;

typedef struct msg_value {
	uint32_t size;
	void *value[0];
} msg_value;

// Set in allocate_rdma_message
#define SERVER_CATEGORY 26368 //0x6700
#define CLIENT_CATEGORY 21760 //0x5500

typedef struct tu_data_message {
#if TU_SEMAPHORE
	sem_t sem;
#endif
	/*Inform server where we expect the reply*/
	void *reply;
	volatile uint32_t reply_length;

	uint32_t pay_len; //Size of the payload
	uint32_t padding_and_tail; //padding so as MESSAGE total size is a multiple of MESSAGE_SEGMENT_SIZE
	uint16_t type; // Type of the message: PUT_REQUEST, PUT_REPLY, GET_QUERY, GET_REPLY, etc.
	uint8_t error_code;

	uint32_t value; //Number of operations included in the payload
	volatile uint64_t local_offset; //Offset regarding the local Memory region
	volatile uint64_t remote_offset; //Offset regarding the remote Memory region
	//From Client to head, local_offset == remote_offset
	//From head to replica1, it could be that local_offset != remote_offset
	//(real CHAIN implementation should be equal, since head only puts and tail only gets)
	//From replica-i to replica-i+1, local_offset == remote_offset
	/*<gesalous>*/
	/*for asynchronous requests*/
	void *callback_function_args;
	void (*callback_function)(void *args);
	/*</gesalous>*/

	void *reply_message; /* Filled by the receiving side on arrival of a message. If request_message_local_addr of
													 a received message is not NULL the receiving side uses the request_message_local_addr to
													 find the initial message and fill its reply_message field with the address of the new message*/

	void *request_message_local_addr; /* This field contains the local memory address of this message.
																			 * It is piggybacked by the remote side to its
																			 *  corresponding reply message. In this way
																			 *  incoming messages can be associated with
																			 *  the initial request messages*/

	/*gesalous staff also*/
	volatile int32_t ack_arrived;
	/*from most significant byte to less: <FUTURE_EXTENSION>, <FUTURE_EXTENSION>, 
	 * <FUTUTE_EXTENSION>, SYNC/ASYNC(indicates if this is  a synchronous or asynchronous request*/
	volatile int32_t flags;
#ifdef CHECKSUM_DATA_MESSAGES
	unsigned long hash; // [mvard] hash of data buffer
#endif
	void *data; /*Pointer to the first element of the Payload*/
	void *next; /*Pointer to the "current" element of the payload. Initially equal to data*/
	void *tail; /*Pointer to the tail that will be an integer to indicate that the data has been received*/
	uint32_t receive;
} tu_data_message;

typedef struct msg_multi_get_req {
	uint32_t max_num_entries;
	uint32_t seek_mode;
	msg_key *seek_key;
} msg_multi_get_req;

typedef struct msg_multi_get_rep {
	uint32_t num_entries;
	uint8_t end_of_database;
	uint32_t pos;
	void *kv_buffer[0];
} msg_multi_get_rep;

typedef struct set_connection_property_req {
	int desired_priority_level;
	int desired_RDMA_memory_size;
} set_connection_property_req;

typedef struct set_connection_property_reply {
	int assigned_ppriority_level;
	int assigned_RDMA_memory_size;
} set_connection_property_reply;

int push_buffer_in_tu_data_message(struct tu_data_message *data_message, char *buffer, uint32_t buffer_length);

