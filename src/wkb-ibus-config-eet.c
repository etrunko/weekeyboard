/*
 * Copyright © 2013 Intel Corporation
 * Copyright © 2014 Jaguar Landrover
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>

#include <Eina.h>
#include <Eet.h>
#include <Eldbus.h>

#include "wkb-ibus-config-eet.h"
#include "wkb-ibus-config-key.h"
#include "wkb-log.h"

/*
 * Eet Data Descriptors
 */
static Eet_Data_Descriptor *_ibus_edd;
static Eet_Data_Descriptor *_general_edd;
static Eet_Data_Descriptor *_hotkey_edd;
static Eet_Data_Descriptor *_panel_edd;
static Eet_Data_Descriptor *_engine_edd;
static Eet_Data_Descriptor *_hangul_edd;
static Eet_Data_Descriptor *_pinyin_edd;
static Eet_Data_Descriptor *_bopomofo_edd;
static Eet_Data_Descriptor *_weekeyboard_edd;

/*
 * Base struct for all config types
 */
struct _config_section
{
   const char *id;
   Eina_List *keys;
   Eina_List *subsections;
   Eet_Data_Descriptor *edd;
   struct _config_section *parent;

   void (*set_defaults)(struct _config_section *);
   Eina_Bool (*update)(struct _config_section *);
};

static void
_config_section_free(struct _config_section *base)
{
   struct wkb_config_key *key;
   struct _config_section *sub;

   eina_stringshare_del(base->id);

   EINA_LIST_FREE(base->keys, key)
      wkb_config_key_free(key);

   eina_list_free(base->keys);

   EINA_LIST_FREE(base->subsections, sub)
      _config_section_free(sub);

   eina_list_free(base->subsections);

   free(base);
}

static void
_config_section_set_defaults(struct _config_section *base)
{
   Eina_List *node;
   struct _config_section *sub;

   EINA_LIST_FOREACH(base->subsections, node, sub)
      _config_section_set_defaults(sub);

   if (!base->set_defaults)
      return;

   base->set_defaults(base);
}

static Eina_Bool
_config_section_update(struct _config_section *base)
{
   Eina_List *node;
   struct _config_section *sub;
   Eina_Bool ret = EINA_FALSE;

   EINA_LIST_FOREACH(base->subsections, node, sub)
      if (_config_section_update(sub))
         ret = EINA_TRUE;

   if (!base->update)
      return ret;

   return base->update(base) || ret;
}

static struct _config_section *
_config_section_find(struct _config_section *base, const char *section)
{
   Eina_List *node;
   struct _config_section *ret = NULL, *sub;

   if (!section)
      return NULL;

   if (base->id && !strncasecmp(section, base->id, strlen(section)))
     {
        DBG("Requested section: '%s' match: '%s'", section, base->id);
        return base;
     }

   EINA_LIST_FOREACH(base->subsections, node, sub)
      if ((ret = _config_section_find(sub, section)))
         break;

   return ret;
}

static struct _config_section *
_config_section_toplevel(struct _config_section *base)
{
   while (base->parent != NULL)
      base = base->parent;

   return base;
}

static struct wkb_config_key *
_config_section_find_key(struct _config_section *base, const char *section, const char *name)
{
   struct wkb_config_key *ret = NULL, *key;
   struct _config_section *sec;
   const char *key_id;
   Eina_List *node;

   if (!(sec = _config_section_find(base, section)))
     {
        DBG("Config section with id '%s' not found", section);
        goto end;
     }

   EINA_LIST_FOREACH(sec->keys, node, key)
     {
        key_id = wkb_config_key_id(key);
        if (!strcasecmp(name, key_id))
          {
             DBG("Requested key: '%s' match: '%s'", name, key_id);
             ret = key;
             break;
          }
     }

end:
   return ret;
}

void
_config_section_dump(struct _config_section *base, const char *tab)
{
   Eina_List *node;
   struct _config_section *sec;
   struct wkb_config_key *key;
   const char *sig, *new_tab;

   EINA_LIST_FOREACH(base->keys, node, key)
     {
        printf("%s'%s/%s': ", tab, wkb_config_key_section(key), wkb_config_key_id(key));
        sig = wkb_config_key_signature(key);
        switch (*sig)
          {
           case 's':
                {
                   printf("'%s'\n", wkb_config_key_get_string(key));
                   break;
                }
           case 'i':
                {
                   printf("%d\n", wkb_config_key_get_int(key));
                   break;
                }
           case 'b':
                {
                   printf("%s\n", wkb_config_key_get_bool(key) ? "True" : "False");
                   break;
                }
           case 'a':
                {
                   char **s, **slist = wkb_config_key_get_string_list(key);
                   printf("{");
                   for (s = slist; *s != NULL; ++s)
                     {
                        printf("'%s',", *s);
                     }
                   printf("}\n");
                   free(slist);
                   break;
                }
           default:
              break;
          }
     }

   new_tab = eina_stringshare_printf("\t%s", tab);
   EINA_LIST_FOREACH(base->subsections, node, sec)
     {
        printf("%s%s'%s'\n", base->keys ? "\n" : "", tab, sec->id);
        _config_section_dump(sec, new_tab);
     }
   eina_stringshare_del(new_tab);
}

