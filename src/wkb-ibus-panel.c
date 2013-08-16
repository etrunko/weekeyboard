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

#define PANEL_CHECK_MESSAGE_ERRORS(_msg) \
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

static Eina_Array *_get_properties_from_message_iter(Eldbus_Message_Iter *iter);

struct _ibus_serializable
{
   /*
    * All messages sent by IBus will start with the sa{sv} signature, but
    * those fields don't seem useful for us, this struct is used to help
    * on deserializing those fields
    */
   char *text;
   Eldbus_Message_Iter *variant;
};

struct _ibus_attr
{
   unsigned int type;
   unsigned int value;
   unsigned int start_idx;
   unsigned int end_idx;
};

struct _ibus_text
{
   char *text;
   Eina_Array *attrs;
};

struct _ibus_lookup_table
{
   unsigned int page_size;
   unsigned int cursor_pos;
   Eina_Bool cursor_visible;
   Eina_Bool round;
   int orientation;
   Eina_Array *candidates;
   Eina_Array *labels;
};

struct _ibus_property
{
   char *key;
   char *icon;
   struct _ibus_text *label;
   struct _ibus_text *symbol;
   struct _ibus_text *tooltip;
   Eina_Bool sensitive;
   Eina_Bool visible;
   unsigned int type;
   unsigned int state;
   Eina_Array *sub_properties;
};

static struct _ibus_attr *
_get_attr_from_message_iter(Eldbus_Message_Iter *iter)
{
   struct _ibus_attr *attr = calloc(1, sizeof(*attr));

   EINA_SAFETY_ON_NULL_RETURN_VAL(attr, NULL);

   DBG("Attribute iter signature '%s'", eldbus_message_iter_signature_get(iter));

   if (!eldbus_message_iter_arguments_get(iter, "uuuu", &attr->type,
                                          &attr->value, &attr->start_idx,
                                          &attr->end_idx))
     {
        ERR("Error deserializing IBusAttribute");
        free(attr);
        attr = NULL;
     }

   return attr;
}

static void
_free_eina_array(Eina_Array *array, void (* free_func)(void *))
{
   if (!array)
      return;

   while (eina_array_count(array))
      free_func(eina_array_pop(array));

   eina_array_free(array);
}

static void
_free_text(struct _ibus_text *text)
{
   if (!text)
      return;

   _free_eina_array(text->attrs, free);
   free(text->text);
   free(text);
}

static struct _ibus_text *
_get_text_from_message_iter(Eldbus_Message_Iter *iter)
{
   struct _ibus_serializable ignore = { 0 };
   struct _ibus_text *text = calloc(1, sizeof(*text));
   struct _ibus_attr *attr = NULL;
   Eldbus_Message_Iter *attrs = NULL, *a = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(text, NULL);

   DBG("Text iter signature '%s'", eldbus_message_iter_signature_get(iter));

   if (!eldbus_message_iter_arguments_get(iter, "(sa{sv}sv)", &ignore.text,
                                          &ignore.variant, &text->text, &attrs))
     {
        ERR("Error deserializing IBusText");
        free(text);
        text = NULL;
        goto end;
     }

   /* Check for attributes */
   if (attrs == NULL)
     {
        INF("Text has no attributes");
        goto end;
     }

   while (eldbus_message_iter_get_and_next(attrs, 'v', &a))
     {
        if (!text->attrs)
           text->attrs = eina_array_new(10);

        if (!(attr = _get_attr_from_message_iter(a)))
          {
             _free_text(text);
             text = NULL;
             goto end;
          }

        eina_array_push(text->attrs, attr);
     }

end:
   return text;
}

static void
_free_lookup_table(struct _ibus_lookup_table *table)
{
   if (!table)
      return;

   _free_eina_array(table->candidates, _free_text);
   _free_eina_array(table->labels, _free_text);
   free(table);
}

