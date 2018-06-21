#include "osapi.h"
#include "user_interface.h"
#include "driver/uart.h"
#include "espconn.h"
#include "mem.h"
static os_timer_t ptimer;
static os_timer_t test_timer;

LOCAL struct espconn user_udp_espconn;

const char *ESP8266_MSG = "I'm ESP8266 ";
/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABBBCDDD
 *                A : rf cal
 *                B : at parameters
 *                C : rf init data
 *                D : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 ICACHE_FLASH_ATTR
user_rf_cal_sector_set(void)
{
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        case FLASH_SIZE_64M_MAP_1024_1024:
            rf_cal_sec = 2048 - 5;
            break;
        case FLASH_SIZE_128M_MAP_1024_1024:
            rf_cal_sec = 4096 - 5;
            break;
        default:
            rf_cal_sec = 0;
            break;
    }
    return rf_cal_sec;
}

/*---------------------------------------------------------------------------*/
LOCAL struct espconn ptrespconn;

 /******************************************************************************
  * FunctionName : user_udp_recv_cb
  * Description  : Processing the received udp packet
  * Parameters   : arg -- Additional argument to pass to the callback function
  *                pusrdata -- The received data (or NULL when the connection has been closed!)
  *                length -- The length of received data
  * Returns      : none
 *******************************************************************************/
 LOCAL void ICACHE_FLASH_ATTR
 user_udp_recv_cb(void *arg, char *pusrdata, unsigned short length)
 {
     
     os_printf("recv udp data: %s\n", pusrdata);

 }

 /******************************************************************************
      * FunctionName : user_udp_send
      * Description  : udp send data
      * Parameters  : none
      * Returns      : none
 *******************************************************************************/
 LOCAL void ICACHE_FLASH_ATTR
 user_udp_send(void)
 {
     char DeviceBuffer[40] = {0};
     char hwaddr[6];
     struct ip_info ipconfig;

     const char udp_remote_ip[4] = { 192, 168, 1, 113}; 
     os_memcpy(user_udp_espconn.proto.udp->remote_ip, udp_remote_ip, 4); // ESP8266 udp remote IP need to be set everytime we call espconn_sent
     user_udp_espconn.proto.udp->remote_port = 1112;  // ESP8266 udp remote port need to be set everytime we call espconn_sent

     wifi_get_macaddr(STATION_IF, hwaddr);
 
     os_sprintf(DeviceBuffer, "%s" MACSTR "!" , ESP8266_MSG, MAC2STR(hwaddr));
 
     espconn_sent(&user_udp_espconn, DeviceBuffer, os_strlen(DeviceBuffer));
   
 }
 
 /******************************************************************************
      * FunctionName : user_udp_sent_cb
      * Description  : udp sent successfully
      * Parameters  : arg -- Additional argument to pass to the callback function
      * Returns      : none
 *******************************************************************************/
  LOCAL void ICACHE_FLASH_ATTR
  user_udp_sent_cb(void *arg)
  {
      struct espconn *pespconn = arg;
 
      os_printf("user_udp_send successfully !!!\n");
     
      //disarm timer first
       os_timer_disarm(&test_timer);

      //re-arm timer to check ip
      os_timer_setfn(&test_timer, (os_timer_func_t *)user_udp_send, NULL); // only send next packet after prev packet sent successfully
      os_timer_arm(&test_timer, 1000, 0);
  }


 /******************************************************************************
 * FunctionName : user_check_ip
 * Description  : check whether get ip addr or not
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
 /******************************************************************************
 * FunctionName : user_check_ip
 * Description  : check whether get ip addr or not
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR user_check_ip(void)
{
    struct ip_info ipconfig;

   //disarm timer first
    os_timer_disarm(&test_timer);

   //get ip info of ESP8266 station
    wifi_get_ip_info(STATION_IF, &ipconfig);

    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0)
   {
      os_printf("got ip !!! \r\n");

      wifi_set_broadcast_if(STATIONAP_MODE); // send UDP broadcast from both station and soft-AP interface

      user_udp_espconn.type = ESPCONN_UDP;
      user_udp_espconn.proto.udp = (esp_udp *)os_zalloc(sizeof(esp_udp));
      user_udp_espconn.proto.udp->local_port = espconn_port();  // set a available  port
     
      const char udp_remote_ip[4] = {255, 255, 255, 255}; 
     
      os_memcpy(user_udp_espconn.proto.udp->remote_ip, udp_remote_ip, 4); // ESP8266 udp remote IP
     
      user_udp_espconn.proto.udp->remote_port = 1112;  // ESP8266 udp remote port
     
      espconn_regist_recvcb(&user_udp_espconn, user_udp_recv_cb); // register a udp packet receiving callback
      espconn_regist_sentcb(&user_udp_espconn, user_udp_sent_cb); // register a udp packet sent callback
     
      espconn_create(&user_udp_espconn);   // create udp

      user_udp_send();   // send udp data

    }
   else
   {
        if ((wifi_station_get_connect_status() == STATION_WRONG_PASSWORD ||
                wifi_station_get_connect_status() == STATION_NO_AP_FOUND ||
                wifi_station_get_connect_status() == STATION_CONNECT_FAIL))
        {
         os_printf("connect fail !!! \r\n");
        }
      else
      {
           //re-arm timer to check ip
            os_timer_setfn(&test_timer, (os_timer_func_t *)user_check_ip, NULL);
            os_timer_arm(&test_timer, 100, 0);
        }
    }
}

void ICACHE_FLASH_ATTR user_set_station_config(void){
    struct station_config station_cfg;
    uint8 ssid[] = "Orange-9936";
    uint8 password[] = "56744935";
    wifi_set_opmode(STATION_MODE);
    os_strcpy(station_cfg.ssid,ssid);
    os_strcpy(station_cfg.password,password);
    station_cfg.bssid_set = 1;
    wifi_station_set_config(&station_cfg);    
}


void ICACHE_FLASH_ATTR user_init(void)
{
    gpio_init();
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, 2);
    UART_SetBaudrate(UART1,BIT_RATE_74880);
    UART_SetBaudrate(UART0,BIT_RATE_74880);
    
    os_printf("SDK version:%s\n", system_get_sdk_version());

    // Disable WiFi
    user_set_station_config();

    os_timer_disarm(&ptimer);
    os_timer_setfn(&ptimer, (os_timer_func_t *)user_check_ip, NULL);
    os_timer_arm(&ptimer, 2000, 1);
}
