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

#include "wkb-ibus.h"

int _wkb_ibus_log_dom = -1;

#define CHECK_MESSAGE_ERRORS(_msg) \
   do \
     { \
        const char *error, *error_msg; \
        if (eldbus_message_error_get(_msg, &error, &error_msg)) \
          { \
             ERR("Dbus message error: %s: %s", error, error_msg); \
             return; \
          } \
        DBG("Message '%s' with signature '%s'", eldbus_message_member_get(_msg), eldbus_message_signature_get(_msg)); \
     } while (0);

struct _wkb_ibus_service
{
   Eldbus_Service_Interface *interface;

   Eldbus_Signal_Handler *name_acquired;
   Eldbus_Signal_Handler *name_lost;
};

struct _wkb_ibus_context
{
   char *address;
   Eldbus_Connection *conn;
   Ecore_Exe *ibus_daemon;
#if 0
   struct _wkb_ibus_service config;
#else
   Eldbus_Proxy *config;
#endif
   struct _wkb_ibus_service panel;
   int refcount;
   Eina_Bool address_pending;
};

static struct _wkb_ibus_context *ctx = NULL;

static void
_wkb_config_value_changed_cb(void *data, const Eldbus_Message *msg)
{
   const char *section, name;
   Eldbus_Message_Iter *value;

   CHECK_MESSAGE_ERRORS(msg)

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

#if 0
#else
   if (strcmp(bus, IBUS_SERVICE_CONFIG) == 0)
     {
        if (*new_id)
          {
             Eldbus_Object *obj;

             if (ctx->config)
                return;

             ecore_main_loop_glib_integrate();
             obj = eldbus_object_get(ctx->conn, IBUS_SERVICE_CONFIG, IBUS_PATH_CONFIG);
             ctx->config = eldbus_proxy_get(obj, IBUS_INTERFACE_CONFIG);
             eldbus_proxy_signal_handler_add(ctx->config, "ValueChanged", _wkb_config_value_changed_cb, ctx);

             INF("Got config proxy");
          }
        else
          {
             if (!ctx->config)
                return;

             eldbus_proxy_unref(ctx->config);
             ctx->config = NULL;
          }
     }
#endif
}

static void
_wkb_name_acquired_cb(void *data, const Eldbus_Message *msg)
{
   const char *name;

   DBG("NameAcquired");

   CHECK_MESSAGE_ERRORS(msg)

   if (!eldbus_message_arguments_get(msg, "s", &name))
     {
        ERR("Error reading message arguments");
        return;
     }

   if (strcmp(name, IBUS_INTERFACE_PANEL) == 0)
     {
        if (!ctx->panel.interface)
          {
             ctx->panel.interface = wkb_ibus_panel_register(ctx->conn);
             INF("Registering Panel Interface: %s", ctx->panel.interface ? "Success" : "Fail");
          }
        else
          {
             INF("Panel Interface already registered");
          }
     }
#if 0
   else if (strcmp(name, IBUS_INTERFACE_CONFIG) == 0)
     {
        if (!ctx->config.interface)
          {
             ctx->config.interface = wkb_ibus_config_register(ctx->conn);
             INF("Registering Config Interface: %s", ctx->config.interface ? "Success" : "Fail");
          }
        else
          {
             INF("Config Interface already registered");
          }
     }
#endif
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

   CHECK_MESSAGE_ERRORS(msg)

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

   CHECK_MESSAGE_ERRORS(msg)

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
    ctx->ibus_daemon = ecore_exe_run("ibus-daemon -s", NULL);
    if (!ctx->ibus_daemon)
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
        INF("IBus daemon is not running.");
        _wkb_ibus_launch_daemon();
        goto end;
     }
   else if (strstr(exe_data->data, "unknown command") != NULL)
     {
        ERR("ibus command does not support the 'address' argument");
        goto end;
     }

   free(ctx->address);
   ctx->address = strndup(exe_data->data, exe_data->size);

end:
   ecore_idler_add(ecore_exe_free, exe_data->exe);
   ctx->address_pending = EINA_FALSE;
   return ECORE_CALLBACK_DONE;
}


static void
_wkb_ibus_query_address(void)
{
   const char *ibus_addr;
   Ecore_Exe *ibus_exec = NULL;

   /* Check for IBUS_ADDRESS environment variable */
   if ((ibus_addr = getenv("IBUS_ADDRESS")) != NULL)
     {
        DBG("Got IBus address from IBUS_ADDRESS environment variable %s", ibus_addr);
        ctx->address = strdup(ibus_addr);
        return;
     }

   /* Get IBus address by invoking 'ibus address' from command line */
   DBG("Querying IBus address from using 'ibus address' command");
   ibus_exec = ecore_exe_pipe_run("ibus address",
                                  ECORE_EXE_PIPE_READ |
                                  ECORE_EXE_PIPE_READ_LINE_BUFFERED,
                                  NULL);
   if (!ibus_exec)
     {
        ERR("Unable to retrieve IBus address");
        return;
     }

   ctx->address_pending = EINA_TRUE;
   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _wkb_ibus_query_address_cb, NULL);
}

