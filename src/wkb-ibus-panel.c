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

#include <Eina.h>
#include <Eldbus.h>

#include "wkb-ibus.h"
#include "wkb-ibus-defs.h"
#include "wkb-ibus-helper.h"
#include "wkb-log.h"

#define _panel_check_message_errors(_msg) \
   do \
     { \
        const char *error, *error_msg; \
        if (eldbus_message_error_get(_msg, &error, &error_msg)) \
          { \
             ERR("DBus message error: %s: %s", error, error_msg); \
             return NULL; \
          } \
        DBG("Message '%s' with signature '%s'", eldbus_message_member_get(_msg), eldbus_message_signature_get(_msg)); \
     } while (0)

static Eldbus_Message *
_panel_update_preedit_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *text = NULL;
   unsigned int cursor_pos = 0;
   Eina_Bool visible = 0;
   struct wkb_ibus_text *ibus_text;

   _panel_check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "vub", &text, &cursor_pos, &visible))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("text: '%p', cursor_pos: '%d', visible: '%d')", text, cursor_pos, visible);

   ibus_text = wkb_ibus_text_from_message_iter(text);
   DBG("Preedit text = '%s'", ibus_text->text);
   wkb_ibus_text_free(ibus_text);

   return NULL;
}

static Eldbus_Message *
_panel_show_preedit_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_hide_preedit_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_update_auxiliary_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *text = NULL;
   Eina_Bool visible = 0;
   struct wkb_ibus_text *ibus_text;

   _panel_check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "vb", &text, &visible))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("text: '%p', visible: '%d'", text, visible);

   ibus_text = wkb_ibus_text_from_message_iter(text);
   DBG("Auxiliary text = '%s'", ibus_text->text);
   wkb_ibus_text_free(ibus_text);

   return NULL;
}

static Eldbus_Message *
_panel_show_auxiliary_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_hide_auxiliary_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_update_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *table = NULL;
   Eina_Bool visible =  0;
   struct wkb_ibus_lookup_table *ibus_lookup_table;

   _panel_check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "vb", &table, &visible))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("table: '%p', visible: '%d'", table, visible);

   ibus_lookup_table = wkb_ibus_lookup_table_from_message_iter(table);
   wkb_ibus_lookup_table_free(ibus_lookup_table);

   return NULL;
}

static Eldbus_Message *
_panel_show_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_hide_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_cursor_up_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_cursor_down_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_page_up_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_page_down_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_register_properties(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *props = NULL;
   Eina_Array *properties = NULL;

   _panel_check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "v", &props))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("properties: '%p'", props);

   properties = wkb_ibus_properties_from_message_iter(props);
   wkb_ibus_properties_free(properties);

   return NULL;
}

static Eldbus_Message *
_panel_update_property(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *prop = NULL;

   _panel_check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "v", &prop))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("property : '%p'", prop);
   DBG("Property iter signature: %s", eldbus_message_iter_signature_get(prop));

   return NULL;
}