#define _config_section_init(_section, _id, _parent) \
   do { \
        if (!_section) \
           break; \
        _section->set_defaults = _config_ ## _id ## _set_defaults; \
        _section->update = _config_ ## _id ## _update; \
        _section->parent = _parent; \
        _section->edd = _ ## _id ## _edd; \
        if (!_section->parent) \
           _section->id = eina_stringshare_add(#_id); \
        else \
          { \
             if (!_section->parent->parent) \
                /* do not use parent id if it is toplevel */ \
                _section->id = eina_stringshare_add(#_id); \
             else \
                _section->id = eina_stringshare_printf("%s/" #_id, _section->parent->id); \
             _section->parent->subsections = eina_list_append(_section->parent->subsections, _section); \
          } \
        _config_ ## _id ## _section_init(_section); \
   } while (0)

#define _config_section_add_key(_section, _section_id, _key_type, _field) \
   do { \
        struct _config_ ## _section_id *__conf = (struct _config_ ## _section_id *) _section; \
        struct wkb_config_key *__key = wkb_config_key_ ## _key_type(#_field, _section->id, &__conf->_field); \
        _section->keys = eina_list_append(_section->keys, __key); \
   } while (0)

#define _config_section_add_key_int(_section, _section_id, _field) \
    _config_section_add_key(_section, _section_id, int, _field)

#define _config_section_add_key_bool(_section, _section_id, _field) \
    _config_section_add_key(_section, _section_id, bool, _field)

#define _config_section_add_key_string(_section, _section_id, _field) \
    _config_section_add_key(_section, _section_id, string, _field)

#define _config_section_add_key_string_list(_section, _section_id, _field) \
    _config_section_add_key(_section, _section_id, string_list, _field)

/*
 * Helpers
 */
static Eina_List *
_config_string_list_new(const char **strs)
{
   Eina_List *list = NULL;
   const char *str;

   for (str = *strs; str != NULL; str = *++strs)
      list = eina_list_append(list, eina_stringshare_add(str));

   return list;
}

static char *
_config_string_sanitize(const char *str)
{
   char *s, *sane = strdup(str);

   for (s = sane; *s; s++)
     {
        if (*s == '-')
           *s = '_';
        else if (*s >= 'A' && *s <= 'Z')
           *s += ('a' - 'A');
     }

   return sane;
}

/*
 * <schema path="/desktop/ibus/general/hotkey/" id="org.freedesktop.ibus.general.hotkey">
 *   <key type="as" name="trigger">
 *     <default>[ 'Control+space', 'Zenkaku_Hankaku', 'Alt+Kanji', 'Alt+grave', 'Hangul', 'Alt+Release+Alt_R' ]</default>
 *     <summary>Trigger shortcut keys</summary>
 *     <description>The shortcut keys for turning input method on or off</description>
 *   </key>
 *   <key type="as" name="triggers">
 *     <default>[ '&lt;Super&gt;space' ]</default>
 *     <summary>Trigger shortcut keys for gtk_accelerator_parse</summary>
 *     <description>The shortcut keys for turning input method on or off</description>
 *   </key>
 *   <key type="as" name="enable-unconditional">
 *     <default>[]</default>
 *     <summary>Enable shortcut keys</summary>
 *     <description>The shortcut keys for turning input method on</description>
 *   </key>
 *   <key type="as" name="disable-unconditional">
 *     <default>[]</default>
 *     <summary>Disable shortcut keys</summary>
 *     <description>The shortcut keys for turning input method off</description>
 *   </key>
 *   <key type="as" name="next-engine">
 *     <default>[ 'Alt+Shift_L' ]</default>
 *     <summary>Next engine shortcut keys</summary>
 *     <description>The shortcut keys for switching to the next input method in the list</description>
 *   </key>
 *   <key type="as" name="next-engine-in-menu">
 *     <default>[ 'Alt+Shift_L' ]</default>
 *     <summary>Next engine shortcut keys</summary>
 *     <description>The shortcut keys for switching to the next input method in the list</description>
 *   </key>
 *   <key type="as" name="prev-engine">
 *     <default>[]</default>
 *     <summary>Prev engine shortcut keys</summary>
 *     <description>The shortcut keys for switching to the previous input method</description>
 *   </key>
 *   <key type="as" name="previous-engine">
 *     <default>[]</default>
 *     <summary>Prev engine shortcut keys</summary>
 *     <description>The shortcut keys for switching to the previous input method</description>
 *   </key>
 * </schema>
 */
struct _config_hotkey
{
   struct _config_section base;

   Eina_List *trigger;
   Eina_List *triggers;
   Eina_List *enable_unconditional;
   Eina_List *disable_unconditional;
   Eina_List *next_engine;
   Eina_List *next_engine_in_menu;
   Eina_List *prev_engine;
   Eina_List *previous_engine;
};

static Eet_Data_Descriptor *
_config_hotkey_edd_new(void)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_hotkey);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_hotkey, "trigger", trigger);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_hotkey, "triggers", triggers);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_hotkey, "enable-unconditional", enable_unconditional);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_hotkey, "disable-unconditional", disable_unconditional);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_hotkey, "next-engine", next_engine);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_hotkey, "next-engine-in-menu", next_engine_in_menu);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_hotkey, "prev-engine", prev_engine);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_hotkey, "previous-engine", previous_engine);

   return edd;
}

static void
_config_hotkey_set_defaults(struct _config_section *base)
{
   struct _config_hotkey *hotkey = (struct _config_hotkey *) base;

   const char *trigger[] = { "Control+space", "Zenkaku_Hankaku", "Alt+Kanji", "Alt+grave", "Hangul", "Alt+Release+Alt_R", NULL };
   const char *triggers[] = { "<Super>space", NULL };
   const char *enable_unconditional[] = { NULL };
   const char *disable_unconditional[] = { NULL };
   const char *next_engine[] = { NULL };
   const char *next_engine_in_menu[] = { NULL };
   const char *prev_engine[] = { NULL };
   const char *previous_engine[] = { NULL };

   hotkey->trigger = _config_string_list_new(trigger);
   hotkey->triggers = _config_string_list_new(triggers);
   hotkey->enable_unconditional = _config_string_list_new(enable_unconditional);
   hotkey->disable_unconditional = _config_string_list_new(disable_unconditional);
   hotkey->next_engine = _config_string_list_new(next_engine);
   hotkey->next_engine_in_menu = _config_string_list_new(next_engine_in_menu);
   hotkey->prev_engine = _config_string_list_new(prev_engine);
   hotkey->previous_engine = _config_string_list_new(previous_engine);
}

#define _config_hotkey_update NULL;

static void
_config_hotkey_section_init(struct _config_section *base)
{
   _config_section_add_key_string_list(base, hotkey, trigger);
   _config_section_add_key_string_list(base, hotkey, triggers);
   _config_section_add_key_string_list(base, hotkey, enable_unconditional);
   _config_section_add_key_string_list(base, hotkey, disable_unconditional);
   _config_section_add_key_string_list(base, hotkey, next_engine);
   _config_section_add_key_string_list(base, hotkey, next_engine_in_menu);
   _config_section_add_key_string_list(base, hotkey, prev_engine);
   _config_section_add_key_string_list(base, hotkey, previous_engine);
}

static struct _config_section *
_config_hotkey_new(struct _config_section *parent)
{
   struct _config_hotkey *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, hotkey, parent);
   return base;
}

/*
 * <schema path="/desktop/ibus/general/" id="org.freedesktop.ibus.general">
 *    <key type="as" name="preload-engines">
 *      <default>[]</default>
 *      <summary>Preload engines</summary>
 *      <description>Preload engines during ibus starts up</description>
 *    </key>
 *    <key type="as" name="engines-order">
 *      <default>[]</default>
 *      <summary>Engines order</summary>
 *      <description>Saved engines order in input method list</description>
 *    </key>
 *    <key type="i" name="switcher-delay-time">
 *      <default>400</default>
 *      <summary>Popup delay milliseconds for IME switcher window</summary>
 *      <description>Set popup delay milliseconds to show IME switcher window. The default is 400. 0 = Show the window immediately. 0 &lt; Delay milliseconds. 0 &gt; Do not show the
 *    </key>
 *    <key type="s" name="version">
 *      <default>''</default>
 *      <summary>Saved version number</summary>
 *      <description>The saved version number will be used to check the difference between the version of the previous installed ibus and one of the current ibus.</description>
 *    </key>
 *    <key type="b" name="use-system-keyboard-layout">
 *      <default>false</default>
 *      <summary>Use system keyboard layout</summary>
 *      <description>Use system keyboard (XKB) layout</description>
 *    </key>
 *    <key type="b" name="embed-preedit-text">
 *      <default>true</default>
 *      <summary>Embed Preedit Text</summary>
 *      <description>Embed Preedit Text in Application Window</description>
 *    </key>
 *    <key type="b" name="use-global-engine">
 *      <default>false</default>
 *      <summary>Use global input method</summary>
 *     <description>Share the same input method among all applications</description>
 *    </key>
 *    <key type="b" name="enable-by-default">
 *      <default>false</default>
 *      <summary>Enable input method by default</summary>
 *      <description>Enable input method by default when the application gets input focus</description>
 *    </key>
 *    <key type="as" name="dconf-preserve-name-prefixes">
 *      <default>[ '/desktop/ibus/engine/pinyin', '/desktop/ibus/engine/bopomofo', '/desktop/ibus/engine/hangul' ]</default>
 *      <summary>DConf preserve name prefixes</summary>
 *      <description>Prefixes of DConf keys to stop name conversion</description>
 *    </key>
 *    <child schema="org.freedesktop.ibus.general.hotkey" name="hotkey"/>
 * </schema>
 */
struct _config_general
{
   struct _config_section base;

   struct _config_section *hotkey;

   Eina_List *preload_engines;
   Eina_List *engines_order;
   Eina_List *dconf_preserve_name_prefixes;

