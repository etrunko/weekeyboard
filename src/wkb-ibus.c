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

#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>

#include "wkb-ibus.h"
#include "wkb-ibus-defs.h"
#include "wkb-ibus-helper.h"
#include "wkb-log.h"
#include "wkb-ibus-config-eet.h"

#include "input-method-client-protocol.h"

#define _check_message_errors(_msg) \
   do \
     { \
        const char *error, *error_msg; \
        if (eldbus_message_error_get(_msg, &error, &error_msg)) \
          { \
             ERR("DBus message error: %s: %s", error, error_msg); \
             return; \
          } \
        DBG("Message '%s' with signature '%s'", eldbus_message_member_get(_msg), eldbus_message_signature_get(_msg)); \
     } while (0)

int WKB_IBUS_CONNECTED = 0;
int WKB_IBUS_DISCONNECTED = 0;

static const char *IBUS_ADDRESS_ENV = "IBUS_ADDRESS";
static const char *IBUS_ADDRESS_CMD = "ibus address";
static const char *IBUS_DAEMON_CMD = "ibus-daemon -s";
static const char *IBUS_DEFAULT_ENGINE = "xkb:us::eng";

/* From ibustypes.h */
static const unsigned int IBUS_CAP_PREEDIT_TEXT     = 1 << 0;
static const unsigned int IBUS_CAP_AUXILIARY_TEXT   = 1 << 1;
static const unsigned int IBUS_CAP_LOOKUP_TABLE     = 1 << 2;
static const unsigned int IBUS_CAP_FOCUS            = 1 << 3;
static const unsigned int IBUS_CAP_PROPERTY         = 1 << 4;
static const unsigned int IBUS_CAP_SURROUNDING_TEXT = 1 << 5;

static const unsigned int IBUS_SHIFT_MASK           = 1 << 0;
static const unsigned int IBUS_RELEASE_MASK         = 1 << 30;

struct wkb_ibus_key
{
   unsigned int code;
   unsigned int sym;
   unsigned int modifiers;
};

struct wkb_ibus_input_context
{
   Eldbus_Pending *pending;
   Eldbus_Proxy *ibus_ctx;
   struct wl_input_method_context *wl_ctx;
   char *preedit;
   unsigned int serial;
   unsigned int cursor;
};

struct _wkb_ibus_context
{
   char *address;

   Ecore_Exe *ibus_daemon;
   Ecore_Event_Handler *add_handle; /* ECORE_EXE_EVENT_ADD */
   Ecore_Event_Handler *data_handle; /* ECORE_EXE_EVENT_DATA */

   Eldbus_Connection *conn;
   Eldbus_Service_Interface *config;
   Eldbus_Service_Interface *panel;
   Eldbus_Signal_Handler *name_acquired;
   Eldbus_Signal_Handler *name_lost;
   Eldbus_Proxy *ibus;

   struct wkb_ibus_input_context *input_ctx;

   int refcount;