static Eldbus_Message *
_panel_focus_in(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_focus_out(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_set_cursor_location(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   int x = 0, y = 0, w = 0, h = 0;

   _panel_check_message_errors(msg);

   if (!eldbus_message_arguments_get(msg, "iiii", &x, &y, &w, &h))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("x: %d, y: %d, w: %d, h: %d", x, y, w, h);

   return NULL;
}

static Eldbus_Message *
_panel_reset(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_start_setup(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_state_changed(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_hide_language_bar(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static Eldbus_Message *
_panel_show_language_bar(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   _panel_check_message_errors(msg);

   return NULL;
}

static const Eldbus_Method _wkb_ibus_panel_methods[] =
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
   { .member = "UpdatePreeditText",
     .in = ELDBUS_ARGS({"v", "text"}, {"u", "cursor_pos"}, {"b", "visible"}),
     .cb = _panel_update_preedit_text, },

   { .member = "ShowPreeditText",
     .cb = _panel_show_preedit_text, },

   { .member = "HidePreeditText",
     .cb = _panel_hide_preedit_text, },

   { .member = "UpdateAuxiliaryText",
     .in = ELDBUS_ARGS({"v", "text"}, {"b", "visible"}),
     .cb = _panel_update_auxiliary_text, },

   { .member = "ShowAuxiliaryText",
     .cb = _panel_show_auxiliary_text, },

   { .member = "HideAuxiliaryText",
     .cb = _panel_hide_auxiliary_text, },

   { .member = "UpdateLookupTable",
     .in = ELDBUS_ARGS({"v", "table"}, {"b", "visible"}),
     .cb = _panel_update_lookup_table, },

   { .member = "ShowLookupTable",
     .cb = _panel_show_lookup_table, },

   { .member = "HideLookupTable",
     .cb = _panel_hide_lookup_table, },

   { .member = "CursorUpLookupTable",
     .cb = _panel_cursor_up_lookup_table, },

   { .member = "CursorDownLookupTable",
     .cb = _panel_cursor_down_lookup_table, },

   { .member = "PageUpLookupTable",
     .cb = _panel_page_up_lookup_table, },

   { .member = "PageDownLookupTable",
     .cb = _panel_page_down_lookup_table, },

   { .member = "RegisterProperties",
     .in = ELDBUS_ARGS({"v", "props"}),
     .cb = _panel_register_properties, },

   { .member = "UpdateProperty",
     .in = ELDBUS_ARGS({"v", "prop"}),
     .cb = _panel_update_property, },

   { .member = "FocusIn",
     .in = ELDBUS_ARGS({"o", "ic"}),
     .cb = _panel_focus_in, },

   { .member = "FocusOut",
     .in = ELDBUS_ARGS({"o", "ic"}),
     .cb = _panel_focus_out, },

   { .member = "SetCursorLocation",
     .in = ELDBUS_ARGS({"i", "x"}, {"i", "y"}, {"i", "w"}, {"i", "h"}),
     .cb = _panel_set_cursor_location, },

   { .member = "Reset",
     .cb = _panel_reset, },

   { .member = "StartSetup",
     .cb = _panel_start_setup, },

   { .member = "StateChanged",
     .cb = _panel_state_changed, },

   { .member = "HideLanguageBar",
     .cb = _panel_hide_language_bar, },

   { .member = "ShowLanguageBar",
     .cb = _panel_show_language_bar, },

   { NULL },
};

static const Eldbus_Signal _wkb_ibus_panel_signals[] =
{
/* typedef struct _Eldbus_Signal
 * {
 *    const char *name;
 *    const Eldbus_Arg_Info *args;
 *    unsigned int flags;
 * } Eldbus_Signal;
 */
   { .name = "CursorUp", },

   { .name = "CursorDown", },

   { .name = "PageUp", },

   { .name = "PageDown", },

   { .name = "PropertyActivate",
     .args = ELDBUS_ARGS({"s", "prop_name"}, {"i", "prop_state"}),
     .flags = 0, },

   { .name = "PropertyShow",
     .args = ELDBUS_ARGS({"s", "prop_name"}),
     .flags = 0, },

   { .name = "PropertyHide",
     .args = ELDBUS_ARGS({"s", "prop_name"}),
     .flags = 0, },

   { .name = "CandidateClicked",
     .args = ELDBUS_ARGS({"u", "index"}, {"u", "button"}, {"u", "state"}),
     .flags = 0, },

   { NULL },
};

static const Eldbus_Service_Interface_Desc _wkb_ibus_panel_interface =
{
   .interface = IBUS_INTERFACE_PANEL,
   .methods = _wkb_ibus_panel_methods,
   .signals = _wkb_ibus_panel_signals,
};

Eldbus_Service_Interface *
wkb_ibus_panel_register(Eldbus_Connection *conn)
{
   return eldbus_service_interface_register(conn, IBUS_PATH_PANEL, &_wkb_ibus_panel_interface);
}