   const char *version;

   int switcher_delay_time;

   Eina_Bool use_system_keyboard_layout;
   Eina_Bool embed_preedit_text;
   Eina_Bool use_global_engine;
   Eina_Bool enable_by_default;
};

static Eet_Data_Descriptor *
_config_general_edd_new(Eet_Data_Descriptor *hotkey_edd)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_general);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_general, "preload-engines", preload_engines);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_general, "engines-order", engines_order);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_general, "switcher-delay-time", switcher_delay_time, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_general, "version", version, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_general, "use-system-keyboard-layout", use_system_keyboard_layout, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_general, "embed-preedit-text", embed_preedit_text, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_general, "use-global-engine", use_global_engine, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_general, "enable-by-default", enable_by_default, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_general, "dconf-preserve-name-prefixes", dconf_preserve_name_prefixes);
   EET_DATA_DESCRIPTOR_ADD_SUB(edd, struct _config_general, "hotkey", hotkey, hotkey_edd);

   return edd;
}

static void
_config_general_set_defaults(struct _config_section *base)
{
   struct _config_general *general = (struct _config_general *) base;

   const char *preload_engines[] = { NULL };
   const char *engines_order[] = { NULL };
   const char *dconf_preserve_name_prefixes[] = { "/desktop/ibus/engine/pinyin", "/desktop/ibus/engine/bopomofo", "/desktop/ibus/engine/hangul", NULL };

   general->preload_engines = _config_string_list_new(preload_engines);
   general->engines_order = _config_string_list_new(engines_order);
   general->switcher_delay_time = 400;
   general->version = eina_stringshare_add("");
   general->use_system_keyboard_layout = EINA_FALSE;
   general->embed_preedit_text = EINA_TRUE;
   general->use_global_engine = EINA_FALSE;
   general->enable_by_default = EINA_FALSE;
   general->dconf_preserve_name_prefixes = _config_string_list_new(dconf_preserve_name_prefixes);
}

#define _config_general_update NULL;

static void
_config_general_section_init(struct _config_section *base)
{
   struct _config_general *conf = (struct _config_general *) base;

   _config_section_add_key_string_list(base, general, preload_engines);
   _config_section_add_key_string_list(base, general, engines_order);
   _config_section_add_key_int(base, general, switcher_delay_time);
   _config_section_add_key_string(base, general, version);
   _config_section_add_key_bool(base, general, use_system_keyboard_layout);
   _config_section_add_key_bool(base, general, embed_preedit_text);
   _config_section_add_key_bool(base, general, use_global_engine);
   _config_section_add_key_bool(base, general, enable_by_default);
   _config_section_add_key_string_list(base, general, dconf_preserve_name_prefixes);

   _config_section_init(conf->hotkey, hotkey, base);
}

static struct _config_section *
_config_general_new(struct _config_section *parent)
{
   struct _config_general *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, general, parent);
   conf->hotkey = _config_hotkey_new(base);
   return base;
}

/*
 * <schema path="/desktop/ibus/panel/" id="org.freedesktop.ibus.panel">
 *    <key type="i" name="show">
 *      <default>0</default>
 *      <summary>Auto hide</summary>
 *      <description>The behavior of language panel. 0 = Embedded in menu, 1 = Auto hide, 2 = Always show</description>
 *    </key>
 *    <key type="i" name="x">
 *      <default>-1</default>
 *      <summary>Language panel position</summary>
 *    </key>
 *    <key type="i" name="y">
 *      <default>-1</default>
 *      <summary>Language panel position</summary>
 *    </key>
 *    <key type="i" name="lookup-table-orientation">
 *      <default>1</default>
 *      <summary>Orientation of lookup table</summary>
 *      <description>Orientation of lookup table. 0 = Horizontal, 1 = Vertical</description>
 *    </key>
 *    <key type="b" name="show-icon-on-systray">
 *      <default>true</default>
 *      <summary>Show icon on system tray</summary>
 *      <description>Show icon on system tray</description>
 *    </key>
 *    <key type="b" name="show-im-name">
 *      <default>false</default>
 *      <summary>Show input method name</summary>
 *      <description>Show input method name on language bar</description>
 *    </key>
 *    <key type="b" name="use-custom-font">
 *      <default>false</default>
 *      <summary>Use custom font</summary>
 *      <description>Use custom font name for language panel</description>
 *    </key>
 *    <key type="s" name="custom-font">
 *      <default>'Sans 10'</default>
 *      <summary>Custom font</summary>
 *      <description>Custom font name for language panel</description>
 *    </key>
 * </schema>
 */
struct _config_panel
{
   struct _config_section base;

   const char *custom_font;
   int show;
   int x;
   int y;
   int lookup_table_orientation;
   Eina_Bool show_icon_in_systray;
   Eina_Bool show_im_name;
   Eina_Bool use_custom_font;
};

static Eet_Data_Descriptor *
_config_panel_edd_new(void)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_panel);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_panel, "custom-font", custom_font, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_panel, "show", show, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_panel, "x", x, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_panel, "y", y, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_panel, "lookup-table-orientation", lookup_table_orientation, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_panel, "show-icon-in-systray", show_icon_in_systray, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_panel, "show-im-name", show_im_name, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_panel, "use-custom-font", use_custom_font, EET_T_UCHAR);
   return edd;
}

static void
_config_panel_set_defaults(struct _config_section *base)
{
   struct _config_panel *panel = (struct _config_panel *) base;

   panel->custom_font = eina_stringshare_add("Sans 10");
   panel->show = 0;
   panel->x = -1;
   panel->y = -1;
   panel->lookup_table_orientation = 1;
   panel->show_icon_in_systray = EINA_TRUE;
   panel->show_im_name = EINA_FALSE;
   panel->use_custom_font = EINA_FALSE;
}

#define _config_panel_update NULL;

static void
_config_panel_section_init(struct _config_section *base)
{
   _config_section_add_key_string(base, panel, custom_font);
   _config_section_add_key_int(base, panel, show);
   _config_section_add_key_int(base, panel, x);
   _config_section_add_key_int(base, panel, y);
   _config_section_add_key_int(base, panel, lookup_table_orientation);
   _config_section_add_key_bool(base, panel, show_icon_in_systray);
   _config_section_add_key_bool(base, panel, show_im_name);
   _config_section_add_key_bool(base, panel, use_custom_font);
}

static struct _config_section *
_config_panel_new(struct _config_section *parent)
{
   struct _config_panel *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, panel, parent);
   return base;
}

/*
 * NO SCHEMA AVAILABLE. BASED ON THE SOURCE CODE
 */
struct _config_hangul
{
   struct _config_section base;

   const char *hangulkeyboard;
   Eina_List *hanjakeys;
   Eina_Bool wordcommit;
   Eina_Bool autoreorder;
};

static Eet_Data_Descriptor *
_config_hangul_edd_new(void)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_hangul);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_hangul, "HangulKeyboard", hangulkeyboard, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(edd, struct _config_hangul, "HanjaKeys", hanjakeys);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_hangul, "WordCommit", wordcommit, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_hangul, "AutoReorder", autoreorder, EET_T_UCHAR);

   return edd;
}

static void
_config_hangul_set_defaults(struct _config_section *base)
{
   struct _config_hangul *hangul = (struct _config_hangul *) base;
   const char *hanja_keys[] = { "Hangul_Hanja", "F9", NULL };

   hangul->hangulkeyboard = eina_stringshare_add("2");
   hangul->hanjakeys = _config_string_list_new(hanja_keys);
   hangul->wordcommit = EINA_FALSE;
   hangul->autoreorder = EINA_TRUE;
}

