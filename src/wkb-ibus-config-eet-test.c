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
#include <stdio.h>

#include <Eet.h>

#include "wkb-ibus-config-eet.h"
#include "wkb-log.h"

int
main (int argc, char *argv[])
{
   int ret = 1;
   struct wkb_ibus_config_eet *cfg;

   if (!wkb_log_init("eet-test"))
      return 1;

   if (!wkb_ibus_config_eet_init())
     {
        ERR("Error initializing eet");
        goto eet_err;
     }

   cfg = wkb_ibus_config_eet_new("ibus-cfg.eet", NULL);
   wkb_ibus_config_eet_dump(cfg);
   wkb_ibus_config_eet_free(cfg);
   ret = 0;
   wkb_ibus_config_eet_shutdown();

eet_err:
   wkb_log_shutdown();

   return ret;
}
