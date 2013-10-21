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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Ecore.h>
#include <Eldbus.h>
#include <Efreet.h>

#include "wkb-ibus.h"
#include "wkb-ibus-defs.h"
#include "wkb-log.h"

#define _check_message_errors(_msg) \
   do \
     { \
        const char *error, *error_msg; \
        if (eldbus_message_error_get(_msg, &error, &error_msg)) \
          { \
             ERR("Dbus message error: %s: %s", error, error_msg); \
             return; \
          } \
        DBG("Message '%s' with signature '%s'", eldbus_message_member_get(_msg), eldbus_message_signature_get(_msg)); \
     } while (0)

int WKB_IBUS_CONNECTED = 0;
int WKB_IBUS_DISCONNECTED = 0;

struct _wkb_ibus_context
{
   char *address;
   Ecore_Exe *ibus_daemon;

   Eldbus_Connection *conn;
   Eldbus_Service_Interface *config;
   Eldbus_Service_Interface *panel;
   Eldbus_Signal_Handler *name_acquired;
   Eldbus_Signal_Handler *name_lost;

   int refcount;
   Eina_Bool address_pending;
};

static struct _wkb_ibus_context *wkb_ibus = NULL;

static void
_wkb_config_value_changed_cb(void *data, const Eldbus_Message *msg)
{
   const char *section, *name;
   Eldbus_Message_Iter *value;

   _check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "ssv", &section, &name, &value))
     {
        ERR("Error reading message arguments");
        return;
     }

   DBG("section: '%s', name: '%s', value: '%p", section, name, value);
}

static void
_wkb_name_owner_changed_cb(void *data, const char *bus, const char *old_id, const char *new_id)
{
   DBG("NameOwnerChanged Bus=%s | old=%s | new=%s", bus, old_id, new_id);
}

static void
_wkb_name_acquired_cb(void *data, const Eldbus_Message *msg)
{
   const char *name, *path;

   _check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "s", &name))
     {
        ERR("Error reading message arguments");
        return;
     }

   DBG("NameAcquired: '%s'", name);

   if (strncmp(name, IBUS_INTERFACE_PANEL, strlen(IBUS_INTERFACE_PANEL)) == 0)
     {
        wkb_ibus->panel = wkb_ibus_panel_register(wkb_ibus->conn);
        INF("Registering Panel Interface: %s", wkb_ibus->panel ? "Success" : "Fail");
     }
   else if (strncmp(name, IBUS_INTERFACE_CONFIG, strlen(IBUS_INTERFACE_CONFIG)) == 0)
     {
        path = eina_stringshare_printf("%s/wkb-ibus-cfg.eet", efreet_config_home_get());
        wkb_ibus->config = wkb_ibus_config_register(wkb_ibus->conn, path);
        eina_stringshare_del(path);
        INF("Registering Config Interface: %s", wkb_ibus->config ? "Success" : "Fail");
     }
   else
     {
        WRN("Unexpected name %s", name);
     }
}

static void
_wkb_name_lost_cb(void *data, const Eldbus_Message *msg)
{
   const char *name;

   DBG("NameLost");

   _check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "s", &name))
     {
        ERR("Error reading message arguments");
        return;
     }

   DBG("Name = %s", name);
}

static Eina_Bool
_wkb_ibus_shutdown_idler(void *data)
{
   wkb_ibus_shutdown();
   return ECORE_CALLBACK_CANCEL;
}

static void
_wkb_name_request_cb(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
   const char *error, *error_msg;
   unsigned int reply;

   DBG("NameRequest callback");

   if (eldbus_message_error_get(msg, &error, &error_msg))
     {
        ERR("DBus message error: %s: %s", error, error_msg);
        goto error;
     }

   if (!eldbus_message_arguments_get(msg, "u", &reply))
     {
        ERR("Error reading message arguments");
        goto error;
     }

   if (reply != ELDBUS_NAME_REQUEST_REPLY_PRIMARY_OWNER &&
       reply != ELDBUS_NAME_REQUEST_REPLY_ALREADY_OWNER)
     {
        ERR("Not primary owner: reply=%d", reply);
        goto error;
     }

   return;

error:
   ecore_idler_add(_wkb_ibus_shutdown_idler, NULL);
}

