/*
  ESP32Ping - Ping library for ESP32
  Copyright (c) 2018 Marian Craciunescu. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include "ESP32Ping.h"


extern "C" void esp_schedule(void) {};
extern "C" void esp_yield(void) {};

PingClass::PingClass() {}

bool PingClass::ping(IPAddress dest) 
{
    _errors = 0;
    _success = 0;

    _avg_time = 0;

    memset(&_options, 0, sizeof(struct ping_option));

    // Time interval between two ping (seconds??)
    _options.coarse_time = 1;
    // Destination machine
    _options.ip = dest;

    // Callbacks
    _options.recv_function = reinterpret_cast<ping_recv_function>(&PingClass::_ping_recv_cb);
    _options.sent_function = NULL; //reinterpret_cast<ping_sent_function>(&_ping_sent_cb);

    
    // Suspend till the process end
    esp_yield(); // ????????? Where should this be placed?
    
    // Let's go!
    ping_start(&_options); // Here we do all the work

    // Returns true if at least 1 ping had a pong response 
    return (_success > 0); //_success variable is changed by the callback function
}

bool PingClass::ping(const char *host) 
{
    IPAddress remote_addr;

    if (WiFi.hostByName(host, remote_addr))
        return ping(remote_addr);

    return false;
}

float PingClass::averageTime() 
{
    return _avg_time;
}

void PingClass::_ping_recv_cb(void *opt, void *resp) 
{
    // Cast the parameters to get some usable info
    ping_resp *ping_resp = reinterpret_cast<struct ping_resp *>(resp);
    //ping_option* ping_opt  = reinterpret_cast<struct ping_option*>(opt);

    // Error or success?
    _errors = ping_resp->timeout_count;
    _success = ping_resp->total_count - ping_resp->timeout_count;
    _avg_time = ping_resp->resp_time;
    

    // Done, return to main function
    esp_schedule();
    
}

byte PingClass::_errors = 0;
byte PingClass::_success = 0;
float PingClass::_avg_time = 0;

PingClass Ping;
