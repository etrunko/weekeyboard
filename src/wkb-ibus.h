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

struct wl_input_method_context;

/* Events */
extern int WKB_IBUS_CONNECTED;
extern int WKB_IBUS_DISCONNECTED;

int wkb_ibus_init(void);
Eina_Bool wkb_ibus_shutdown(void);

Eina_Bool wkb_ibus_connect(void);
void wkb_ibus_disconnect(void);

Eina_Bool wkb_ibus_is_connected(void);

/* IBus Input Context */
void wkb_ibus_input_context_create(struct wl_input_method_context *wl_ctx);
void wkb_ibus_input_context_destroy(void);
void wkb_ibus_input_context_process_key_event(const char *key);
void wkb_ibus_input_context_set_surrounding_text(const char *text, unsigned int cursor, unsigned int anchor);
unsigned int wkb_ibus_input_context_serial(void);
void wkb_ibus_input_context_set_serial(unsigned int serial);

/* IBus Panel */
Eldbus_Service_Interface * wkb_ibus_panel_register(Eldbus_Connection *conn);

/* IBus Config */
Eldbus_Service_Interface * wkb_ibus_config_register(Eldbus_Connection *conn, const char *path);
void wkb_ibus_config_unregister(void);

#ifdef __cplusplus
}
#endif

#endif /* _WKB_IBUS_H_ */