   Eina_Bool address_pending :1;
   Eina_Bool shutting_down :1;
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
_wkb_ibus_launch_daemon(void)
{
   DBG("Launching IBus daemon as: '%s'", IBUS_DAEMON_CMD);

   wkb_ibus->ibus_daemon = ecore_exe_run(IBUS_DAEMON_CMD, NULL);
   if (!wkb_ibus->ibus_daemon)
     {
        ERR("Error launching '%s' process", IBUS_DAEMON_CMD);
        return;
     }
}

static Eina_Bool
_wkb_ibus_launch_idler(void *data)
{
   _wkb_ibus_launch_daemon();
   return ECORE_CALLBACK_DONE;
}

static void
_wkb_ibus_query_address(void)
{
   unsigned int flags = ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_READ_LINE_BUFFERED;
   Ecore_Exe *ibus_exe = NULL;

   if (wkb_ibus->address_pending)
      return;

   INF("Querying IBus address with '%s' command", IBUS_ADDRESS_CMD);

   if (!(ibus_exe = ecore_exe_pipe_run(IBUS_ADDRESS_CMD, flags, NULL)))
     {
        ERR("Error spawning '%s' command", IBUS_ADDRESS_CMD);
        return;
     }

   wkb_ibus->address_pending = EINA_TRUE;
}

static Eina_Bool
_wkb_ibus_address_idler(void *data)
{
   _wkb_ibus_query_address();
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_wkb_ibus_exe_add_cb(void *data, int type, void *event_data)
{
   Ecore_Exe_Event_Add *exe_data = (Ecore_Exe_Event_Add *)event_data;
   const char *cmd = NULL;

   if (!exe_data || !exe_data->exe)
     {
        INF("Unable to get information about the process");
        return ECORE_CALLBACK_RENEW;
     }

   cmd = ecore_exe_cmd_get(exe_data->exe);

   if (strncmp(cmd, IBUS_DAEMON_CMD, strlen(IBUS_DAEMON_CMD)))
      return ECORE_CALLBACK_RENEW;

   INF("IBus daemon is up");
   ecore_timer_add(1, _wkb_ibus_address_idler, NULL);

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_wkb_ibus_connect_idler(void *data)
{
   wkb_ibus_connect();
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_wkb_ibus_exe_data_cb(void *data, int type, void *event_data)
{
   Ecore_Exe_Event_Data *exe_data = (Ecore_Exe_Event_Data *) event_data;
   const char *cmd = NULL;

   if (!exe_data || !exe_data->exe)
     {
        INF("Unable to get information about the process");
        return ECORE_CALLBACK_RENEW;
     }

   cmd = ecore_exe_cmd_get(exe_data->exe);

   if (strncmp(cmd, IBUS_ADDRESS_CMD, strlen(IBUS_ADDRESS_CMD)))
      return ECORE_CALLBACK_RENEW;

   if (strncmp(exe_data->data, "(null)", exe_data->size) == 0)
     {
        INF("IBus daemon is not running, spawning");
        ecore_idler_add(_wkb_ibus_launch_idler, NULL);
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
   ecore_idler_add(_wkb_ibus_connect_idler, NULL);

end:
   wkb_ibus->address_pending = EINA_FALSE;
   return ECORE_CALLBACK_RENEW;
}

static void
_wkb_ibus_disconnected_cb(void *data, Eldbus_Connection *conn, void *event_data)
{
   DBG("Lost connection to IBus daemon");
   wkb_ibus_config_unregister();

   free(wkb_ibus->address);
   wkb_ibus->address = NULL;

   eldbus_connection_unref(wkb_ibus->conn);
   wkb_ibus->conn = NULL;

   if (!wkb_ibus->shutting_down)
      ecore_idler_add(_wkb_ibus_connect_idler, NULL);
}

static void
_ibus_global_engine(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
   const char *error, *error_msg;
   Eldbus_Message_Iter *iter, *desc_iter;
   struct wkb_ibus_engine_desc *desc = NULL;

   if (eldbus_message_error_get(msg, &error, &error_msg))
     {
        DBG("DBus message error: %s: %s", error, error_msg);
        goto end;
     }
   else if (!eldbus_message_arguments_get(msg, "v", &iter))
     {
        DBG("Error reading message arguments");
        goto end;
     }

   if (!eldbus_message_iter_arguments_get(iter, "v", &desc_iter))
     {
        DBG("Error retrieving GlobalEngine property");
        goto end;
     }

   desc = wkb_ibus_engine_desc_from_message_iter(desc_iter);
   if (!desc || !desc->name)
     {
        goto end;
     }

   DBG("Global engine is set to '%s'", desc->name);
   free(desc);
   return;

end:
   INF("Global engine is not set, using default: '%s'", IBUS_DEFAULT_ENGINE);
   eldbus_proxy_call(wkb_ibus->ibus, "SetGlobalEngine",
                     NULL, NULL, -1, "s", IBUS_DEFAULT_ENGINE);
}

Eina_Bool
wkb_ibus_connect(void)
{
   Eldbus_Object *obj;

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
        char *env_addr = getenv(IBUS_ADDRESS_ENV);
        if (!env_addr)
          {
             _wkb_ibus_query_address();
             return EINA_FALSE;
          }

        DBG("Got IBus address from '%s' environment variable: '%s'", IBUS_ADDRESS_ENV, env_addr);
        wkb_ibus->address = strdup(env_addr);
     }

   INF("Connecting to IBus at address '%s'", wkb_ibus->address);
   wkb_ibus->conn = eldbus_address_connection_get(wkb_ibus->address);

   if (!wkb_ibus->conn)
     {
        ERR("Error connecting to IBus");
        return EINA_FALSE;
     }

   eldbus_connection_event_callback_add(wkb_ibus->conn,
                                        ELDBUS_CONNECTION_EVENT_DISCONNECTED,
                                        _wkb_ibus_disconnected_cb, NULL);

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

   obj = eldbus_object_get(wkb_ibus->conn, IBUS_SERVICE_IBUS, IBUS_PATH_IBUS);
   wkb_ibus->ibus = eldbus_proxy_get(obj, IBUS_INTERFACE_IBUS);
   eldbus_proxy_property_get(wkb_ibus->ibus, "GlobalEngine", _ibus_global_engine, NULL);

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

   wkb_ibus->add_handle = ecore_event_handler_add(ECORE_EXE_EVENT_ADD, _wkb_ibus_exe_add_cb, NULL);
   wkb_ibus->data_handle = ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _wkb_ibus_exe_data_cb, NULL);

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

static void
_wkb_ibus_shutdown_finish(void)
{
   DBG("Finish");

   ecore_event_handler_del(wkb_ibus->add_handle);
   ecore_event_handler_del(wkb_ibus->data_handle);

   free(wkb_ibus);
   wkb_ibus = NULL;

   ecore_main_loop_quit();

   wkb_ibus_config_eet_shutdown();
   efreet_shutdown();
   eldbus_shutdown();
}

Eina_Bool
wkb_ibus_shutdown(void)
{
   if (!wkb_ibus)
     {
        ERR("Not initialized");
        return EINA_FALSE;
     }

   if (wkb_ibus->shutting_down)
      return EINA_TRUE;

   if (wkb_ibus->refcount == 0)
     {
        ERR("Refcount already 0");
        return EINA_FALSE;
     }

   if (--(wkb_ibus->refcount) != 0)
      return EINA_TRUE;

   DBG("Shutting down");
   wkb_ibus->shutting_down = EINA_TRUE;
   wkb_ibus_disconnect();

   return EINA_TRUE;
}

void
_wkb_ibus_disconnect_free(void *data, void *func_data)
{
   DBG("Finishing Eldbus Connection");
   eldbus_connection_unref(wkb_ibus->conn);
   wkb_ibus->conn = NULL;

   if (wkb_ibus->ibus_daemon)
     {
        DBG("Terminating ibus-daemon");
        ecore_exe_terminate(wkb_ibus->ibus_daemon);
        ecore_exe_free(wkb_ibus->ibus_daemon);
        wkb_ibus->ibus_daemon = NULL;
     }

   if (wkb_ibus->shutting_down)
      _wkb_ibus_shutdown_finish();
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

   /* IBus InputContext proxy */
   wkb_ibus_input_context_destroy();

   /* IBus proxy */
   if (wkb_ibus->ibus)
     {
        eldbus_proxy_unref(wkb_ibus->ibus);
        wkb_ibus->ibus = NULL;
     }

   if (wkb_ibus->panel)
     {
        eldbus_service_interface_unregister(wkb_ibus->panel);
        wkb_ibus->panel = NULL;
     }

   if (wkb_ibus->config)
     {
        wkb_ibus_config_unregister();
        eldbus_service_interface_unregister(wkb_ibus->config);
        wkb_ibus->config = NULL;
     }

   free(wkb_ibus->address);
   wkb_ibus->address = NULL;

   ecore_event_add(WKB_IBUS_DISCONNECTED, NULL, _wkb_ibus_disconnect_free, NULL);
}

Eina_Bool
wkb_ibus_is_connected(void)
{
   return wkb_ibus->conn != NULL;
}

static void
_ibus_input_ctx_commit_text(void *data, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *iter = NULL;
   struct wkb_ibus_text *txt;

   _check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "v", &iter))
     {
        ERR("Error reading message arguments");
        return;
     }

   txt = wkb_ibus_text_from_message_iter(iter);
   DBG("Commit text: '%s'", txt->text);
   wl_input_method_context_commit_string(wkb_ibus->input_ctx->wl_ctx,
                                         wkb_ibus->input_ctx->serial,
                                         txt->text);
   wkb_ibus_text_free(txt);
}

static void
_ibus_input_ctx_forward_key_event(void *data, const Eldbus_Message *msg)
{
   unsigned int val, code, modifiers, state = WL_KEYBOARD_KEY_STATE_PRESSED;

   _check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "uuu", &val, &code, &modifiers))
     {
        ERR("Error reading message arguments");
        return;
     }

