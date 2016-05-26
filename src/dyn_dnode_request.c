/*
 * Dynomite - A thin, distributed replication layer for multi non-distributed storages.
 * Copyright (C) 2014 Netflix, Inc.
 */ 

#include "dyn_core.h"
#include "dyn_topology.h"
#include "dyn_dnode_peer.h"
#include "dyn_mbuf.h"
#include "dyn_server.h"


//static struct string client_request_dyn_msg = string("Client_request");
static uint64_t peer_msg_id = 0;

/*
 //for sending a string over to a peer
void
peer_gossip_forward1(struct context *ctx, struct conn *conn, bool redis, struct string *data)
{
    rstatus_t status;
    struct msg *msg = msg_get(conn, 1, redis);

    if (msg == NULL) {
        log_debug(LOG_DEBUG, "Unable to obtain a msg");
        return;
    }

    struct mbuf *nbuf = mbuf_get();
    if (nbuf == NULL) {
        log_debug(LOG_DEBUG, "Unable to obtain a mbuf");
        msg_put(msg);
        return;
    }

    uint64_t msg_id = peer_msg_id++;

    dmsg_write(nbuf, msg_id, GOSSIP_SYN, version, data);
    mbuf_insert_head(&msg->mhdr, nbuf);

    if (TAILQ_EMPTY(&conn->imsg_q)) {
        status = event_add_out(ctx->evb, conn);
        if (status != DN_OK) {
            dnode_req_forward_error(ctx, conn, msg);
            conn->err = errno;
            return;
        }
    }

    conn->enqueue_inq(ctx, conn, msg);


    log_debug(LOG_VERB, "gossip to peer %d with msg_id %"PRIu64" '%.*s'", conn->sd, msg->id,
                         data->len, data->data);

}
 */

/*
 * Sending a mbuf of gossip data over the wire to a peer
 */
void
dnode_peer_gossip_forward(struct context *ctx, struct conn *conn, struct mbuf *data_buf)
{
    rstatus_t status;
    struct msg *msg = msg_get(conn, 1, __FUNCTION__);

    if (msg == NULL) {
        log_debug(LOG_DEBUG, "Unable to obtain a msg");
        return;
    }

    struct mbuf *header_buf = mbuf_get();
    if (header_buf == NULL) {
        log_debug(LOG_DEBUG, "Unable to obtain a data_buf");
        rsp_put(msg);
        return;
    }

    uint64_t msg_id = peer_msg_id++;

    if (conn->dnode_secured) {
        if (log_loggable(LOG_VERB)) {
           log_debug(LOG_VERB, "Assemble a secured msg to send");
           log_debug(LOG_VERB, "AES encryption key: %s\n", base64_encode(conn->aes_key, AES_KEYLEN));
        }

        if (ENCRYPTION) {
            struct mbuf *encrypted_buf = mbuf_get();
            if (encrypted_buf == NULL) {
                loga("Unable to obtain an data_buf for encryption!");
                return; //TODOs: need to clean up
            }

            status = dyn_aes_encrypt(data_buf->pos, (int)mbuf_length(data_buf), encrypted_buf, conn->aes_key);
            if (log_loggable(LOG_VERB)) {
               log_debug(LOG_VERB, "#encrypted bytes : %d", status);
            }

            //write dnode header
            dmsg_write(header_buf, msg_id, GOSSIP_SYN, conn, mbuf_length(encrypted_buf));

            if (log_loggable(LOG_VVERB)) {
                log_hexdump(LOG_VVERB, data_buf->pos, mbuf_length(data_buf), "dyn message original payload: ");
                log_hexdump(LOG_VVERB, encrypted_buf->pos, mbuf_length(encrypted_buf), "dyn message encrypted payload: ");
            }

            mbuf_remove(&msg->mhdr, data_buf);
            mbuf_insert(&msg->mhdr, encrypted_buf);
            //free data_buf as no one will need it again
            mbuf_put(data_buf);  //TODOS: need to remove this from the msg->mhdr as in the other method

        } else {
            if (log_loggable(LOG_VVERB)) {
               log_debug(LOG_VVERB, "No encryption");
            }
            dmsg_write_mbuf(header_buf, msg_id, GOSSIP_SYN, conn, mbuf_length(data_buf));
            mbuf_insert(&msg->mhdr, data_buf);
        }

    } else {
        if (log_loggable(LOG_VVERB)) {
           log_debug(LOG_VVERB, "Assemble a non-secured msg to send");
        }
        dmsg_write_mbuf(header_buf, msg_id, GOSSIP_SYN, conn, mbuf_length(data_buf));
        mbuf_insert(&msg->mhdr, data_buf);
    }

    mbuf_insert_head(&msg->mhdr, header_buf);

    if (log_loggable(LOG_VVERB)) {
        log_hexdump(LOG_VVERB, header_buf->pos, mbuf_length(header_buf), "dyn gossip message header: ");
        msg_dump(msg);
    }

    /* enqueue the message (request) into peer inq */
    if (TAILQ_EMPTY(&conn->imsg_q)) {
        status = event_add_out(ctx->evb, conn);
        if (status != DN_OK) {
            dnode_req_forward_error(ctx, conn, msg, errno);
            conn->err = errno;
            return;
        }
    }

    //need to handle a reply
    //conn->enqueue_outq(ctx, conn, msg);

    msg->expect_datastore_reply = 0;
    conn_enqueue_inq(ctx, conn, msg);
}
