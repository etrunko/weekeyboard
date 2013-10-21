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
#include "wkb-log.h"

#define _GNU_SOURCE
#include <signal.h>

#include <Eina.h>
#include <Ecore.h>
#include <Eldbus.h>
#include <Efreet.h>

static void
_finish(int foo)
{
   wkb_ibus_shutdown();
}

int
main (int argc, char *argv[])
{
   int ret = 1;

   if (!wkb_log_init("ibus-test"))
      return 1;

   if (!ecore_init())
     {
        ERR("Error initializing ecore");
        goto ecore_err;
     }

   if (!wkb_ibus_init())
     {
        ERR("Error initializing ibus");
        goto ibus_err;
     }

   wkb_ibus_connect();

   signal(SIGTERM, _finish);
   signal(SIGINT, _finish);

   ecore_main_loop_begin();
   ret = 0;

ibus_err:
   ecore_shutdown();

ecore_err:
   wkb_log_shutdown();

   return ret;
}