#define _config_hangul_update NULL;

static void
_config_hangul_section_init(struct _config_section *base)
{
   _config_section_add_key_string(base, hangul, hangulkeyboard);
   _config_section_add_key_string_list(base, hangul, hanjakeys);
   _config_section_add_key_bool(base, hangul, wordcommit);
   _config_section_add_key_bool(base, hangul, autoreorder);
}

static struct _config_section *
_config_hangul_new(struct _config_section *parent)
{
   struct _config_hangul *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, hangul, parent);
   return base;
}

/*
 * NO SCHEMA AVAILABLE. BASED ON THE SOURCE CODE
 */
struct _config_pinyin
{
   struct _config_section base;

   Eina_Bool   autocommit;
   Eina_Bool   commaperiodpage;
   Eina_Bool   correctpinyin;
   Eina_Bool   correctpinyin_gn_ng;
   Eina_Bool   correctpinyin_iou_iu;
   Eina_Bool   correctpinyin_mg_ng;
   Eina_Bool   correctpinyin_on_ong;
   Eina_Bool   correctpinyin_uei_ui;
   Eina_Bool   correctpinyin_uen_un;
   Eina_Bool   correctpinyin_ue_ve;
   Eina_Bool   correctpinyin_v_u;
   Eina_Bool   ctrlswitch;
   const char *dictionaries;
   Eina_Bool   doublepinyin;
   int         doublepinyinschema;
   Eina_Bool   dynamicadjust;
   Eina_Bool   fuzzypinyin;
   Eina_Bool   fuzzypinyin_an_ang;
   Eina_Bool   fuzzypinyin_ang_an;
   Eina_Bool   fuzzypinyin_c_ch;
   Eina_Bool   fuzzypinyin_ch_c;
   Eina_Bool   fuzzypinyin_en_eng;
   Eina_Bool   fuzzypinyin_eng_en;
   Eina_Bool   fuzzypinyin_f_h;
   Eina_Bool   fuzzypinyin_g_k;
   Eina_Bool   fuzzypinyin_h_f;
   Eina_Bool   fuzzypinyin_ing_in;
   Eina_Bool   fuzzypinyin_in_ing;
   Eina_Bool   fuzzypinyin_k_g;
   Eina_Bool   fuzzypinyin_l_n;
   Eina_Bool   fuzzypinyin_l_r;
   Eina_Bool   fuzzypinyin_n_l;
   Eina_Bool   fuzzypinyin_r_l;
   Eina_Bool   fuzzypinyin_sh_s;
   Eina_Bool   fuzzypinyin_s_sh;
   Eina_Bool   fuzzypinyin_zh_z;
   Eina_Bool   fuzzypinyin_z_zh;
   Eina_Bool   incompletepinyin;
   Eina_Bool   initchinese;
   Eina_Bool   initfull;
   Eina_Bool   initfullpunct;
   Eina_Bool   initsimplifiedchinese;
   int         lookuptableorientation;
   int         lookuptablepagesize;
   Eina_Bool   minusequalpage;
   Eina_Bool   shiftselectcandidate;
   Eina_Bool   specialphrases;
};

static Eet_Data_Descriptor *
_config_pinyin_edd_new(void)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_pinyin);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "AutoCommit", autocommit, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CommaPeriodPage", commaperiodpage, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CorrectPinyin", correctpinyin, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CorrectPinyin_GN_NG", correctpinyin_gn_ng, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CorrectPinyin_IOU_IU", correctpinyin_iou_iu, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CorrectPinyin_MG_NG", correctpinyin_mg_ng, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CorrectPinyin_ON_ONG", correctpinyin_on_ong, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CorrectPinyin_UEI_UI", correctpinyin_uei_ui, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CorrectPinyin_UEN_UN", correctpinyin_uen_un, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CorrectPinyin_UE_VE", correctpinyin_ue_ve, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CorrectPinyin_V_U", correctpinyin_v_u, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "CtrlSwitch", ctrlswitch, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "Dictionaries", dictionaries, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "DoublePinyin", doublepinyin, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "DoublePinyinSchema", doublepinyinschema, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "DynamicAdjust", dynamicadjust, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin", fuzzypinyin, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_AN_ANG", fuzzypinyin_an_ang, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_ANG_AN", fuzzypinyin_ang_an, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_C_CH", fuzzypinyin_c_ch, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_CH_C", fuzzypinyin_ch_c, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_EN_ENG", fuzzypinyin_en_eng, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_ENG_EN", fuzzypinyin_eng_en, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_F_H", fuzzypinyin_f_h, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_G_K", fuzzypinyin_g_k, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_H_F", fuzzypinyin_h_f, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_ING_IN", fuzzypinyin_ing_in, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_IN_ING", fuzzypinyin_in_ing, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_K_G", fuzzypinyin_k_g, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_L_N", fuzzypinyin_l_n, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_L_R", fuzzypinyin_l_r, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_N_L", fuzzypinyin_n_l, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_R_L", fuzzypinyin_r_l, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_SH_S", fuzzypinyin_sh_s, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_S_SH", fuzzypinyin_s_sh, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_ZH_Z", fuzzypinyin_zh_z, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "FuzzyPinyin_Z_ZH", fuzzypinyin_z_zh, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "IncompletePinyin", incompletepinyin, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "InitChinese", initchinese, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "InitFull", initfull, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "InitFullPunct", initfullpunct, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "InitSimplifiedChinese", initsimplifiedchinese, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "LookupTableOrientation", lookuptableorientation, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "LookupTablePageSize", lookuptablepagesize, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "MinusEqualPage", minusequalpage, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "ShiftSelectCandidate", shiftselectcandidate, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_pinyin, "SpecialPhrases", specialphrases, EET_T_UCHAR);

   return edd;
}

static void
_config_pinyin_set_defaults(struct _config_section *base)
{
   struct _config_pinyin *pinyin = (struct _config_pinyin *) base;

   pinyin->autocommit = EINA_FALSE;
   pinyin->commaperiodpage = EINA_TRUE;
   pinyin->correctpinyin = EINA_TRUE;
   pinyin->correctpinyin_gn_ng = EINA_TRUE;
   pinyin->correctpinyin_iou_iu = EINA_TRUE;
   pinyin->correctpinyin_mg_ng = EINA_TRUE;
   pinyin->correctpinyin_on_ong = EINA_TRUE;
   pinyin->correctpinyin_uei_ui = EINA_TRUE;
   pinyin->correctpinyin_uen_un = EINA_TRUE;
   pinyin->correctpinyin_ue_ve = EINA_TRUE;
   pinyin->correctpinyin_v_u = EINA_TRUE;
   pinyin->ctrlswitch = EINA_FALSE;
   pinyin->dictionaries = eina_stringshare_add("2");
   pinyin->doublepinyin = EINA_FALSE;
   pinyin->doublepinyinschema = 0;
   pinyin->dynamicadjust = EINA_TRUE;
   pinyin->fuzzypinyin = EINA_FALSE;
   pinyin->fuzzypinyin_an_ang = EINA_TRUE;
   pinyin->fuzzypinyin_ang_an = EINA_TRUE;
   pinyin->fuzzypinyin_c_ch = EINA_TRUE;
   pinyin->fuzzypinyin_ch_c = EINA_FALSE;
   pinyin->fuzzypinyin_en_eng = EINA_TRUE;
   pinyin->fuzzypinyin_eng_en = EINA_TRUE;
   pinyin->fuzzypinyin_f_h = EINA_TRUE;
   pinyin->fuzzypinyin_g_k = EINA_FALSE;
   pinyin->fuzzypinyin_h_f = EINA_FALSE;
   pinyin->fuzzypinyin_ing_in = EINA_TRUE;
   pinyin->fuzzypinyin_in_ing = EINA_TRUE;
   pinyin->fuzzypinyin_k_g = EINA_TRUE;
   pinyin->fuzzypinyin_l_n = EINA_TRUE;
   pinyin->fuzzypinyin_l_r = EINA_FALSE;
   pinyin->fuzzypinyin_n_l = EINA_FALSE;
   pinyin->fuzzypinyin_r_l = EINA_FALSE;
   pinyin->fuzzypinyin_sh_s = EINA_FALSE;
   pinyin->fuzzypinyin_s_sh = EINA_TRUE;
   pinyin->fuzzypinyin_zh_z = EINA_FALSE;
   pinyin->fuzzypinyin_z_zh = EINA_TRUE;
   pinyin->incompletepinyin = EINA_TRUE;
   pinyin->initchinese = EINA_TRUE;
   pinyin->initfull = EINA_FALSE;
   pinyin->initfullpunct = EINA_TRUE;
   pinyin->initsimplifiedchinese = EINA_TRUE;
   pinyin->lookuptableorientation = 0;
   pinyin->lookuptablepagesize = 5;
   pinyin->minusequalpage = EINA_TRUE;
   pinyin->shiftselectcandidate = EINA_FALSE;
   pinyin->specialphrases = EINA_TRUE;
}

