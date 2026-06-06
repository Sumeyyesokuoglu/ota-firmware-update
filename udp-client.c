#include "contiki.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "ota-metadata.h"
#include "firmware_data.h"
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "sys/node-id.h"
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678

#define SEND_INTERVAL		  (2 * CLOCK_SECOND)

static struct simple_udp_connection udp_conn;
static uint32_t current_offset = 0;
static bool eof_sent = false;

static uint16_t compute_checksum(const uint8_t *data, uint16_t len) {
  uint16_t sum = 0;
  for(uint16_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum;
}
static ota_boot_metadata_t boot_metadata = {
  .magic = OTA_IMAGE_MAGIC,
  .active_slot = OTA_SLOT_A,
  .candidate_slot = OTA_SLOT_NONE,
  .state_a = OTA_IMAGE_STATE_CONFIRMED,
  .state_b = OTA_IMAGE_STATE_EMPTY,
};

/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "UDP client");
AUTOSTART_PROCESSES(&udp_client_process);
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
  (void)c;
  (void)sender_port;
  (void)receiver_addr;
  (void)receiver_port;

  if(datalen == sizeof(uint32_t)) {
    uint32_t ack_offset;
    memcpy(&ack_offset, data, sizeof(uint32_t));
    LOG_INFO("Client received ACK for offset %" PRIu32 " from ", ack_offset);
    LOG_INFO_6ADDR(sender_addr);
    LOG_INFO_("\n");

    if(ack_offset == current_offset && !eof_sent) {
      /* Başarılı iletim, sonraki bloğa geç */
      uint32_t chunk_length = 64;
      if(current_offset + 64 > firmware_length) {
        chunk_length = firmware_length - current_offset;
      }
      current_offset += chunk_length;
    } else if(eof_sent && ack_offset == firmware_length) {
      LOG_INFO("Bitiş (EOF) paketi de onaylandı. Aktarım tamamen bitti.\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer periodic_timer;
  static char str[32];
  uip_ipaddr_t dest_ipaddr;
  static uint32_t tx_count;
  static uint32_t missed_tx_count;

  PROCESS_BEGIN();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);

  etimer_set(&periodic_timer, random_rand() % SEND_INTERVAL);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

    if(NETSTACK_ROUTING.node_is_reachable() &&
        NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {

// Bu blok 3 numarali cihazda calismaz. 2-den-1-e gonderim icin yapildi. 3 numarali
// cihaz komsuluk gorevi yapar. İletime yardim eder.

      if(node_id == 2) {
        if(current_offset < firmware_length) {
          ota_chunk_t chunk;
          memset(&chunk, 0, sizeof(chunk));
          chunk.offset = current_offset;
          
          uint32_t len = 64;
          if(current_offset + len > firmware_length) {
            len = firmware_length - current_offset;
          }
          chunk.length = (uint16_t)len;
          memcpy(chunk.payload, firmware_data + current_offset, len);
          chunk.checksum = compute_checksum(chunk.payload, chunk.length);

          LOG_INFO("Sending chunk at offset %" PRIu32 ", length %u to ", chunk.offset, chunk.length);
          LOG_INFO_6ADDR(&dest_ipaddr);
          LOG_INFO_("\n");

          simple_udp_sendto(&udp_conn, &chunk, sizeof(ota_chunk_t), &dest_ipaddr);
        } else if(!eof_sent) {
          /* Dosya aktarımı bitti, tam dosya CRC32'sini hesapla ve EOF paketine ekle */
          ota_chunk_t eof_chunk;
          memset(&eof_chunk, 0, sizeof(eof_chunk));
          eof_chunk.offset = firmware_length;
          eof_chunk.length = 0; /* EOF sinyali */
          eof_chunk.checksum = 0;
          
          uint32_t full_crc = ota_crc32_buffer(firmware_data, firmware_length);
          memcpy(eof_chunk.payload, &full_crc, sizeof(uint32_t));

          LOG_INFO("Sending EOF chunk with CRC32 0x%08\" PRIX32 \" to ", full_crc);
          LOG_INFO_6ADDR(&dest_ipaddr);
          LOG_INFO_("\n");

          simple_udp_sendto(&udp_conn, &eof_chunk, sizeof(ota_chunk_t), &dest_ipaddr);
          eof_sent = true;
        }
      }


    } else {
      LOG_INFO("Not reachable yet\n");
      if(tx_count > 0) {
        missed_tx_count++;
      }
    }

    /* Add some jitter */
    etimer_set(&periodic_timer, SEND_INTERVAL
      - CLOCK_SECOND + (random_rand() % (2 * CLOCK_SECOND)));
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
