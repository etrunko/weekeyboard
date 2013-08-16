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

#include <Ecore.h>
#include <Eldbus.h>

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
   if (!ecore_init())
     {
        printf("Error initializing ecore");
        return 1;
     }

   if (!eldbus_init())
     {
        printf("Error initializing eldbus");
        return 1;
     }

   if (!wkb_ibus_init())
     {
        printf("Error initializing ibus");
        return 1;
     }

   ecore_timer_add(1, _connect_timer, NULL);

   signal(SIGTERM, _finish);
   signal(SIGINT, _finish);

   ecore_main_loop_begin();

   eldbus_shutdown();
   ecore_shutdown();
   return 0;
}
