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

#include <Eldbus.h>

#include "wkb-ibus.h"

#define CONFIG_CHECK_MESSAGE_ERRORS(_msg) \
   do \
     { \
        const char *error, *error_msg; \
        if (eldbus_message_error_get(_msg, &error, &error_msg)) \
          { \
             ERR("DBus message error: %s: %s", error, error_msg); \
             return NULL; \
          } \
        DBG("Message '%s' with signature '%s'", eldbus_message_member_get(_msg), eldbus_message_signature_get(_msg)); \
     } while (0);

static Eldbus_Message *
_config_set_value(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   const char *section, *name;
   Eldbus_Message_Iter *value;

   CONFIG_CHECK_MESSAGE_ERRORS(msg)

   if (!eldbus_message_arguments_get(msg, "ssv", &section, &name, &value))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("section: '%s', name: '%s', value: '%p'", section, name, value);

   return NULL;
}

static Eldbus_Message *
_config_get_value(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   const char *section, *name;

   CONFIG_CHECK_MESSAGE_ERRORS(msg)

   if (!eldbus_message_arguments_get(msg, "ss", &section, &name))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("section: '%s', name: '%s'", section, name);

   return NULL;
}

static Eldbus_Message *
_config_get_values(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   const char *section;

   CONFIG_CHECK_MESSAGE_ERRORS(msg)

   if (!eldbus_message_arguments_get(msg, "s", &section))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("section: '%s'", section);

   return NULL;
}

static Eldbus_Message *
_config_unset_value(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   const char *section, *name;

   CONFIG_CHECK_MESSAGE_ERRORS(msg)

   if (!eldbus_message_arguments_get(msg, "ss", &section, &name))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("section: '%s', name: '%s'", section, name);

   return NULL;
}

static const Eldbus_Method _wkb_ibus_config_methods[] =
{
/* typedef struct _Eldbus_Method
 * {
 *    const char *member;
 *    const Eldbus_Arg_Info *in;
 *    const Eldbus_Arg_Info *out;
 *    Eldbus_Method_Cb cb;
 *    unsigned int flags;
 * } Eldbus_Method;
 */
   { .member = "SetValue",
     .in = ELDBUS_ARGS({"s", "section"}, {"s", "name"}, {"v", "value"}),
     .cb = _config_set_value, },

   { .member = "GetValue",
     .in = ELDBUS_ARGS({"s", "section"}, {"s", "name"}),
     .out = ELDBUS_ARGS({"v", "value"}),
     .cb = _config_get_value, },

   { .member = "GetValues",
     .in = ELDBUS_ARGS({"s", "section"}),
     .out = ELDBUS_ARGS({"a{sv}", "values"}),
     .cb = _config_get_values, },

   { .member = "UnsetValue",
     .in = ELDBUS_ARGS({"s", "section"}, {"s", "name"}),
     .cb = _config_unset_value, },

   { NULL },
};

static const Eldbus_Signal _wkb_ibus_config_signals[] =
{
/* typedef struct _Eldbus_Signal
 * {
 *    const char *name;
 *    const Eldbus_Arg_Info *args;
 *    unsigned int flags;
 * } Eldbus_Signal;
 */
   { .name = "ValueChanged",
     .args = ELDBUS_ARGS({"s", "section"}, {"s", "name"}, {"v", "value"}),
     .flags = 0, },

   { NULL },
};

static const Eldbus_Service_Interface_Desc _wkb_ibus_config_interface =
{
   .interface = IBUS_INTERFACE_CONFIG,
   .methods = _wkb_ibus_config_methods,
   .signals = _wkb_ibus_config_signals,
};

Eldbus_Service_Interface *
wkb_ibus_config_register(Eldbus_Connection *conn)
{
   return eldbus_service_interface_register(conn, IBUS_PATH_CONFIG, &_wkb_ibus_config_interface);
}