#define _config_pinyin_update NULL;

static void
_config_pinyin_section_init(struct _config_section *base)
{
   _config_section_add_key_bool(base, pinyin, autocommit);
   _config_section_add_key_bool(base, pinyin, commaperiodpage);
   _config_section_add_key_bool(base, pinyin, correctpinyin);
   _config_section_add_key_bool(base, pinyin, correctpinyin_gn_ng);
   _config_section_add_key_bool(base, pinyin, correctpinyin_iou_iu);
   _config_section_add_key_bool(base, pinyin, correctpinyin_mg_ng);
   _config_section_add_key_bool(base, pinyin, correctpinyin_on_ong);
   _config_section_add_key_bool(base, pinyin, correctpinyin_uei_ui);
   _config_section_add_key_bool(base, pinyin, correctpinyin_uen_un);
   _config_section_add_key_bool(base, pinyin, correctpinyin_ue_ve);
   _config_section_add_key_bool(base, pinyin, correctpinyin_v_u);
   _config_section_add_key_bool(base, pinyin, ctrlswitch);
   _config_section_add_key_string(base, pinyin, dictionaries);
   _config_section_add_key_bool(base, pinyin, doublepinyin);
   _config_section_add_key_int(base, pinyin, doublepinyinschema);
   _config_section_add_key_bool(base, pinyin, dynamicadjust);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_an_ang);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_ang_an);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_c_ch);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_ch_c);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_en_eng);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_eng_en);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_f_h);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_g_k);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_h_f);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_ing_in);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_in_ing);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_k_g);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_l_n);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_l_r);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_n_l);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_r_l);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_sh_s);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_s_sh);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_zh_z);
   _config_section_add_key_bool(base, pinyin, fuzzypinyin_z_zh);
   _config_section_add_key_bool(base, pinyin, incompletepinyin);
   _config_section_add_key_bool(base, pinyin, initchinese);
   _config_section_add_key_bool(base, pinyin, initfull);
   _config_section_add_key_bool(base, pinyin, initfullpunct);
   _config_section_add_key_bool(base, pinyin, initsimplifiedchinese);
   _config_section_add_key_int(base, pinyin, lookuptableorientation);
   _config_section_add_key_int(base, pinyin, lookuptablepagesize);
   _config_section_add_key_bool(base, pinyin, minusequalpage);
   _config_section_add_key_bool(base, pinyin, shiftselectcandidate);
   _config_section_add_key_bool(base, pinyin, specialphrases);
}

static struct _config_section *
_config_pinyin_new(struct _config_section *parent)
{
   struct _config_pinyin *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, pinyin, parent);
   return base;
}

/*
 * NO SCHEMA AVAILABLE. BASED ON THE SOURCE CODE
 */
struct _config_bopomofo
{
   struct _config_section base;

   int         auxiliaryselectkey_f;
   int         auxiliaryselectkey_kp;
   int         bopomofokeyboardmapping;
   Eina_Bool   ctrlswitch;
   const char *dictionaries;
   Eina_Bool   dynamicadjust;
   Eina_Bool   enterkey;
   Eina_Bool   fuzzypinyin;
   Eina_Bool   fuzzypinyin_an_ang;
   Eina_Bool   fuzzypinyin_ang_an;
   Eina_Bool   fuzzypinyin_c_ch;
   Eina_Bool   fuzzypinyin_ch_c;
   Eina_Bool   fuzzypinyin_en_eng;
   Eina_Bool   fuzzypinyin_eng_en;
   Eina_Bool   fuzzypinyin_f_h;
   Eina_Bool   fuzzypinyin_g_k;
   Eina_Bool   fuzzypinyin_h_f;
   Eina_Bool   fuzzypinyin_ing_in;
   Eina_Bool   fuzzypinyin_in_ing;
   Eina_Bool   fuzzypinyin_k_g;
   Eina_Bool   fuzzypinyin_l_n;
   Eina_Bool   fuzzypinyin_l_r;
   Eina_Bool   fuzzypinyin_n_l;
   Eina_Bool   fuzzypinyin_r_l;
   Eina_Bool   fuzzypinyin_sh_s;
   Eina_Bool   fuzzypinyin_s_sh;
   Eina_Bool   fuzzypinyin_zh_z;
   Eina_Bool   fuzzypinyin_z_zh;
   int         guidekey;
   Eina_Bool   incompletepinyin;
   Eina_Bool   initchinese;
   Eina_Bool   initfull;
   Eina_Bool   initfullpunct;
   Eina_Bool   initsimplifiedchinese;
   int         lookuptableorientation;
   int         lookuptablepagesize;
   int         selectkeys;
   Eina_Bool   specialphrases;
};

static Eet_Data_Descriptor *
_config_bopomofo_edd_new(void)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_bopomofo);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "AuxiliarySelectKey_F", auxiliaryselectkey_f, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "AuxiliarySelectKey_KP", auxiliaryselectkey_kp, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "BopomofoKeyboardMapping", bopomofokeyboardmapping, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "CtrlSwitch", ctrlswitch, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "Dictionaries", dictionaries, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "DynamicAdjust", dynamicadjust, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "EnterKey", enterkey, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin", fuzzypinyin, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_AN_ANG", fuzzypinyin_an_ang, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_ANG_AN", fuzzypinyin_ang_an, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_C_CH", fuzzypinyin_c_ch, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_CH_C", fuzzypinyin_ch_c, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_EN_ENG", fuzzypinyin_en_eng, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_ENG_EN", fuzzypinyin_eng_en, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_F_H", fuzzypinyin_f_h, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_G_K", fuzzypinyin_g_k, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_H_F", fuzzypinyin_h_f, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_ING_IN", fuzzypinyin_ing_in, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_IN_ING", fuzzypinyin_in_ing, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_K_G", fuzzypinyin_k_g, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_L_N", fuzzypinyin_l_n, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_L_R", fuzzypinyin_l_r, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_N_L", fuzzypinyin_n_l, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_R_L", fuzzypinyin_r_l, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_SH_S", fuzzypinyin_sh_s, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_S_SH", fuzzypinyin_s_sh, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_ZH_Z", fuzzypinyin_zh_z, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "FuzzyPinyin_Z_ZH", fuzzypinyin_z_zh, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "GuideKey", guidekey, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "IncompletePinyin", incompletepinyin, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "InitChinese", initchinese, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "InitFull", initfull, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "InitFullPunct", initfullpunct, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "InitSimplifiedChinese", initsimplifiedchinese, EET_T_UCHAR);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "LookupTableOrientation", lookuptableorientation, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "LookupTablePageSize", lookuptablepagesize, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "SelectKeys", selectkeys, EET_T_INT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_bopomofo, "SpecialPhrases", specialphrases, EET_T_UCHAR);

   return edd;
}

