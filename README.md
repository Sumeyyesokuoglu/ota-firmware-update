# OTA Firmware Update Projesi Raporu

## 1. Cooja Ortamı ve Sistem Çalışma Videosu

Sistemin Cooja ortamındaki simülasyonunu ve çalışmasını detaylıca anlattığımız videomuzu aşağıdaki YouTube bağlantısından izleyebilirsiniz.

https://youtu.be/uNWd9F8DIWU

*(Not: Videoyu hazırlarken grup üyeleriyle meet üzerinden toplanmayı, ekran kaydı alırken kameralarınızın açık olmasını, herkesin kendi bölümünü anlatmasını ve kullanılan hash/CRC algoritmalarından kısaca teorik olarak bahsetmeyi unutmayın.)*

## 2. Proje Dizini

Bu proje Contiki-NG ortamında geliştirilmiştir ve Contiki-NG dizin yapısına uygun olarak tasarlanmıştır.

**Konum:** Proje dosyalarının `Contiki-NG/examples/ota-firmware-update` dizini altına yerleştirilerek derlenmesi/çalıştırılması öngörülmüştür.

## 3. Gerçeklenen Yöntemler, Paket Uzunlukları ve Alınan Önlemler

### 3.1 Gerçeklenen Yöntemler
Projede **Over-The-Air (OTA) Firmware Update** işlemini simüle etmek için **Stop-and-Wait ARQ** mantığıyla çalışan güvenilir bir UDP haberleşmesi kurulmuştur. 
- **İstemci (Client - Düğüm 2):** Mevcut firmware verisini (`firmware_data.h` içindeki byte dizisi) okur, paketlere böler ve sunucuya gönderir. Paketin ulaştığına dair onay (ACK) gelene kadar bir sonraki pakete geçmez.
- **Sunucu (Server - DAG Root):** Gelen UDP paketlerini alır, sağlama (checksum) kontrolü yapar ve hatasızsa **Coffee File System (CFS)** kullanarak diske (flaş belleğe) yazar. Başarılı yazım sonrası ACK döner.
- **Yönlendirici (Router - Düğüm 3):** İstemci ile sunucu arasında köprü görevi görür ve veri paketlerinin çok sekme (multi-hop) ile iletilmesini sağlar.

### 3.2 Paket Uzunlukları ve Yapısı
Firmware verisi **64 byte**'lık bloklar (chunk) halinde gönderilmektedir. Seçilen bu boyut, IEEE 802.15.4 MTU (Maximum Transmission Unit) sınırları içerisinde parçalanmayı (fragmentation) önlemek ve paket kaybı riskini azaltmak için idealdir. 

**Kod Parçası (İstemci tarafı - udp-client.c):**
```c
uint32_t len = 64;
if(current_offset + len > firmware_length) {
    len = firmware_length - current_offset; // Son parça için kalan boyut
}
chunk.length = (uint16_t)len;
memcpy(chunk.payload, firmware_data + current_offset, len);
```

### 3.3 Alınan Önlemler ve Hata Denetimi (Hash/Checksum)

Ağ üzerinde veri iletilirken kayıp ve bozulmaları önlemek için iki aşamalı bir güvenlik ve doğrulama önlemi alınmıştır:

**1. Parça Bazlı Doğrulama (Checksum):**
Her 64 byte'lık paketin verisi basit bir toplama algoritması ile hash'lenir (checksum). Sunucu paketi aldığında aynı işlemi yapar. Eğer uyuşmazlık varsa paket bozuk sayılır, ACK gönderilmez ve istemcinin paketi tekrar göndermesi beklenir.
```c
static uint16_t compute_checksum(const uint8_t *data, uint16_t len) {
  uint16_t sum = 0;
  for(uint16_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum;
}
```

**2. Tüm Dosya Bütünlüğü Doğrulaması (CRC32):**
İstemci tüm dosyayı gönderdikten sonra (EOF paketi), tüm dosyanın CRC32 özet değerini gönderir. Sunucu, CFS üzerinden kendi yazdığı tüm dosyanın CRC32 değerini hesaplar ve istemciden gelen değerle karşılaştırır. Bu işlem, firmware'in cihaza kusursuz aktarıldığını garanti eder.
```c
// İstemci tarafı:
uint32_t full_crc = ota_crc32_buffer(firmware_data, firmware_length);

// Sunucu tarafı:
if(calc_crc == expected_crc) {
    LOG_INFO("Tum imaj dogrulamasi BASARILI (CRC32: 0x%08" PRIX32 ").\n", calc_crc);
}
```

**3. Ağ Tıkanıklığını Önleme (Jitter):**
Paketlerin gönderim zamanına rastgele bir gecikme (jitter) eklenerek ağdaki olası çarpışmalar (collision) ve tıkanıklıklar önlenmiştir.
```c
/* Add some jitter */
etimer_set(&periodic_timer, SEND_INTERVAL - CLOCK_SECOND + (random_rand() % (2 * CLOCK_SECOND)));
```
