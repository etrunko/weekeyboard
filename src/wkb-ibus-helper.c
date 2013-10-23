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

#include "wkb-ibus-helper.h"
#include "wkb-log.h"

struct wkb_ibus_serializable
{
   /*
    * All messages sent by IBus will start with the sa{sv} signature, but
    * those fields don't seem useful for us, this struct is used to help
    * on deserializing those fields
    */
   char *text;
   Eldbus_Message_Iter *variant;
};

typedef void (*_free_func) (void*);

static void
_free_eina_array(Eina_Array *array, _free_func free_cb)
{
   if (!array)
      return;

   while (eina_array_count(array))
      free_cb(eina_array_pop(array));

   eina_array_free(array);
}

struct wkb_ibus_attr *
wkb_ibus_attr_from_message_iter(Eldbus_Message_Iter *iter)
{
   struct wkb_ibus_attr *attr = calloc(1, sizeof(*attr));

   EINA_SAFETY_ON_NULL_RETURN_VAL(attr, NULL);

   DBG("Attribute iter signature '%s'", eldbus_message_iter_signature_get(iter));

   if (!eldbus_message_iter_arguments_get(iter, "uuuu", &attr->type,
                                          &attr->value, &attr->start_idx,
                                          &attr->end_idx))
     {
        ERR("Error deserializing IBusAttribute");
        wkb_ibus_attr_free(attr);
        attr = NULL;
     }

   return attr;
}

void
wkb_ibus_attr_free(struct wkb_ibus_attr *attr)
{
   free(attr);
}

void
wkb_ibus_text_free(struct wkb_ibus_text *text)
{
   if (!text)
      return;

   _free_eina_array(text->attrs, (_free_func) free);
   free(text->text);
   free(text);
}

struct wkb_ibus_text *
wkb_ibus_text_from_string(const char *str)
{
   /* TODO */
   return NULL;
}

struct wkb_ibus_text *
wkb_ibus_text_from_message_iter(Eldbus_Message_Iter *iter)
{
   struct wkb_ibus_serializable ignore = { 0 };
   struct wkb_ibus_text *text = calloc(1, sizeof(*text));
   struct wkb_ibus_attr *attr = NULL;
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

        if (!(attr = wkb_ibus_attr_from_message_iter(a)))
          {
             wkb_ibus_text_free(text);
             text = NULL;
             goto end;
          }

        eina_array_push(text->attrs, attr);
     }

end:
   return text;
}

void
wkb_ibus_lookup_table_free(struct wkb_ibus_lookup_table *table)
{
   if (!table)
      return;

   _free_eina_array(table->candidates, (_free_func) wkb_ibus_text_free);
   _free_eina_array(table->labels, (_free_func) wkb_ibus_text_free);
   free(table);
}

struct wkb_ibus_lookup_table *
wkb_ibus_lookup_table_from_message_iter(Eldbus_Message_Iter *iter)
{
   struct wkb_ibus_serializable ignore = { 0 };
   struct wkb_ibus_lookup_table *table = calloc(1, sizeof(*table));
   struct wkb_ibus_text *text = NULL;
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

        if (!(text = wkb_ibus_text_from_message_iter(t)))
          {
             wkb_ibus_lookup_table_free(table);
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

        if (!(text = wkb_ibus_text_from_message_iter(t)))
          {
             wkb_ibus_lookup_table_free(table);
             table = NULL;
             goto end;
          }

        DBG("Appending new label %s", text->text);
        eina_array_push(table->labels, text);
     }

end:
   return table;
}

void
wkb_ibus_property_free(struct wkb_ibus_property *property)
{
   if (!property)
      return;

   free(property->key);
   free(property->icon);
   wkb_ibus_text_free(property->label);
   wkb_ibus_text_free(property->symbol);
   wkb_ibus_text_free(property->tooltip);
   _free_eina_array(property->sub_properties, (_free_func) wkb_ibus_property_free);
   free(property);
}