static void
_config_bopomofo_set_defaults(struct _config_section *base)
{
   struct _config_bopomofo *bopomofo = (struct _config_bopomofo *) base;

   bopomofo->auxiliaryselectkey_f = 1;
   bopomofo->auxiliaryselectkey_kp = 1;
   bopomofo->bopomofokeyboardmapping = 0;
   bopomofo->ctrlswitch = EINA_FALSE;
   bopomofo->dictionaries = eina_stringshare_add("2");
   bopomofo->dynamicadjust = EINA_TRUE;
   bopomofo->enterkey = EINA_TRUE;
   bopomofo->fuzzypinyin = EINA_TRUE;
   bopomofo->fuzzypinyin_an_ang = EINA_TRUE;
   bopomofo->fuzzypinyin_ang_an = EINA_FALSE;
   bopomofo->fuzzypinyin_c_ch = EINA_TRUE;
   bopomofo->fuzzypinyin_ch_c = EINA_FALSE;
   bopomofo->fuzzypinyin_en_eng = EINA_TRUE;
   bopomofo->fuzzypinyin_eng_en = EINA_TRUE;
   bopomofo->fuzzypinyin_f_h = EINA_TRUE;
   bopomofo->fuzzypinyin_g_k = EINA_FALSE;
   bopomofo->fuzzypinyin_h_f = EINA_FALSE;
   bopomofo->fuzzypinyin_ing_in = EINA_TRUE;
   bopomofo->fuzzypinyin_in_ing = EINA_TRUE;
   bopomofo->fuzzypinyin_k_g = EINA_TRUE;
   bopomofo->fuzzypinyin_l_n = EINA_TRUE;
   bopomofo->fuzzypinyin_l_r = EINA_FALSE;
   bopomofo->fuzzypinyin_n_l = EINA_FALSE;
   bopomofo->fuzzypinyin_r_l = EINA_FALSE;
   bopomofo->fuzzypinyin_sh_s = EINA_FALSE;
   bopomofo->fuzzypinyin_s_sh = EINA_TRUE;
   bopomofo->fuzzypinyin_zh_z = EINA_FALSE;
   bopomofo->fuzzypinyin_z_zh = EINA_TRUE;
   bopomofo->guidekey = 1;
   bopomofo->incompletepinyin = EINA_FALSE;
   bopomofo->initchinese = EINA_TRUE;
   bopomofo->initfull = EINA_FALSE;
   bopomofo->initfullpunct = EINA_TRUE;
   bopomofo->initsimplifiedchinese = EINA_TRUE;
   bopomofo->lookuptableorientation = 0;
   bopomofo->lookuptablepagesize = 5;
   bopomofo->selectkeys = 0;
   bopomofo->specialphrases = EINA_TRUE;
}

#define _config_bopomofo_update NULL;

static void
_config_bopomofo_section_init(struct _config_section *base)
{
   _config_section_add_key_int(base, bopomofo, auxiliaryselectkey_f);
   _config_section_add_key_int(base, bopomofo, auxiliaryselectkey_kp);
   _config_section_add_key_int(base, bopomofo, bopomofokeyboardmapping);
   _config_section_add_key_bool(base, bopomofo, ctrlswitch);
   _config_section_add_key_string(base, bopomofo, dictionaries);
   _config_section_add_key_bool(base, bopomofo, dynamicadjust);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_an_ang);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_ang_an);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_c_ch);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_ch_c);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_en_eng);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_eng_en);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_f_h);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_g_k);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_h_f);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_ing_in);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_in_ing);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_k_g);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_l_n);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_l_r);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_n_l);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_r_l);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_sh_s);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_s_sh);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_zh_z);
   _config_section_add_key_bool(base, bopomofo, fuzzypinyin_z_zh);
   _config_section_add_key_int(base, bopomofo, guidekey);
   _config_section_add_key_bool(base, bopomofo, incompletepinyin);
   _config_section_add_key_bool(base, bopomofo, initchinese);
   _config_section_add_key_bool(base, bopomofo, initfull);
   _config_section_add_key_bool(base, bopomofo, initfullpunct);
   _config_section_add_key_bool(base, bopomofo, initsimplifiedchinese);
   _config_section_add_key_int(base, bopomofo, lookuptableorientation);
   _config_section_add_key_int(base, bopomofo, lookuptablepagesize);
   _config_section_add_key_int(base, bopomofo, selectkeys);
   _config_section_add_key_bool(base, bopomofo, specialphrases);
}

static struct _config_section *
_config_bopomofo_new(struct _config_section *parent)
{
   struct _config_bopomofo *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, bopomofo, parent);
   return base;
}

/*
 * NO SCHEMA AVAILABLE. BASED ON THE SOURCE CODE
 */
struct _config_engine
{
   struct _config_section base;
   struct _config_section *hangul;
   struct _config_section *pinyin;
   struct _config_section *bopomofo;
};

static Eet_Data_Descriptor *
_config_engine_edd_new(Eet_Data_Descriptor *hangul_edd, Eet_Data_Descriptor *pinyin_edd, Eet_Data_Descriptor *bopomofo_edd)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_engine);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_SUB(edd, struct _config_engine, "Hangul", hangul, hangul_edd);
   EET_DATA_DESCRIPTOR_ADD_SUB(edd, struct _config_engine, "Pinyin", pinyin, pinyin_edd);
   EET_DATA_DESCRIPTOR_ADD_SUB(edd, struct _config_engine, "Bopomofo", bopomofo, bopomofo_edd);

   return edd;
}

#define _config_engine_set_defaults NULL;

static Eina_Bool
_config_engine_update(struct _config_section *base)
{
   struct _config_engine *conf = (struct _config_engine *) base;

   if (conf->pinyin && conf->bopomofo)
      return EINA_FALSE;

   INF("Updating 'engine' section");

   if (!conf->pinyin)
     {
        conf->pinyin = _config_pinyin_new(base);
        _config_section_set_defaults(conf->pinyin);
     }

   if (!conf->bopomofo)
     {
        conf->bopomofo = _config_bopomofo_new(base);
        _config_section_set_defaults(conf->bopomofo);
     }

   return EINA_TRUE;
}

static void
_config_engine_section_init(struct _config_section *base)
{
   struct _config_engine *conf = (struct _config_engine *) base;

   _config_section_init(conf->hangul, hangul, base);
   _config_section_init(conf->pinyin, pinyin, base);
   _config_section_init(conf->bopomofo, bopomofo, base);
}

static struct _config_section *
_config_engine_new(struct _config_section *parent)
{
   struct _config_engine *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, engine, parent);
   conf->hangul = _config_hangul_new(base);
   conf->pinyin = _config_pinyin_new(base);
   conf->bopomofo = _config_bopomofo_new(base);

   return base;
}