Eina_Bool
wkb_ibus_connect(void)
{
   if (ctx->conn)
      return EINA_TRUE;

   if (!ctx->address)
     {
        INF("IBus address is not set.", ctx->address_pending);
        if (!ctx->address_pending)
            _wkb_ibus_query_address();

        return EINA_FALSE;
    }

   INF("Connecting to IBus at address '%s'", ctx->address);
   ctx->conn = eldbus_address_connection_get(ctx->address);

   if (!ctx->conn)
     {
        ERR("Error connecting to IBus");
        return EINA_FALSE;
     }

   /* Panel */
   eldbus_name_owner_changed_callback_add(ctx->conn,
                                          IBUS_SERVICE_PANEL,
                                          _wkb_name_owner_changed_cb,
                                          ctx, EINA_TRUE);

   ctx->panel.name_acquired = eldbus_signal_handler_add(ctx->conn,
                                                        ELDBUS_FDO_BUS,
                                                        ELDBUS_FDO_PATH,
                                                        IBUS_INTERFACE_PANEL,
                                                        "NameAcquired",
                                                        _wkb_name_acquired_cb,
                                                        ctx);

   ctx->panel.name_lost = eldbus_signal_handler_add(ctx->conn,
                                                    ELDBUS_FDO_BUS,
                                                    ELDBUS_FDO_PATH,
                                                    IBUS_INTERFACE_PANEL,
                                                    "NameLost",
                                                    _wkb_name_lost_cb,
                                                    ctx);

   eldbus_name_request(ctx->conn, IBUS_SERVICE_PANEL,
                       ELDBUS_NAME_REQUEST_FLAG_REPLACE_EXISTING | ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
                       _wkb_name_request_cb, ctx);

   /* Config */
   eldbus_name_owner_changed_callback_add(ctx->conn,
                                          IBUS_SERVICE_CONFIG,
                                          _wkb_name_owner_changed_cb,
                                          ctx, EINA_TRUE);

#if 0
   ctx->config.name_acquired = eldbus_signal_handler_add(ctx->conn,
                                                         ELDBUS_FDO_BUS,
                                                         ELDBUS_FDO_PATH,
                                                         IBUS_INTERFACE_CONFIG,
                                                         "NameAcquired",
                                                         _wkb_name_acquired_cb,
                                                         ctx);

   ctx->config.name_lost = eldbus_signal_handler_add(ctx->conn,
                                                     ELDBUS_FDO_BUS,
                                                     ELDBUS_FDO_PATH,
                                                     IBUS_INTERFACE_CONFIG,
                                                     "NameLost",
                                                     _wkb_name_lost_cb,
                                                     ctx);

   eldbus_name_request(ctx->conn, IBUS_SERVICE_CONFIG,
                       ELDBUS_NAME_REQUEST_FLAG_REPLACE_EXISTING | ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
                       _wkb_name_request_cb, ctx);
#endif
   return EINA_TRUE;
}


int
wkb_ibus_init(void)
{
   if (ctx && ctx->refcount > 0)
      goto end;

   if (!eina_init())
     {
        fprintf(stderr, "Error initializing Eina\n");
        return 0;
     }

   _wkb_ibus_log_dom = eina_log_domain_register("wkb-ibus", EINA_COLOR_LIGHTCYAN);
   if (_wkb_ibus_log_dom < 0)
      {
         EINA_LOG_ERR("Unable to register 'wkb-ibus' log domain");
         eina_shutdown();
         return 0;
      }

   if (!ctx && !(ctx = calloc(1, sizeof(*ctx))))
     {
        ERR("Error calloc\n");
        eina_shutdown();
        return 0;
     }

   _wkb_ibus_query_address();

end:
   return ++ctx->refcount;
}

void
wkb_ibus_shutdown(void)
{
   if (!ctx)
     {
        fprintf(stderr, "Not initialized\n");
        return;
     }

   if (ctx->refcount == 0)
     {
        ERR("Refcount already 0");
        goto end;
     }

   if (--(ctx->refcount) != 0)
      return;

   DBG("Shutting down");
   wkb_ibus_disconnect();

   free(ctx->address);

   if (ctx->ibus_daemon)
     {
        DBG("Terminating ibus-daemon");
        ecore_exe_terminate(ctx->ibus_daemon);
        ecore_exe_free(ctx->ibus_daemon);
     }

end:
   free(ctx);
   ctx = NULL;

   ecore_main_loop_quit();
   DBG("Main loop quit");
}

void
wkb_ibus_disconnect(void)
{
   if (!ctx->conn)
     {
        ERR("Not connected");
        return;
     }

   DBG("Disconnect");

   if (ctx->panel.interface)
     {
        eldbus_name_release(ctx->conn, IBUS_SERVICE_PANEL, _wkb_name_release_cb, ctx);
        eldbus_signal_handler_del(ctx->panel.name_acquired);
        eldbus_signal_handler_del(ctx->panel.name_lost);
        eldbus_service_interface_unregister(ctx->panel.interface);
        ctx->panel.interface = NULL;
     }

   if (ctx->config)
     {
        eldbus_proxy_unref(ctx->config);
        ctx->config = NULL;
     }
#if 0
   if (ctx->config.interface)
     {
        eldbus_name_release(ctx->conn, IBUS_SERVICE_CONFIG, _wkb_name_release_cb, ctx);
        eldbus_signal_handler_del(ctx->config.name_acquired);
        eldbus_signal_handler_del(ctx->config.name_lost);
        eldbus_service_interface_unregister(ctx->config.interface);
        ctx->config.interface = NULL;
     }
#endif

   eldbus_connection_unref(ctx->conn);
}

Eina_Bool
wkb_ibus_is_connected(void)
{
    return ctx->conn != NULL;
}
