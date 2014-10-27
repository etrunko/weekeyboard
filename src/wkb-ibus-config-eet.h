/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2014 Jaguar Landrover
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

#ifndef _WKB_IBUS_CONFIG_EET_H_
#define _WKB_IBUS_CONFIG_EET_H_

#include <Eina.h>
#include <Eldbus.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wkb_ibus_config_eet;
struct wkb_config_key;

struct wkb_config_key *wkb_ibus_config_eet_find_key(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name);

Eina_Bool wkb_ibus_config_eet_set_value(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name, Eldbus_Message_Iter *value);
Eina_Bool wkb_ibus_config_eet_get_value(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name, Eldbus_Message_Iter *reply);
Eina_Bool wkb_ibus_config_eet_get_values(struct wkb_ibus_config_eet *config_eet, const char *section, Eldbus_Message_Iter *reply);

void wkb_ibus_config_eet_set_defaults(struct wkb_ibus_config_eet *config_eet);

struct wkb_ibus_config_eet *wkb_ibus_config_eet_new(const char *path, Eldbus_Service_Interface *iface);
void wkb_ibus_config_eet_free(struct wkb_ibus_config_eet *config_eet);

int wkb_ibus_config_eet_init(void);
void wkb_ibus_config_eet_shutdown(void);

int wkb_ibus_config_eet_get_value_int(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name);
Eina_Bool wkb_ibus_config_eet_get_value_bool(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name);
const char *wkb_ibus_config_eet_get_value_string(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name);
char **wkb_ibus_config_eet_get_value_string_list(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name);
void wkb_ibus_config_eet_dump(struct wkb_ibus_config_eet *config_eet);
#ifdef __cplusplus
}
#endif

#endif  /* _WKB_IBUS_CONFIG_EET_H_ */
