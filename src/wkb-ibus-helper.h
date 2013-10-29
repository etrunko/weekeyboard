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

#ifndef _WKB_IBUS_HELPER_H_
#define _WKB_IBUS_HELPER_H_

#include <Eina.h>
#include <Eldbus.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wkb_ibus_attr
{
   unsigned int type;
   unsigned int value;
   unsigned int start_idx;
   unsigned int end_idx;
};

struct wkb_ibus_text
{
   const char *text;
   Eina_Array *attrs;
};

struct wkb_ibus_lookup_table
{
   unsigned int page_size;
   unsigned int cursor_pos;
   Eina_Bool cursor_visible;
   Eina_Bool round;
   int orientation;
   Eina_Array *candidates;
   Eina_Array *labels;
};

struct wkb_ibus_property
{
   const char *key;
   const char *icon;
   struct wkb_ibus_text *label;
   struct wkb_ibus_text *symbol;
   struct wkb_ibus_text *tooltip;
   Eina_Bool sensitive;
   Eina_Bool visible;
   unsigned int type;
   unsigned int state;
   Eina_Array *sub_properties;
};

struct wkb_ibus_engine_desc
{
   const char *name;
   const char *long_name;
   const char *desc;
   const char *lang;
   const char *license;
   const char *author;
   const char *icon;
   const char *layout;
   unsigned int rank;
   const char *hotkeys;
   const char *symbol;
   const char *setup;
   const char *layout_variant;
   const char *layout_option;
   const char *version;
   const char *text_domain;
};

struct wkb_ibus_attr *wkb_ibus_attr_from_message_iter(Eldbus_Message_Iter *iter);
void wkb_ibus_attr_free(struct wkb_ibus_attr *attr);

struct wkb_ibus_text *wkb_ibus_text_from_string(const char *str);
struct wkb_ibus_text *wkb_ibus_text_from_message_iter(Eldbus_Message_Iter *iter);
void wkb_ibus_text_free(struct wkb_ibus_text *text);

struct wkb_ibus_lookup_table *wkb_ibus_lookup_table_from_message_iter(Eldbus_Message_Iter *iter);
void wkb_ibus_lookup_table_free(struct wkb_ibus_lookup_table *table);

struct wkb_ibus_property *wkb_ibus_property_from_message_iter(Eldbus_Message_Iter *iter);
void wkb_ibus_property_free(struct wkb_ibus_property *property);

Eina_Array *wkb_ibus_properties_from_message_iter(Eldbus_Message_Iter *iter);
void wkb_ibus_properties_free(Eina_Array *properties);

struct wkb_ibus_engine_desc *wkb_ibus_engine_desc_from_message_iter(Eldbus_Message_Iter *iter);
void wkb_ibus_engine_desc_free(struct wkb_ibus_engine_desc *desc);

void wkb_ibus_iter_append_text(Eldbus_Message_Iter *iter, struct wkb_ibus_text *text);
#ifdef __cplusplus
}
#endif

#endif /* _WKB_IBUS_HELPER_H_ */