static struct _ibus_lookup_table *
_get_lookup_table_from_message_iter(Eldbus_Message_Iter *iter)
{
   struct _ibus_serializable ignore = { 0 };
   struct _ibus_lookup_table *table = calloc(1, sizeof(*table));
   struct _ibus_text *text = NULL;
   Eldbus_Message_Iter *candidates = NULL, *labels = NULL, *t = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(table, NULL);

   DBG("LookupTable iter signature '%s'", eldbus_message_iter_signature_get(iter));

   if (!eldbus_message_iter_arguments_get(iter, "(sa{sv}uubbiavav)",
                                          &ignore.text, &ignore.variant,
                                          &table->page_size, &table->cursor_pos,
                                          &table->cursor_visible, &table->round,
                                          &table->orientation, &candidates,
                                          &labels))
     {
        ERR("Error deserializing IBusLookupTable");
        free(table);
        table = NULL;
        goto end;
     }

   DBG("Lookup table:");
   DBG("\tPage size.......: '%d'", table->page_size);
   DBG("\tCursor position.: '%d'", table->cursor_pos);
   DBG("\tCursor visible..: '%d'", table->cursor_visible);
   DBG("\tRound...........: '%d'", table->round);
   DBG("\tOrientation.....: '%d'", table->orientation);
   DBG("\tCandidates......: '%p'", candidates);
   DBG("\tLabels..........: '%p'", labels);

   if (!candidates)
     {
        INF("Lookup table has no candidates");
        goto labels;
     }

   while (eldbus_message_iter_get_and_next(candidates, 'v', &t))
     {
        if (!table->candidates)
           table->candidates = eina_array_new(10);

        if (!(text = _get_text_from_message_iter(t)))
          {
             _free_lookup_table(table);
             table = NULL;
             goto end;
          }

        DBG("Appending new candidate %s", text->text);
        eina_array_push(table->candidates, text);
     }

labels:
   if (!labels)
     {
        INF("Lookup table has no labels");
        goto end;
     }

   while (eldbus_message_iter_get_and_next(labels, 'v', &t))
     {
        if (!table->labels)
           table->labels = eina_array_new(10);

        if (!(text = _get_text_from_message_iter(t)))
          {
             _free_lookup_table(table);
             table = NULL;
             goto end;
          }

        DBG("Appending new label %s", text->text);
        eina_array_push(table->labels, text);
     }

end:
   return table;
}

static void
_free_property(struct _ibus_property *property)
{
   if (!property)
      return;

   free(property->key);
   free(property->icon);
   _free_text(property->label);
   _free_text(property->symbol);
   _free_text(property->tooltip);
   _free_eina_array(property->sub_properties, _free_property);
   free(property);
}

static struct _ibus_property *
_get_property_from_message_iter(Eldbus_Message_Iter *iter)
{
   struct _ibus_serializable ignore = { 0 };
   struct _ibus_property *prop = calloc(1, sizeof(*prop));
   Eldbus_Message_Iter *label = NULL, *symbol = NULL, *tooltip = NULL, *sub_props = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(prop, NULL);

   DBG("Property iter signature '%s'", eldbus_message_iter_signature_get(iter));

   if (!eldbus_message_iter_arguments_get(iter, "(sa{sv}suvsvbbuvv)",
                                          &ignore.text, &ignore.variant,
                                          &prop->key, &prop->type,
                                          &label, &prop->icon, &tooltip,
                                          &prop->sensitive, &prop->visible,
                                          &prop->state, &sub_props, &symbol))
     {
        ERR("Error deserializing IBusProperty");
        free(prop);
        prop = NULL;
        goto end;
     }

   DBG("Property :");
   DBG("\tKey.............: '%s'", prop->key);
   DBG("\tType............: '%d'", prop->type);
   DBG("\tLabel...........: '%p'", label);
   DBG("\tIcon............: '%s'", prop->icon);
   DBG("\tTooltip.........: '%p'", tooltip);
   DBG("\tSensitive.......: '%d'", prop->sensitive);
   DBG("\tVisible.........: '%d'", prop->visible);
   DBG("\tState...........: '%d'", prop->state);
   DBG("\tSub Properties..: '%p'", sub_props);
   DBG("\tSymbol..........: '%p'", symbol);

   if (!label)
     {
        INF("Property has no label");
        goto symbol;
     }

   if (!(prop->label = _get_text_from_message_iter(label)))
     {
        _free_property(prop);
        prop = NULL;
        goto end;
     }

symbol:
   if (!symbol)
     {
        INF("Property has no symbol");
        goto tooltip;
     }

   if (!(prop->symbol = _get_text_from_message_iter(symbol)))
     {
        _free_property(prop);
        prop = NULL;
        goto end;
     }

tooltip:
   if (!tooltip)
     {
        INF("Property has no tooltip");
        goto sub_props;
     }

   if (!(prop->tooltip = _get_text_from_message_iter(tooltip)))
     {
        _free_property(prop);
        prop = NULL;
        goto end;
     }

sub_props:
   if (!sub_props)
     {
        INF("Property has no sub properties");
        goto end;
     }

   prop->sub_properties = _get_properties_from_message_iter(sub_props);

end:
   return prop;
}

