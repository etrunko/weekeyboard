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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "wkb-ibus-config-key.h"
#include "wkb-log.h"

typedef void (*key_free_cb) (void *);
typedef Eina_Bool (*key_set_cb) (struct wkb_config_key *, Eldbus_Message_Iter *);
typedef Eina_Bool (*key_get_cb) (struct wkb_config_key *, Eldbus_Message_Iter *);

struct wkb_config_key
{
   const char *id;
   const char *signature;
   void *field; /* pointer to the actual struct field */

   key_free_cb free;
   key_set_cb set;
   key_get_cb get;
};

static struct wkb_config_key *
_key_new(const char *id, const char *signature, void *field, key_free_cb free_cb, key_set_cb set_cb, key_get_cb get_cb)
{
   struct wkb_config_key *key = calloc(1, sizeof(*key));
   key->id = eina_stringshare_add(id);
   key->signature = eina_stringshare_add(signature);
   key->field = field;
   key->free = free_cb;
   key->set = set_cb;
   key->get = get_cb;
   return key;
}

#define _key_basic_set(_key, _type) \
   do { \
        _type __value = 0; \
        _type *__field = (_type *) _key->field; \
        if (!eldbus_message_iter_arguments_get(iter, _key->signature, &__value)) \
          { \
             ERR("Error decoding " #_type " value using '%s'", _key->signature); \
             return EINA_FALSE; \
          } \
        *__field = __value; \
        return EINA_TRUE; \
   } while (0)

#define _key_basic_get(_key, _type, _iter) \
   do { \
        _type *__field = (_type *) _key->field; \
       eldbus_message_iter_basic_append(_iter, *_key->signature, *__field); \
       return EINA_TRUE; \
   } while (0)

static Eina_Bool
_key_int_set(struct wkb_config_key *key, Eldbus_Message_Iter *iter)
{
   _key_basic_set(key, int);
}

static Eina_Bool
_key_int_get(struct wkb_config_key *key, Eldbus_Message_Iter *reply)
{
   _key_basic_get(key, int, reply);
}

static Eina_Bool
_key_bool_set(struct wkb_config_key *key, Eldbus_Message_Iter *iter)
{
   _key_basic_set(key, Eina_Bool);
}

static Eina_Bool
_key_bool_get(struct wkb_config_key *key, Eldbus_Message_Iter *reply)
{
   _key_basic_get(key, Eina_Bool, reply);
}

static void
_key_string_free(const char **str)
{
   if (!*str)
      return;

   eina_stringshare_del(*str);
   *str = NULL;
}

static Eina_Bool
_key_string_set(struct wkb_config_key *key, Eldbus_Message_Iter *iter)
{
   const char *str = NULL;
   const char **field;

   if (iter && !eldbus_message_iter_arguments_get(iter, "s", &str))
     {
        ERR("Error decoding string value using 's'");
        return EINA_FALSE;
     }

   if ((field = (const char **) key->field))
      _key_string_free(field);

   if (str && strlen(str))
      *field = eina_stringshare_add(str);

   return EINA_TRUE;
}

static Eina_Bool
_key_string_get(struct wkb_config_key *key, Eldbus_Message_Iter *reply)
{
   _key_basic_get(key, const char *, reply);
}

static void
_key_string_list_free(Eina_List **list)
{
   const char *str;

   if (!*list)
      return;

   EINA_LIST_FREE(*list, str)
      eina_stringshare_del(str);

   eina_list_free(*list);
   *list = NULL;
}

static Eina_Bool
_key_string_list_set(struct wkb_config_key *key, Eldbus_Message_Iter *iter)
{
   const char *str;
   Eina_List *list = NULL;
   Eina_List **field;

   while (iter && eldbus_message_iter_get_and_next(iter, 's', &str))
      list = eina_list_append(list,eina_stringshare_add(str));

   if ((field = (Eina_List **) key->field))
      _key_string_list_free(field);

   *field = list;

   return EINA_TRUE;
}

static Eina_Bool
_key_string_list_get(struct wkb_config_key *key, Eldbus_Message_Iter *reply)
{
   Eina_List *node, **list = (Eina_List **) key->field;
   const char *str;
   Eldbus_Message_Iter *array;

   array = eldbus_message_iter_container_new(reply, 'a', "s");

   EINA_LIST_FOREACH(*list, node, str)
      eldbus_message_iter_basic_append(array, 's', str);

   eldbus_message_iter_container_close(reply, array);

   return EINA_TRUE;
}

/*
 * PUBLIC FUNCTIONS
 */
struct wkb_config_key *
wkb_config_key_int(const char *id, void *field)
{
   return _key_new(id, "i", field, NULL, _key_int_set, _key_int_get);
}

struct wkb_config_key *
wkb_config_key_bool(const char *id, void *field)
{
   return _key_new(id, "b", field, NULL, _key_bool_set, _key_bool_get);
}

struct wkb_config_key *
wkb_config_key_string(const char *id, void *field)
{
   return _key_new(id, "s", field, (key_free_cb) _key_string_free, _key_string_set, _key_string_get);
}

struct wkb_config_key *
wkb_config_key_string_list(const char *id, void *field)
{
   return _key_new(id, "as", field, (key_free_cb) _key_string_list_free, _key_string_list_set, _key_string_list_get);
}

void
wkb_config_key_free(struct wkb_config_key *key)
{
   if (key->free && key->field)
      key->free(key->field);

   eina_stringshare_del(key->id);
   eina_stringshare_del(key->signature);
   free(key);
}

const char *
wkb_config_key_id(struct wkb_config_key *key)
{
   return key->id;
}

const char *
wkb_config_key_signature(struct wkb_config_key *key)
{
   return key->signature;
}

Eina_Bool
wkb_config_key_set(struct wkb_config_key * key, Eldbus_Message_Iter *iter)
{
   if (!key->field || !key->set)
      return EINA_FALSE;

   return key->set(key, iter);
}

Eina_Bool
wkb_config_key_get(struct wkb_config_key *key, Eldbus_Message_Iter *reply)
{
   Eina_Bool ret = EINA_FALSE;
   Eldbus_Message_Iter *value;

   if (!key->field || !key->get)
      return EINA_FALSE;

   value = eldbus_message_iter_container_new(reply, 'v', key->signature);

   if (!(ret = key->get(key, value)))
     {
        ERR("Unexpected error retrieving value for key: '%s'", key->id);
     }

   eldbus_message_iter_container_close(reply, value);

   return ret;
}