   if (modifiers & IBUS_RELEASE_MASK)
      state = WL_KEYBOARD_KEY_STATE_RELEASED;

   wl_input_method_context_keysym(wkb_ibus->input_ctx->wl_ctx,
                                  wkb_ibus->input_ctx->serial,
                                  0, val, state, modifiers);
}

static void
_set_preedit_text(char *text)
{
   wl_input_method_context_preedit_string(wkb_ibus->input_ctx->wl_ctx,
                                          wkb_ibus->input_ctx->serial,
                                          text, text);
}

static void
_ibus_input_ctx_show_preedit_text(void *data, const Eldbus_Message *msg)
{
   _check_message_errors(msg);
   wl_input_method_context_preedit_cursor(wkb_ibus->input_ctx->wl_ctx,
                                          wkb_ibus->input_ctx->cursor);
   _set_preedit_text(wkb_ibus->input_ctx->preedit);
}

static void
_ibus_input_ctx_hide_preedit_text(void *data, const Eldbus_Message *msg)
{
   _check_message_errors(msg);
   _set_preedit_text("");
}

static void
_ibus_input_ctx_update_preedit_text(void *data, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *iter = NULL;
   unsigned int cursor;
   struct wkb_ibus_text *txt;
   Eina_Bool visible;

   _check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "vub", &iter, &cursor, &visible))
     {
        ERR("Error reading message arguments");
        return;
     }

   if (wkb_ibus->input_ctx->preedit)
     {
        free(wkb_ibus->input_ctx->preedit);
        wkb_ibus->input_ctx->preedit = NULL;
     }

   txt = wkb_ibus_text_from_message_iter(iter);
   DBG("Preedit text: '%s', Cursor: '%d'", txt->text, cursor);
   wkb_ibus->input_ctx->preedit = strdup(txt->text);
   wkb_ibus->input_ctx->cursor = cursor;

   if (!visible)
     {
        _set_preedit_text("");
        return;
     }

   wl_input_method_context_preedit_cursor(wkb_ibus->input_ctx->wl_ctx, cursor);
   _set_preedit_text(wkb_ibus->input_ctx->preedit);
}