/*
 * <schema path="/desktop/ibus/" id="org.freedesktop.ibus">
 *    <child schema="org.freedesktop.ibus.general" name="general"/>
 *    <child schema="org.freedesktop.ibus.panel" name="panel"/>
 * </schema>
 */
struct _config_ibus
{
   struct _config_section base;

   struct _config_section *general;
   struct _config_section *panel;
   struct _config_section *engine;
};

static Eet_Data_Descriptor *
_config_ibus_edd_new(Eet_Data_Descriptor *general_edd, Eet_Data_Descriptor *panel_edd, Eet_Data_Descriptor *engine_edd)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_ibus);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_SUB(edd, struct _config_ibus, "general", general, general_edd);
   EET_DATA_DESCRIPTOR_ADD_SUB(edd, struct _config_ibus, "panel", panel, panel_edd);
   EET_DATA_DESCRIPTOR_ADD_SUB(edd, struct _config_ibus, "engine", engine, engine_edd);

   return edd;
}

#define _config_ibus_set_defaults NULL;
#define _config_ibus_update NULL;

static void
_config_ibus_section_init(struct _config_section *base)
{
   struct _config_ibus *conf = (struct _config_ibus *) base;

   _config_section_init(conf->general, general, base);
   _config_section_init(conf->panel, panel, base);
   _config_section_init(conf->engine, engine, base);
}

static struct _config_section *
_config_ibus_new(void)
{
   struct _config_ibus *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, ibus, NULL);
   conf->general = _config_general_new(base);
   conf->panel = _config_panel_new(base);
   conf->engine = _config_engine_new(base);
   return base;
}

/*
 * Weekeyboard specific configuration
 */
struct _config_weekeyboard
{
   struct _config_section base;
   const char *theme;
};

static Eet_Data_Descriptor *
_config_weekeyboard_edd_new(void)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_weekeyboard);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_BASIC(edd, struct _config_weekeyboard, "theme", theme, EET_T_STRING);

   return edd;
}

static void
_config_weekeyboard_set_defaults(struct _config_section *base)
{
   struct _config_weekeyboard *conf = (struct _config_weekeyboard *) base;

   conf->theme = eina_stringshare_add("default");
}

#define _config_weekeyboard_update NULL

static void
_config_weekeyboard_section_init(struct _config_section *base)
{
   _config_section_add_key_string(base, weekeyboard, theme);
}

static struct _config_section *
_config_weekeyboard_new(void)
{
   struct _config_weekeyboard *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, weekeyboard, NULL);
   return base;
}

/*
 * MAIN
 */
struct wkb_ibus_config_eet
{
   const char *path;
   Eldbus_Service_Interface *iface;
   Eina_List *sections;
   Eet_File *file;
};

static void
_config_eet_value_changed(struct wkb_ibus_config_eet *config_eet, struct wkb_config_key *key)
{
   Eldbus_Message *signal;
   Eldbus_Message_Iter *value, *iter;
   const char *sig;

   signal = eldbus_service_signal_new(config_eet->iface, 0);
   iter = eldbus_message_iter_get(signal);
   eldbus_message_iter_arguments_append(iter, "ss", wkb_config_key_section(key), wkb_config_key_id(key));

   sig = wkb_config_key_signature(key);
   switch (*sig)
     {
      case 's':
           {
              value = eldbus_message_iter_container_new(iter, 'v', sig);
              eldbus_message_iter_basic_append(value, 's', wkb_config_key_get_string(key));
              break;
           }
      case 'i':
           {
              value = eldbus_message_iter_container_new(iter, 'v', sig);
              eldbus_message_iter_basic_append(value, 'i', wkb_config_key_get_int(key));
              break;
           }
      case 'b':
           {
              value = eldbus_message_iter_container_new(iter, 'v', sig);
              eldbus_message_iter_basic_append(value, 'b', wkb_config_key_get_bool(key));
              break;
           }
      case 'a':
           {
              char **s, **slist = wkb_config_key_get_string_list(key);
              Eldbus_Message_Iter *array;

              value = eldbus_message_iter_container_new(iter, 'v', "as");
              array = eldbus_message_iter_container_new(value, 'a', "s");

              for (s = slist; *s != NULL; ++s)
                 eldbus_message_iter_arguments_append(array, "s", *s);

              eldbus_message_iter_container_close(value, array);

              free(slist);
              break;
           }
      default:
           {
              value = eldbus_message_iter_container_new(iter, 'v', NULL);
              break;
           }
     }

   eldbus_message_iter_container_close(iter, value);
   eldbus_service_signal_send(config_eet->iface, signal);
}

struct wkb_config_key *
wkb_ibus_config_eet_find_key(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name)
{
   struct wkb_config_key *key;
   struct _config_section *sec;
   Eina_List *node;

   EINA_LIST_FOREACH(config_eet->sections, node, sec)
      if ((key = _config_section_find_key(sec, section, name)))
         return key;

   return NULL;
}

static struct _config_section *
wkb_ibus_config_section_find(struct wkb_ibus_config_eet *config_eet, const char *section)
{
   struct _config_section *sec, *s;
   Eina_List *node;

   EINA_LIST_FOREACH(config_eet->sections, node, s)
      if ((sec = _config_section_find(s, section)))
         return sec;

   return NULL;
}

static Eina_Bool
wkb_ibus_config_section_write(struct wkb_ibus_config_eet *config_eet, struct _config_section *section)
{
   Eina_Bool ret = EINA_TRUE;

   if (!eet_data_write(config_eet->file, section->edd, section->id, section, EINA_TRUE))
     {
        ERR("Error writing section '%s' to Eet file '%s'", section->id, config_eet->path);
        ret = EINA_FALSE;
     }

   DBG("Wrote section '%s' to Eet file '%s'", section->id, config_eet->path);
   return ret;
}

