/*
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
 *
 */

#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "ota-metadata.h"
#include <inttypes.h>
#include "cfs/cfs.h"

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define WITH_SERVER_REPLY  1
#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678

#define FIRMWARE_FILENAME "firmware.bin"

typedef struct {
  int fd;                  /* Dosya tanımlayıcı (CFS için) */
  uint32_t total_size;     /* Firmware toplam boyutu */
  uint32_t current_offset; /* Okunacak/yazılacak güncel konum */
} firmware_context_t;

static firmware_context_t fw_ctx;

static void init_firmware_context(void)
{
  cfs_remove(FIRMWARE_FILENAME);
  fw_ctx.fd = -1;
  fw_ctx.total_size = 0;
  fw_ctx.current_offset = 0;
  LOG_INFO("Firmware context initialized, ready for new firmware.\n");
}

static struct simple_udp_connection udp_conn;

PROCESS(udp_server_process, "UDP server");
AUTOSTART_PROCESSES(&udp_server_process);
/*---------------------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  if(datalen >= sizeof(ota_chunk_t)) {
    const ota_chunk_t *chunk = (const ota_chunk_t *)data;
    LOG_INFO("Received OTA chunk: offset=%" PRIu32 ", length=%u, checksum=%u from ",
             chunk->offset, chunk->length, chunk->checksum);
    LOG_INFO_6ADDR(sender_addr);
    LOG_INFO_("\n");
    
    /* TODO: Gelen paketin checksum dogrulamasi yapilacak */
    
    /* Gelen paketi kalıcı depolamaya (diske) yaz */
    fw_ctx.fd = cfs_open(FIRMWARE_FILENAME, CFS_WRITE);
    if(fw_ctx.fd >= 0) {
      if(cfs_seek(fw_ctx.fd, chunk->offset, CFS_SEEK_SET) != (cfs_offset_t)-1) {
        cfs_write(fw_ctx.fd, chunk->payload, chunk->length);
        fw_ctx.current_offset += chunk->length;
        LOG_INFO("Wrote %u bytes to CFS at offset %" PRIu32 ".\n", chunk->length, chunk->offset);
      } else {
        LOG_ERR("Failed to seek to offset %" PRIu32 " in CFS.\n", chunk->offset);
      }
      cfs_close(fw_ctx.fd);
    } else {
      LOG_ERR("Failed to open CFS file %s for writing.\n", FIRMWARE_FILENAME);
    }
  } else {
    LOG_INFO("Received generic request '%.*s' from ", datalen, (char *) data);
    LOG_INFO_6ADDR(sender_addr);
    LOG_INFO_("\n");
  }

#if WITH_SERVER_REPLY
  /* send back the same string to the client as an echo reply */
  LOG_INFO("Sending response.\n");
  simple_udp_sendto(&udp_conn, data, datalen, sender_addr);
#endif /* WITH_SERVER_REPLY */
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();

  init_firmware_context();

  /* Initialize DAG root */
  NETSTACK_ROUTING.root_start();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL,
                      UDP_CLIENT_PORT, udp_rx_callback);

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
