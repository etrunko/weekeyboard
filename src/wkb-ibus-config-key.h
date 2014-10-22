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

#ifndef _WKB_IBUS_CONFIG_KEY_H_
#define _WKB_IBUS_CONFIG_KEY_H_

#include <Eina.h>
#include <Eldbus.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wkb_config_key;

struct wkb_config_key *wkb_config_key_int(const char *id, const char *section, void *field);
struct wkb_config_key *wkb_config_key_bool(const char *id, const char *section, void *field);
struct wkb_config_key *wkb_config_key_string(const char *id, const char *section, void *field);
struct wkb_config_key *wkb_config_key_string_list(const char *id, const char *section, void *field);

void wkb_config_key_free(struct wkb_config_key *key);
const char *wkb_config_key_id(struct wkb_config_key *key);
const char *wkb_config_key_section(struct wkb_config_key *key);
const char *wkb_config_key_signature(struct wkb_config_key *key);
Eina_Bool wkb_config_key_set(struct wkb_config_key * key, Eldbus_Message_Iter *iter);
Eina_Bool wkb_config_key_get(struct wkb_config_key *key, Eldbus_Message_Iter *reply);

int         wkb_config_key_get_int(struct wkb_config_key* key);
Eina_Bool   wkb_config_key_get_bool(struct wkb_config_key* key);
const char *wkb_config_key_get_string(struct wkb_config_key* key);
char      **wkb_config_key_get_string_list(struct wkb_config_key *key);

#ifdef __cplusplus
}
#endif

#endif  /* _WKB_IBUS_CONFIG_KEY_H_ */
