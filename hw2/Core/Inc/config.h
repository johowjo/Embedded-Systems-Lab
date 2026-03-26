/*
 * config.h
 *
 *  Created on: 19 feb 2021
 *      Author: Raul Rosa
 */

#ifndef INC_CONFIG_H_
#define INC_CONFIG_H_

/*
 * Local HTTP server: board and PC must be on the same Wi-Fi. Set IPgw to your
 * computer's LAN IPv4 (not 127.0.0.1 — the MCU uses another host on the network).
 * Run scripts/local_http_server.py on the PC, then match PORTgw and HTTP_PATH.
 */
#define SSID "iPhone"            //name of the wifi
#define PSW "88881234"         //Wifi Password
#define IPgw "172.20.10.7"     //LAN IP of the PC running scripts/local_http_server.py (same subnet as the board)
#define PORTgw "8080"          //TCP port your local server listens on
#define HTTP_PATH "/sensor_data"  //request path (must match the server route)
#define deviceid "456"         //value in JSON "device" field
#define TIME_INTERVAL 1000     //ms between POSTs







#endif /* INC_CONFIG_H_ */