static void
_wkb_name_release_cb(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
   unsigned int reply;

   _check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "u", &reply))
     {
        ERR("Error reading message arguments");
        return;
     }

   if (reply != ELDBUS_NAME_RELEASE_REPLY_RELEASED)
     {
        ERR("Unexpected name release reply: %d", reply);
        return;
     }
}

static void
_wkb_ibus_launch_daemon(void)
{
    DBG("Launching ibus-daemon");
    wkb_ibus->ibus_daemon = ecore_exe_run("ibus-daemon -s", NULL);
    if (!wkb_ibus->ibus_daemon)
      {
         ERR("Error launching ibus-daemon process");
         return;
      }
}

static Eina_Bool
_wkb_ibus_query_address_cb(void *data, int type, void *event)
{
   Ecore_Exe_Event_Data *exe_data = (Ecore_Exe_Event_Data *)event;

   if (strncmp(exe_data->data, "(null)", exe_data->size) == 0)
     {
        WRN("IBus daemon is not running.");
        _wkb_ibus_launch_daemon();
        goto end;
     }
   else if (strstr(exe_data->data, "unknown command") != NULL)
     {
        ERR("ibus command does not support the 'address' argument");
        goto end;
     }

   free(wkb_ibus->address);
   wkb_ibus->address = strndup(exe_data->data, exe_data->size);
   DBG("Got IBus address: '%s'", wkb_ibus->address);

end:
   wkb_ibus->address_pending = EINA_FALSE;
   ecore_idler_add((Ecore_Task_Cb) ecore_exe_free, exe_data->exe);
   return ECORE_CALLBACK_DONE;
}

static void
_wkb_ibus_query_address(void)
{
   Ecore_Exe *ibus_exec = NULL;

   INF("Querying IBus address from 'ibus address' command");
   ibus_exec = ecore_exe_pipe_run("ibus address",
                                  ECORE_EXE_PIPE_READ |
                                  ECORE_EXE_PIPE_READ_LINE_BUFFERED,
                                  NULL);
   if (!ibus_exec)
     {
        ERR("Unable to retrieve IBus address");
        return;
     }

   wkb_ibus->address_pending = EINA_TRUE;
   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _wkb_ibus_query_address_cb, NULL);
}

Eina_Bool
wkb_ibus_connect(void)
{
   if (wkb_ibus->conn)
     {
        INF("Already connected to IBus");
        return EINA_TRUE;
     }

   if (wkb_ibus->address_pending)
     {
        INF("IBus address query in progress");
        return EINA_FALSE;
     }

   if (!wkb_ibus->address)
     {
        char *env_addr = getenv("IBUS_ADDRESS");
        if (!env_addr)
          {
             _wkb_ibus_query_address();
             return EINA_FALSE;
          }

        DBG("Got IBus address from environment variable: '%s'", env_addr);
        wkb_ibus->address = strdup(env_addr);
     }

   INF("Connecting to IBus at address '%s'", wkb_ibus->address);
   wkb_ibus->conn = eldbus_address_connection_get(wkb_ibus->address);

   if (!wkb_ibus->conn)
     {
        ERR("Error connecting to IBus");
        return EINA_FALSE;
     }

   wkb_ibus->name_acquired = eldbus_signal_handler_add(wkb_ibus->conn,
                                                  ELDBUS_FDO_BUS,
                                                  ELDBUS_FDO_PATH,
                                                  ELDBUS_FDO_INTERFACE,
                                                  "NameAcquired",
                                                  _wkb_name_acquired_cb,
                                                  wkb_ibus);

   wkb_ibus->name_lost = eldbus_signal_handler_add(wkb_ibus->conn,
                                              ELDBUS_FDO_BUS,
                                              ELDBUS_FDO_PATH,
                                              ELDBUS_FDO_INTERFACE,
                                              "NameLost",
                                              _wkb_name_lost_cb,
                                              wkb_ibus);

   /* Config */
   eldbus_name_owner_changed_callback_add(wkb_ibus->conn,
                                          IBUS_SERVICE_CONFIG,
                                          _wkb_name_owner_changed_cb,
                                          wkb_ibus, EINA_TRUE);

   DBG("Requesting ownership of " IBUS_SERVICE_CONFIG);
   eldbus_name_request(wkb_ibus->conn, IBUS_SERVICE_CONFIG,
                       ELDBUS_NAME_REQUEST_FLAG_REPLACE_EXISTING | ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
                       _wkb_name_request_cb, wkb_ibus);

   /* Panel */
   eldbus_name_owner_changed_callback_add(wkb_ibus->conn,
                                          IBUS_SERVICE_PANEL,
                                          _wkb_name_owner_changed_cb,
                                          wkb_ibus, EINA_TRUE);

   DBG("Requesting ownership of " IBUS_SERVICE_PANEL);
   eldbus_name_request(wkb_ibus->conn, IBUS_SERVICE_PANEL,
                       ELDBUS_NAME_REQUEST_FLAG_REPLACE_EXISTING | ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
                       _wkb_name_request_cb, wkb_ibus);

   ecore_event_add(WKB_IBUS_CONNECTED, NULL, NULL, NULL);

   return EINA_TRUE;
}

