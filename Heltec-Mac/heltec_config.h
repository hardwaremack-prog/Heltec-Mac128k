// Which board this firmware is for.
//
//  * Heltec WiFi LoRa 32 V3 (8 MB flash, no PSRAM): Heltec-Mac, a Mac 128K / Plus with 128 KB.
//  * Heltec WiFi LoRa 32 V4 (16 MB flash, 2 MB PSRAM): Heltec-Mac SE, a 1 MB Mac SE (or Plus),
//    using the V4's PSRAM for the Mac's memory. Picked automatically when you compile for
//    "Heltec WiFi LoRa 32(V4)"; also use partitions-v4.csv as partitions.csv (see README).
#pragma once

#if defined(WIFI_LORA_32_V4) && !defined(HELTEC_V4)
#define HELTEC_V4
#endif

#ifdef HELTEC_V4
#define BOARD_NAME      "Heltec-Mac SE"
#define BOARD_HEADER    "HELTEC-MAC SE"
#define BOARD_AP_SSID   "Heltec-MacSE-Setup"
#define BOARD_HOSTNAME  "heltec-macse"
#define BOARD_OLED      " HELTEC SE"
#define BOARD_MODEL     "Heltec WiFi LoRa 32 V4"
#else
#define BOARD_NAME      "Heltec-Mac"
#define BOARD_HEADER    "HELTEC-MAC"
#define BOARD_AP_SSID   "Heltec-Mac-Setup"
#define BOARD_HOSTNAME  "heltec-mac"
#define BOARD_OLED      " HELTEC-MAC"
#define BOARD_MODEL     "Heltec WiFi LoRa 32 V3"
#endif