static void
_ibus_input_ctx_create(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
   const char *obj_path;
   Eldbus_Object *obj;
   Eldbus_Proxy *ibus_ctx;
   unsigned int capabilities = IBUS_CAP_FOCUS | IBUS_CAP_PREEDIT_TEXT | IBUS_CAP_SURROUNDING_TEXT;

   _check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "o", &obj_path))
     {
        ERR("Error reading message arguments");
        goto end;
     }

   DBG("Got new IBus input context: '%s'", obj_path);

   obj = eldbus_object_get(wkb_ibus->conn, IBUS_SERVICE_IBUS, obj_path);
   wkb_ibus->input_ctx->ibus_ctx = ibus_ctx = eldbus_proxy_get(obj, IBUS_INTERFACE_INPUT_CONTEXT);

   eldbus_proxy_signal_handler_add(ibus_ctx, "CommitText", _ibus_input_ctx_commit_text, NULL);
   eldbus_proxy_signal_handler_add(ibus_ctx, "ForwardKeyEvent", _ibus_input_ctx_forward_key_event, NULL);
   eldbus_proxy_signal_handler_add(ibus_ctx, "UpdatePreeditText", _ibus_input_ctx_update_preedit_text, NULL);
   eldbus_proxy_signal_handler_add(ibus_ctx, "ShowPreeditText", _ibus_input_ctx_show_preedit_text, NULL);
   eldbus_proxy_signal_handler_add(ibus_ctx, "HidePreeditText", _ibus_input_ctx_hide_preedit_text, NULL);

   eldbus_proxy_call(ibus_ctx, "FocusIn", NULL, NULL, -1, "");
   eldbus_proxy_call(ibus_ctx, "SetCapabilities", NULL, NULL, -1, "u", capabilities);