struct wkb_ibus_property *
wkb_ibus_property_from_message_iter(Eldbus_Message_Iter *iter)
{
   struct wkb_ibus_serializable ignore = { 0 };
   struct wkb_ibus_property *prop = calloc(1, sizeof(*prop));
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

   if (!(prop->label = wkb_ibus_text_from_message_iter(label)))
     {
        wkb_ibus_property_free(prop);
        prop = NULL;
        goto end;
     }

symbol:
   if (!symbol)
     {
        INF("Property has no symbol");
        goto tooltip;
     }

   if (!(prop->symbol = wkb_ibus_text_from_message_iter(symbol)))
     {
        wkb_ibus_property_free(prop);
        prop = NULL;
        goto end;
     }

tooltip:
   if (!tooltip)
     {
        INF("Property has no tooltip");
        goto sub_props;
     }

   if (!(prop->tooltip = wkb_ibus_text_from_message_iter(tooltip)))
     {
        wkb_ibus_property_free(prop);
        prop = NULL;
        goto end;
     }

sub_props:
   if (!sub_props)
     {
        INF("Property has no sub properties");
        goto end;
     }

   prop->sub_properties = wkb_ibus_properties_from_message_iter(sub_props);

end:
   return prop;
}

void
wkb_ibus_properties_free(Eina_Array *properties)
{
   _free_eina_array(properties, (_free_func) wkb_ibus_property_free);
}

Eina_Array *
wkb_ibus_properties_from_message_iter(Eldbus_Message_Iter *iter)
{
   Eina_Array *properties = NULL;
   Eldbus_Message_Iter *props = NULL, *prop = NULL;
   struct wkb_ibus_serializable ignore = { 0 };
   struct wkb_ibus_property *property = NULL;

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

        if (!(property = wkb_ibus_property_from_message_iter(prop)))
          {
             wkb_ibus_properties_free(properties);
             properties = NULL;
             goto end;
          }

        DBG("Appending new property %p", property);
        eina_array_push(properties, property);
     }

end:
   return properties;
}

struct wkb_ibus_engine_desc *
wkb_ibus_engine_desc_from_message_iter(Eldbus_Message_Iter *iter)
{
   struct wkb_ibus_serializable ignore = { 0 };
   struct wkb_ibus_engine_desc *desc = calloc(1, sizeof(*desc));

   EINA_SAFETY_ON_NULL_RETURN_VAL(desc, NULL);

   DBG("EngineDesc iter signature '%s'", eldbus_message_iter_signature_get(iter));

   if (!eldbus_message_iter_arguments_get(iter, "(sa{sv}ssssssssusssssss)",
                                          &ignore.text, &ignore.variant,
                                          &desc->name, &desc->long_name,
                                          &desc->desc, &desc->lang,
                                          &desc->license, &desc->author,
                                          &desc->icon, &desc->layout,
                                          &desc->rank, &desc->hotkeys,
                                          &desc->symbol, &desc->setup,
                                          &desc->layout_variant, &desc->layout_option,
                                          &desc->version, &desc->text_domain))
     {
        ERR("Error deserializing IBusEngineDesc");
        free(desc);
        desc = NULL;
        goto end;
     }

   DBG("Engine description:");
   DBG("\tName...........: %s", desc->name);
   DBG("\tLong Name......: %s", desc->long_name);
   DBG("\tDescription....: %s", desc->desc);
   DBG("\tLanguage.......: %s", desc->lang);
   DBG("\tLicense........: %s", desc->license);
   DBG("\tAuthor.........: %s", desc->author);
   DBG("\tIcon...........: %s", desc->icon);
   DBG("\tLayout.........: %s", desc->layout);
   DBG("\tRank...........: %d", desc->rank);
   DBG("\tHotkeys........: %s", desc->hotkeys);
   DBG("\tSymbol.........: %s", desc->symbol);
   DBG("\tSetup..........: %s", desc->setup);
   DBG("\tLayout variant.: %s", desc->layout_variant);
   DBG("\tLayout option..: %s", desc->layout_option);
   DBG("\tVersion........: %s", desc->version);
   DBG("\tText domain....: %s", desc->text_domain);

end:
   return desc;

}

void
wkb_ibus_engine_desc_free(struct wkb_ibus_engine_desc *desc)
{
   if (!desc)
      return;

   free(desc->name);
   free(desc->long_name);
   free(desc->desc);
   free(desc->lang);
   free(desc->license);
   free(desc->author);
   free(desc->icon);
   free(desc->layout);
   free(desc->hotkeys);
   free(desc->symbol);
   free(desc->setup);
   free(desc->layout_variant);
   free(desc->layout_option);
   free(desc->version);
   free(desc->text_domain);
   free(desc);
}

void
wkb_ibus_iter_append_text(Eldbus_Message_Iter *iter, struct wkb_ibus_text *text)
{
   Eldbus_Message_Iter *txt_iter = NULL;

   /* TODO */
   txt_iter = eldbus_message_iter_container_new(iter, 'v', "(sa{sv}sv)");
   eldbus_message_iter_container_close(iter, txt_iter);
}