static Eina_Array *
_get_properties_from_message_iter(Eldbus_Message_Iter *iter)
{
   Eina_Array *properties = NULL;
   Eldbus_Message_Iter *props = NULL, *prop = NULL;
   struct _ibus_serializable ignore = { 0 };
   struct _ibus_property *property = NULL;

   DBG("PropList iter signature '%s'", eldbus_message_iter_signature_get(iter));

   if (!eldbus_message_iter_arguments_get(iter, "(sa{sv}av)", &ignore.text, &ignore.variant, &props))
     {
        ERR("Error deserializing IBusPropList");
        goto end;
     }

   if (!props)
     {
        INF("PropList has no property");
        goto end;
     }

   while (eldbus_message_iter_get_and_next(props, 'v', &prop))
     {
        if (!properties)
           properties = eina_array_new(10);

        if (!(property = _get_property_from_message_iter(prop)))
          {
             _free_eina_array(properties, _free_property);
             properties = NULL;
             goto end;
          }

        DBG("Appending new property %p", property);
        eina_array_push(properties, property);
     }

end:
   return properties;
}

static Eldbus_Message *
_panel_update_preedit_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *text = NULL;
   unsigned int cursor_pos = 0;
   Eina_Bool visible = 0;
   struct _ibus_text *ibus_text;

   PANEL_CHECK_MESSAGE_ERRORS(msg)

   if (!eldbus_message_arguments_get(msg, "vub", &text, &cursor_pos, &visible))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("text: '%p', cursor_pos: '%d', visible: '%d')", text, cursor_pos, visible);

   ibus_text = _get_text_from_message_iter(text);
   DBG("Preedit text = '%s'", ibus_text->text);
   _free_text(ibus_text);

   return NULL;
}

static Eldbus_Message *
_panel_show_preedit_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_hide_preedit_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_update_auxiliary_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *text = NULL;
   Eina_Bool visible = 0;
   struct _ibus_text *ibus_text;

   PANEL_CHECK_MESSAGE_ERRORS(msg)

   if (!eldbus_message_arguments_get(msg, "vb", &text, &visible))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("text: '%p', visible: '%d'", text, visible);

   ibus_text = _get_text_from_message_iter(text);
   DBG("Auxiliary text = '%s'", ibus_text->text);
   _free_text(ibus_text);

   return NULL;
}

static Eldbus_Message *
_panel_show_auxiliary_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_hide_auxiliary_text(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_update_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *table = NULL;
   Eina_Bool visible =  0;
   struct _ibus_lookup_table *ibus_lookup_table;

   PANEL_CHECK_MESSAGE_ERRORS(msg)

   if (!eldbus_message_arguments_get(msg, "vb", &table, &visible))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("table: '%p', visible: '%d'", table, visible);

   ibus_lookup_table = _get_lookup_table_from_message_iter(table);
   _free_lookup_table(ibus_lookup_table);

   return NULL;
}

static Eldbus_Message *
_panel_show_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_hide_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   DBG("here");

   return NULL;
}

static Eldbus_Message *
_panel_cursor_up_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   DBG("here");

   return NULL;
}

static Eldbus_Message *
_panel_cursor_down_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_page_up_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_page_down_lookup_table(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_register_properties(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *props = NULL;
   Eina_Array *properties = NULL;

   PANEL_CHECK_MESSAGE_ERRORS(msg)

   if (!eldbus_message_arguments_get(msg, "v", &props))
     {
        ERR("Error reading message arguments");
        return NULL;
     }

   DBG("properties: '%p'", props);

   properties = _get_properties_from_message_iter(props);
   _free_eina_array(properties, _free_property);

   return NULL;
}

static Eldbus_Message *
_panel_update_property(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message_Iter *prop = NULL;

   PANEL_CHECK_MESSAGE_ERRORS(msg)

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
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_focus_out(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_set_cursor_location(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   int x = 0, y = 0, w = 0, h = 0;

   PANEL_CHECK_MESSAGE_ERRORS(msg)

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
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_start_setup(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_state_changed(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_hide_language_bar(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

   return NULL;
}

static Eldbus_Message *
_panel_show_language_bar(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   PANEL_CHECK_MESSAGE_ERRORS(msg)

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