end:
   wkb_ibus->input_ctx->pending = NULL;
}

void
wkb_ibus_input_context_create(struct wl_input_method_context *wl_ctx)
{
   const char *ctx_name = "wayland";

   if (!wkb_ibus)
       return;

   if (wkb_ibus->input_ctx)
     {
        WRN("Input context already exists");
        wkb_ibus_input_context_destroy();
     }

   wkb_ibus->input_ctx = calloc(1, sizeof(*(wkb_ibus->input_ctx)));
   wkb_ibus->input_ctx->wl_ctx = wl_ctx;

   if (!wkb_ibus->conn)
     {
        ERR("Not connected");
        return;
     }

   if (!wkb_ibus->ibus)
     {
        ERR("No IBus proxy");
        return;
     }

   wkb_ibus->input_ctx->pending = eldbus_proxy_call(wkb_ibus->ibus,
                                                    "CreateInputContext",
                                                    _ibus_input_ctx_create,
                                                    NULL, -1, "s", ctx_name);
}

void
wkb_ibus_input_context_destroy(void)
{
   if (!wkb_ibus || !wkb_ibus->input_ctx)
      return;

   if (wkb_ibus->input_ctx->pending)
      eldbus_pending_cancel(wkb_ibus->input_ctx->pending);

   if (wkb_ibus->input_ctx->ibus_ctx)
     {
        eldbus_proxy_call(wkb_ibus->input_ctx->ibus_ctx, "FocusOut", NULL, NULL, -1, "");
        eldbus_proxy_unref(wkb_ibus->input_ctx->ibus_ctx);
     }

   free(wkb_ibus->input_ctx->preedit);

end:
   free(wkb_ibus->input_ctx);
   wkb_ibus->input_ctx = NULL;
}

static void
_ibus_input_ctx_key_press(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
   struct wkb_ibus_key *key = (struct wkb_ibus_key *) data;
   Eina_Bool ret = EINA_FALSE;

   if (msg)
     {
        _check_message_errors(msg);

        if (!eldbus_message_arguments_get(msg, "b", &ret))
           ERR("Error reading message arguments");
     }

   if (!ret)
     {
        INF("Key press was not handled by IBus");
        if (key->modifiers)
           wl_input_method_context_modifiers(wkb_ibus->input_ctx->wl_ctx,
                                             wkb_ibus->input_ctx->serial,
                                             key->modifiers, 0, 0, 0);

        wl_input_method_context_key(wkb_ibus->input_ctx->wl_ctx,
                                    wkb_ibus->input_ctx->serial,
                                    0, key->code-8, WL_KEYBOARD_KEY_STATE_PRESSED);
     }
}

