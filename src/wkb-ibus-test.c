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

#include "wkb-ibus.h"

#define _GNU_SOURCE
#include <signal.h>

#include <Eina.h>
#include <Eet.h>
#include <Ecore.h>
#include <Eldbus.h>
#include <Efreet.h>

static void
_finish(int foo)
{
   printf("FINISH\n");
   wkb_ibus_shutdown();
}

static Eina_Bool
_connect_timer(void *data)
{
   return !wkb_ibus_connect();
}

int
main (int argc, char *argv[])
{
   int ret = 0;
   if (!eet_init())
     {
        printf("Error initializing eet");
        return 1;
     }

   if (!ecore_init())
     {
        printf("Error initializing ecore");
        ret = 1;
        goto ecore_err;
     }

   if (!eldbus_init())
     {
        printf("Error initializing eldbus");
        ret = 1;
        goto eldbus_err;
     }

   if (!efreet_init())
     {
        printf("Error initializing efreet");
        ret = 1;
        goto efreet_err;
     }

   if (!wkb_ibus_init())
     {
        printf("Error initializing ibus");
        ret = 1;
        goto end;
     }

   ecore_timer_add(1, _connect_timer, NULL);

   signal(SIGTERM, _finish);
   signal(SIGINT, _finish);

   ecore_main_loop_begin();

end:
   efreet_shutdown();

efreet_err:
   eldbus_shutdown();

eldbus_err:
   ecore_shutdown();

ecore_err:
   eet_shutdown();

   return ret;
}