#define wkb_ibus_config_section_read(_eet, _id) \
   do { \
        struct _config_section *sec = NULL; \
        if (!(sec = eet_data_read(_eet->file, _ ## _id ## _edd, #_id))) \
          { \
             INF("Error reading section '%s' from Eet file '%s'. Adding.", #_id , _eet->path); \
             sec = _config_ ## _id ## _new(); \
             _config_section_set_defaults(sec); \
             _eet->sections = eina_list_append(_eet->sections, sec); \
             wkb_ibus_config_section_write(_eet, sec); \
          } \
        else \
          { \
             DBG("Read section '%s' from Eet file '%s'", #_id , _eet->path); \
             _config_section_init(sec, _id, NULL); \
             if (_config_section_update(sec)) \
                wkb_ibus_config_section_write(_eet, sec); \
             _eet->sections = eina_list_append(_eet->sections, sec); \
          } \
   } while (0)

Eina_Bool
wkb_ibus_config_eet_set_value(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name, Eldbus_Message_Iter *value)
{
   Eina_Bool ret = EINA_FALSE;
   struct wkb_config_key *key;
   struct _config_section *sec, *top;

   if (!(sec = wkb_ibus_config_section_find(config_eet, section)))
     {
        ERR("Config section '%s' not found", section);
        goto end;
     }

   if (!(key = _config_section_find_key(sec, section, name)))
     {
        ERR("Config key '%s' not found", name);
        goto end;
     }

   if (!(ret = wkb_config_key_set(key, value)))
     {
        ERR("Error setting new value for key '%s'", wkb_config_key_id(key));
        goto end;
     }

   _config_eet_value_changed(config_eet, key);

   top = _config_section_toplevel(sec);
   ret = wkb_ibus_config_section_write(config_eet, top);
   eet_sync(config_eet->file);

end:
   return ret;
}

Eina_Bool
wkb_ibus_config_eet_get_value(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name, Eldbus_Message_Iter *reply)
{
   struct wkb_config_key *key;

   if (!(key = wkb_ibus_config_eet_find_key(config_eet, section, name)))
     {
        ERR("Config key with id '%s' not found", name);
        return EINA_FALSE;
     }

   return wkb_config_key_get(key, reply);
}

int
wkb_ibus_config_eet_get_value_int(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name)
{
   struct wkb_config_key *key;

   if (!(key = wkb_ibus_config_eet_find_key(config_eet, section, name)))
     {
        ERR("Config key with id '%s' not found", name);
        return -1;
     }

   DBG("Found key: section = <%s> name = <%s>", section, name);

   return wkb_config_key_get_int(key);
}

Eina_Bool
wkb_ibus_config_eet_get_value_bool(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name)
{
   struct wkb_config_key *key;

   if (!(key = wkb_ibus_config_eet_find_key(config_eet, section, name)))
     {
        ERR("Config key with id '%s' not found", name);
        return EINA_FALSE;
     }

   DBG("Found key: section = <%s> name = <%s>", section, name);

   return wkb_config_key_get_bool(key);
}

const char *
wkb_ibus_config_eet_get_value_string(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name)
{
   struct wkb_config_key *key;

   if (!(key = wkb_ibus_config_eet_find_key(config_eet, section, name)))
     {
        ERR("Config key with id '%s' not found", name);
        return NULL;
     }

   DBG("Found key: section = <%s> name = <%s>", section, name);

   return wkb_config_key_get_string(key);
}

char **
wkb_ibus_config_eet_get_value_string_list(struct wkb_ibus_config_eet *config_eet, const char *section, const char *name)
{
   struct wkb_config_key *key;

   if (!(key = wkb_ibus_config_eet_find_key(config_eet, section, name)))
     {
        ERR("Config key with id '%s' not found", name);
        return NULL;
     }

   DBG("Found key: section = <%s> name = <%s>", section, name);

   return wkb_config_key_get_string_list(key);
}

Eina_Bool
wkb_ibus_config_eet_get_values(struct wkb_ibus_config_eet *config_eet, const char *section, Eldbus_Message_Iter *reply)
{
   Eina_Bool ret = EINA_FALSE;
   struct _config_section *sec;
   struct wkb_config_key *key;
   Eina_List *node;
   Eldbus_Message_Iter *dict, *entry;

   if (!(sec = wkb_ibus_config_section_find(config_eet, section)))
     {
        ERR("Config section with id '%s' not found", section);
        goto end;
     }

   dict = eldbus_message_iter_container_new(reply, 'a', "{sv}");

   EINA_LIST_FOREACH(sec->keys, node, key)
     {
        entry = eldbus_message_iter_container_new(dict, 'e', NULL);
        eldbus_message_iter_basic_append(entry, 's', wkb_config_key_id(key));
        ret = wkb_config_key_get(key, entry);
        eldbus_message_iter_container_close(dict, entry);
        if (!ret)
           break;
     }

   eldbus_message_iter_container_close(reply, dict);

end:
   return ret;
}

void
wkb_ibus_config_eet_set_defaults(struct wkb_ibus_config_eet *config_eet)
{
   struct _config_section *sec;
   Eina_List *node;

   EINA_LIST_FREE(config_eet->sections, sec)
      _config_section_free(sec);

   config_eet->sections = eina_list_append(config_eet->sections, _config_ibus_new());
   config_eet->sections = eina_list_append(config_eet->sections, _config_weekeyboard_new());

   EINA_LIST_FOREACH(config_eet->sections, node, sec)
      _config_section_set_defaults(sec);
}

static struct wkb_ibus_config_eet *
_config_eet_init(const char *path, Eldbus_Service_Interface *iface)
{
   struct wkb_ibus_config_eet *eet = calloc(1, sizeof(*eet));
   eet->iface = iface;
   eet->path = eina_stringshare_add(path);

   _hotkey_edd = _config_hotkey_edd_new();
   _general_edd = _config_general_edd_new(_hotkey_edd);
   _panel_edd = _config_panel_edd_new();
   _hangul_edd = _config_hangul_edd_new();
   _pinyin_edd = _config_pinyin_edd_new();
   _bopomofo_edd = _config_bopomofo_edd_new();
   _engine_edd = _config_engine_edd_new(_hangul_edd, _pinyin_edd, _bopomofo_edd);
   _ibus_edd = _config_ibus_edd_new(_general_edd, _panel_edd, _engine_edd);
   _weekeyboard_edd = _config_weekeyboard_edd_new();

   return eet;
}

static Eina_Bool
_config_eet_exists(const char *path)
{
   struct stat buf;
   return stat(path, &buf) == 0;
}

struct wkb_ibus_config_eet *
wkb_ibus_config_eet_new(const char *path, Eldbus_Service_Interface *iface)
{
   struct wkb_ibus_config_eet *eet = _config_eet_init(path, iface);

   if (!(eet->file = eet_open(eet->path, EET_FILE_MODE_READ_WRITE)))
     {
        ERR("Error opening Eet file '%s'", eet->path);
        return EINA_FALSE;
     }

   if (!_config_eet_exists(path))
     {
        Eina_List *node;
        struct _config_section *sec;

        wkb_ibus_config_eet_set_defaults(eet);
        EINA_LIST_FOREACH(eet->sections, node, sec)
           wkb_ibus_config_section_write(eet, sec);

        goto end;
     }

   wkb_ibus_config_section_read(eet, ibus);
   wkb_ibus_config_section_read(eet, weekeyboard);

end:
   eet_sync(eet->file);
   return eet;
}

void
wkb_ibus_config_eet_free(struct wkb_ibus_config_eet *config_eet)
{
   struct _config_section *sec;

   EINA_LIST_FREE(config_eet->sections, sec)
      _config_section_free(sec);

   eina_stringshare_del(config_eet->path);

   eet_data_descriptor_free(_hotkey_edd);
   eet_data_descriptor_free(_general_edd);
   eet_data_descriptor_free(_panel_edd);
   eet_data_descriptor_free(_hangul_edd);
   eet_data_descriptor_free(_pinyin_edd);
   eet_data_descriptor_free(_bopomofo_edd);
   eet_data_descriptor_free(_engine_edd);
   eet_data_descriptor_free(_ibus_edd);
   eet_data_descriptor_free(_weekeyboard_edd);

   _hotkey_edd = NULL;
   _general_edd = NULL;
   _panel_edd = NULL;
   _hangul_edd = NULL;
   _pinyin_edd = NULL;
   _bopomofo_edd = NULL;
   _engine_edd = NULL;
   _ibus_edd = NULL;
   _weekeyboard_edd = NULL;

   eet_close(config_eet->file);
   free(config_eet);
}

static int _init_count = 0;

int
wkb_ibus_config_eet_init(void)
{
   if (_init_count)
      goto end;

   if (!eet_init())
     {
        ERR("Error initializing Eet");
        return 0;
     }

end:
   return ++_init_count;
}

void
wkb_ibus_config_eet_shutdown()
{
   if (_init_count <= 0 || --_init_count > 0)
      return;

   eet_shutdown();
}

void
wkb_ibus_config_eet_dump(struct wkb_ibus_config_eet *eet)
{
   Eina_List *node;
   struct _config_section *sec;

   EINA_LIST_FOREACH(eet->sections, node, sec)
     {
        printf("'%s'\n", sec->id);
        _config_section_dump(sec, "\t");
     }
}