static void
_ibus_input_ctx_key_release(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
   struct wkb_ibus_key *key = (struct wkb_ibus_key *) data;
   Eina_Bool ret = EINA_FALSE;

   if (msg)
     {
        _check_message_errors(msg);

        if (!eldbus_message_arguments_get(msg, "b", &ret))
           ERR("Error reading message arguments");
     }

   if (!ret)
     {
        INF("Key release was not handled by IBus");
        wl_input_method_context_key(wkb_ibus->input_ctx->wl_ctx,
                                    wkb_ibus->input_ctx->serial,
                                    0, key->code-8, WL_KEYBOARD_KEY_STATE_RELEASED);

        if (key->modifiers)
           wl_input_method_context_modifiers(wkb_ibus->input_ctx->wl_ctx,
                                             wkb_ibus->input_ctx->serial,
                                             0, 0, 0, 0);

     }
}

static void
_wkb_ibus_key_from_str(const char *key_str, struct wkb_ibus_key *key)
{
   key->modifiers = 0;

   if (!strcmp(key_str, "shift"))
     {
        key->sym = XKB_KEY_Shift_L;
        key->code = KEY_LEFTSHIFT;
        return;
     }

   if (!strcmp(key_str, "backspace"))
     {
        key->sym = XKB_KEY_BackSpace;
        key->code = KEY_BACKSPACE;
        return;
     }

   if (!strcmp(key_str, "enter"))
     {
        key->sym = XKB_KEY_Return;
        key->code = KEY_ENTER;
        return;
     }

   if (!strcmp(key_str, "space"))
     {
        key->sym = XKB_KEY_space;
        key->code = KEY_SPACE;
        return;
     }

   key->sym = *key_str;

#define CASE_KEY_SYM(_sym, _alt, _code) \
   case XKB_KEY_ ## _alt: \
      key->modifiers = 1; \
   case XKB_KEY_ ## _sym: \
      key->code = KEY_ ## _code; \
      return

#define CASE_NUMBER(_num, _alt) \
   CASE_KEY_SYM(_num, _alt, _num)

#define CASE_LETTER(_low, _up) \
   CASE_KEY_SYM(_low, _up, _up)

   switch(key->sym)
     {
      CASE_KEY_SYM(grave, asciitilde, GRAVE);
      CASE_NUMBER(1, exclam);
      CASE_NUMBER(2, at);
      CASE_NUMBER(3, numbersign);
      CASE_NUMBER(4, dollar);
      CASE_NUMBER(5, percent);
      CASE_NUMBER(6, asciicircum);
      CASE_NUMBER(7, ampersand);
      CASE_NUMBER(8, asterisk);
      CASE_NUMBER(9, parenleft);
      CASE_NUMBER(0, parenright);
      CASE_KEY_SYM(minus, underscore, MINUS);
      CASE_KEY_SYM(equal, plus, EQUAL);

      CASE_LETTER(q, Q);
      CASE_LETTER(w, W);
      CASE_LETTER(e, E);
      CASE_LETTER(r, R);
      CASE_LETTER(t, T);
      CASE_LETTER(y, Y);
      CASE_LETTER(u, U);
      CASE_LETTER(i, I);
      CASE_LETTER(o, O);
      CASE_LETTER(p, P);
      CASE_KEY_SYM(bracketleft, braceleft, LEFTBRACE);
      CASE_KEY_SYM(bracketright, braceright, RIGHTBRACE);
      CASE_KEY_SYM(backslash, bar, BACKSLASH);

      CASE_LETTER(a, A);
      CASE_LETTER(s, S);
      CASE_LETTER(d, D);
      CASE_LETTER(f, F);
      CASE_LETTER(g, G);
      CASE_LETTER(h, H);
      CASE_LETTER(j, J);
      CASE_LETTER(k, K);
      CASE_LETTER(l, L);
      CASE_KEY_SYM(semicolon, colon, SEMICOLON);
      CASE_KEY_SYM(apostrophe, quotedbl, APOSTROPHE);

      CASE_LETTER(z, Z);
      CASE_LETTER(x, X);
      CASE_LETTER(c, C);
      CASE_LETTER(v, V);
      CASE_LETTER(b, B);
      CASE_LETTER(n, N);
      CASE_LETTER(m, M);
      CASE_KEY_SYM(comma, less, COMMA);
      CASE_KEY_SYM(period, greater, DOT);
      CASE_KEY_SYM(slash, question, SLASH);

#if 0
      CASE_KEY_SYM(yen, ); /* '¥' */
      CASE_KEY_SYM(EuroSign, ; /* '€' */
      CASE_KEY_SYM(WonSign, ); /* '₩' */
      CASE_KEY_SYM(cent, ); /* '¢' */
      CASE_KEY_SYM(degree, ); /* '°' */
      CASE_KEY_SYM(periodcentered, ); /* '˙' */
      CASE_KEY_SYM(registered, ); /* '®' */
      CASE_KEY_SYM(copyright, ); /* '©' */
      CASE_KEY_SYM(questiondown, ); /* '¿' */
#endif

      default:
         ERR("Unexpected key '%s'", key_str);
         key->sym = XKB_KEY_NoSymbol;
         key->code = KEY_RESERVED;
     }

#undef CASE_SYM
#undef CASE_NUMBER
#undef CASE_LETTER
}

void
wkb_ibus_input_context_process_key_event(const char *key_str)
{
   static struct wkb_ibus_key key = { 0 };

   if (!wkb_ibus || !wkb_ibus->input_ctx)
      return;

   _wkb_ibus_key_from_str(key_str, &key);

   if (key.code == KEY_RESERVED)
     {
        ERR("Unexpected key '%s'", key_str);
        return;
     }

   key.code += 8;

   INF("Process key event with '%s'", key_str);

   /* Key press */
   if (!wkb_ibus->input_ctx->ibus_ctx)
      _ibus_input_ctx_key_press(&key, NULL, NULL);
   else
      eldbus_proxy_call(wkb_ibus->input_ctx->ibus_ctx, "ProcessKeyEvent",
                        _ibus_input_ctx_key_press, &key,
                        -1, "uuu", key.sym, key.code, key.modifiers);

   if (key.sym == XKB_KEY_Shift_L)
      key.modifiers = IBUS_SHIFT_MASK;

   /* Key release */
   if (!wkb_ibus->input_ctx->ibus_ctx)
      _ibus_input_ctx_key_release(&key, NULL, NULL);
   else
      eldbus_proxy_call(wkb_ibus->input_ctx->ibus_ctx, "ProcessKeyEvent",
                        _ibus_input_ctx_key_release, &key,
                        -1, "uuu", key.sym, key.code, key.modifiers | IBUS_RELEASE_MASK);
}

static void
_ibus_input_ctx_set_surrounding_text(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
   struct wkb_ibus_text *txt = (struct wkb_ibus_text *) data;
   wkb_ibus_text_free(txt);
}

void
wkb_ibus_input_context_set_surrounding_text(const char *text, unsigned int cursor, unsigned int anchor)
{
   Eldbus_Message *msg;
   Eldbus_Message_Iter *iter;
   struct wkb_ibus_text *txt;

   if (!wkb_ibus || !wkb_ibus->input_ctx || !wkb_ibus->input_ctx->ibus_ctx)
      return;

   txt = wkb_ibus_text_from_string(text);

   msg = eldbus_proxy_method_call_new(wkb_ibus->input_ctx->ibus_ctx, "SetSurroundingText");
   iter = eldbus_message_iter_get(msg);
   wkb_ibus_iter_append_text(iter, txt);
   eldbus_message_iter_basic_append(iter, 'u', cursor);
   eldbus_message_iter_basic_append(iter, 'u', anchor);
   eldbus_proxy_send(wkb_ibus->input_ctx->ibus_ctx, msg,
                     _ibus_input_ctx_set_surrounding_text,
                     txt, -1);
}

unsigned int
wkb_ibus_input_context_serial(void)
{
   if (!wkb_ibus || !wkb_ibus->input_ctx)
      return 0;

   return wkb_ibus->input_ctx->serial;
}

void
wkb_ibus_input_context_set_serial(unsigned int serial)
{
   if (!wkb_ibus || !wkb_ibus->input_ctx)
      return;

   wkb_ibus->input_ctx->serial = serial;
}
