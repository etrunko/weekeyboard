/*
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

#ifndef _WKB_IBUS_CONFIG_H_
#define _WKB_IBUS_CONFIG_H_

#include <Eina.h>
#include <Eldbus.h>

#ifdef __cplusplus
extern "C" {
#endif

int         wkb_ibus_config_get_value_int(const char *section, const char *name);
Eina_Bool   wkb_ibus_config_get_value_bool(const char *section, const char *name);
const char *wkb_ibus_config_get_value_string(const char *section, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _WKB_IBUS_CONFIG_H_ */
