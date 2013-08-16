/*
 * Copyright © 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _WKB_IBUS_H_
#define _WKB_IBUS_H_

#include <Eina.h>
#include <Eldbus.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int _wkb_ibus_log_dom;
#define DBG(...)      EINA_LOG_DOM_DBG(_wkb_ibus_log_dom, __VA_ARGS__)
#define INF(...)      EINA_LOG_DOM_INFO(_wkb_ibus_log_dom, __VA_ARGS__)
#define WRN(...)      EINA_LOG_DOM_WARN(_wkb_ibus_log_dom, __VA_ARGS__)
#define ERR(...)      EINA_LOG_DOM_ERR(_wkb_ibus_log_dom, __VA_ARGS__)
#define CRITICAL(...) EINA_LOG_DOM_CRIT(_wkb_ibus_log_dom, __VA_ARGS__)


/* from ibusshare.h */
#define IBUS_SERVICE_IBUS       "org.freedesktop.IBus"
#define IBUS_SERVICE_PANEL      "org.freedesktop.IBus.Panel"
#define IBUS_SERVICE_CONFIG     "org.freedesktop.IBus.Config"

#define IBUS_PATH_IBUS          "/org/freedesktop/IBus"
#define IBUS_PATH_PANEL         "/org/freedesktop/IBus/Panel"
#define IBUS_PATH_CONFIG        "/org/freedesktop/IBus/Config"

#define IBUS_INTERFACE_IBUS     "org.freedesktop.IBus"
#define IBUS_INTERFACE_PANEL    "org.freedesktop.IBus.Panel"
#define IBUS_INTERFACE_CONFIG   "org.freedesktop.IBus.Config"


int wkb_ibus_init(void);
void wkb_ibus_shutdown(void);

Eina_Bool wkb_ibus_connect(void);
void wkb_ibus_disconnect(void);

Eina_Bool wkb_ibus_is_connected(void);

/* Panel */
Eldbus_Service_Interface * wkb_ibus_panel_register(Eldbus_Connection *conn);

/* Config */
#if 0
Eldbus_Service_Interface * wkb_ibus_config_register(Eldbus_Connection *conn);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WKB_IBUS_H_ */