int
wkb_ibus_init(void)
{
   if (wkb_ibus && wkb_ibus->refcount)
      goto end;

   if (!eldbus_init())
     {
        ERR("Error initializing Eldbus");
        goto eldbus_err;
     }

   if (!efreet_init())
     {
        ERR("Error initializing Efreet");
        goto efreet_err;
     }

   if (!wkb_ibus_config_eet_init())
     {
        ERR("Error initializing wkb_config_eet");
        goto eet_err;
     }

   if (!wkb_ibus && !(wkb_ibus = calloc(1, sizeof(*wkb_ibus))))
     {
        ERR("Error calloc");
        goto calloc_err;
     }

   WKB_IBUS_CONNECTED = ecore_event_type_new();
   WKB_IBUS_DISCONNECTED = ecore_event_type_new();

end:
   return ++wkb_ibus->refcount;

calloc_err:
   wkb_ibus_config_eet_shutdown();

eet_err:
   efreet_shutdown();

efreet_err:
   eldbus_shutdown();

eldbus_err:
   return 0;
}

void
wkb_ibus_shutdown(void)
{
   if (!wkb_ibus)
     {
        ERR("Not initialized");
        return;
     }

   if (wkb_ibus->refcount == 0)
     {
        ERR("Refcount already 0");
        goto end;
     }

   if (--(wkb_ibus->refcount) != 0)
      return;

   DBG("Shutting down");
   wkb_ibus_disconnect();

   free(wkb_ibus->address);

   if (wkb_ibus->ibus_daemon)
     {
        DBG("Terminating ibus-daemon");
        ecore_exe_terminate(wkb_ibus->ibus_daemon);
        ecore_exe_free(wkb_ibus->ibus_daemon);
     }

end:
   free(wkb_ibus);
   wkb_ibus = NULL;

   ecore_main_loop_quit();
   DBG("Main loop quit");

   wkb_ibus_config_eet_shutdown();
   efreet_shutdown();
   eldbus_shutdown();
}

void
_wkb_ibus_disconnect_free(void *data, void *func_data)
{
   DBG("Finishing Eldbus Connection");
   eldbus_connection_unref(wkb_ibus->conn);
}

void
wkb_ibus_disconnect(void)
{
   if (!wkb_ibus->conn)
     {
        ERR("Not connected");
        return;
     }

   DBG("Disconnect");

   eldbus_signal_handler_del(wkb_ibus->name_acquired);
   eldbus_signal_handler_del(wkb_ibus->name_lost);

   if (wkb_ibus->panel)
     {
        eldbus_name_release(wkb_ibus->conn, IBUS_SERVICE_PANEL, _wkb_name_release_cb, wkb_ibus);
        eldbus_service_interface_unregister(wkb_ibus->panel);
        wkb_ibus->panel = NULL;
     }

   if (wkb_ibus->config)
     {
        wkb_ibus_config_unregister();
        eldbus_name_release(wkb_ibus->conn, IBUS_SERVICE_CONFIG, _wkb_name_release_cb, wkb_ibus);
        eldbus_service_interface_unregister(wkb_ibus->config);
        wkb_ibus->config = NULL;
     }

   ecore_event_add(WKB_IBUS_DISCONNECTED, NULL, _wkb_ibus_disconnect_free, NULL);
}

Eina_Bool
wkb_ibus_is_connected(void)
{
    return wkb_ibus->conn != NULL;
}
