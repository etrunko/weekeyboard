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
struct _config_engine
{
   struct _config_section base;
   struct _config_section *hangul;
};

static Eet_Data_Descriptor *
_config_engine_edd_new(Eet_Data_Descriptor *hangul_edd)
{
   Eet_Data_Descriptor *edd;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, struct _config_engine);
   edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_SUB(edd, struct _config_engine, "Hangul", hangul, hangul_edd);

   return edd;
}

static void
_config_engine_set_defaults(struct _config_section *base)
{
}

static void
_config_engine_section_init(struct _config_section *base)
{
   struct _config_engine *conf = (struct _config_engine *) base;

   _config_section_init(conf->hangul, hangul, base);
}

static struct _config_section *
_config_engine_new(struct _config_section *parent)
{
   struct _config_engine *conf = calloc(1, sizeof(*conf));
   struct _config_section *base = (struct _config_section *) conf;

   _config_section_init(base, engine, parent);
   conf->hangul = _config_hangul_new(base);
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

static void
_config_ibus_set_defaults(struct _config_section *base)
{
}

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
                 eldbus_message_iter_arguments_append(array, "s", s);

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
      if((sec = _config_section_find(s, section)))
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
   _engine_edd = _config_engine_edd_new(_hangul_edd);
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
   eet_data_descriptor_free(_engine_edd);
   eet_data_descriptor_free(_ibus_edd);
   eet_data_descriptor_free(_weekeyboard_edd);

   _hotkey_edd = NULL;
   _general_edd = NULL;
   _panel_edd = NULL;
   _hangul_edd = NULL;
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
